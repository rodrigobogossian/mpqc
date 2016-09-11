#include "../common/namespaces.h"
#include "../common/typedefs.h"

#include "../include/tiledarray.h"

#include "../clustering/kmeans.h"
#include "../utility/make_array.h"

#include "../molecule/atom.h"
#include "../molecule/atom_based_cluster.h"
#include "../molecule/clustering_functions.h"
#include "../molecule/make_clusters.h"
#include "../molecule/molecule.h"

#include "../scf/scf.h"

#include "../basis/atom_basisset.h"
#include "../basis/basis.h"
#include "../basis/basis_set.h"

#include "../integrals/atomic_integral.h"
#include "../integrals/integrals.h"
#include "../utility/cc_utility.h"
#include <mpqc/chemistry/qc/expression/orbital_registry.h>
#include <mpqc/chemistry/qc/f12/mp2f12.h>

#include "../scf/eigen_solve_density_builder.h"
#include "../scf/traditional_df_fock_builder.h"
#include "../ta_routines/array_to_eigen.h"
#include "../utility/array_info.h"
#include "../utility/parallel_file.h"
#include "../utility/time.h"

#include "../scf/traditional_four_center_fock_builder.h"
#include <mpqc/chemistry/qc/mbpt/mp2.h>

#include <locale>
#include <memory>

using namespace mpqc;
namespace ints = mpqc::integrals;

TA::TensorD ta_pass_through(TA::TensorD &&ten) { return std::move(ten); }

int main(int argc, char *argv[]) {
  auto &world = madness::initialize(argc, argv);
  std::wcout.imbue(std::locale("en_US.UTF-8"));
  std::string mol_file = "";
  std::string basis_name = "";
  std::string aux_basis_name = "";
  std::string df_basis_name = "";
  std::size_t mo_block_size = 0;
  std::size_t ao_block_size = 0;
  int nclusters = 0;
  std::cout << std::setprecision(15);
  double threshold = 1e-30;
  double well_sep_threshold = 0.1;
  integrals::QQR::well_sep_threshold(well_sep_threshold);
  if (argc == 8) {
    mol_file = argv[1];
    basis_name = argv[2];
    aux_basis_name = argv[3];
    df_basis_name = argv[4];
    nclusters = std::stoi(argv[5]);
    mo_block_size = std::stoi(argv[6]);
    ao_block_size = std::stoi(argv[7]);
  } else {
    std::cout << "input is $./program mol_file basis_file aux_basis df_basis "
                 "nclusters mo_block_size ao_block_size\n";
    return 0;
  }
  TiledArray::SparseShape<float>::threshold(threshold);

  if (world.rank() == 0) {
    std::cout << "Molecule: " << mol_file << std::endl;
    std::cout << "N Cluster: " << nclusters << std::endl;
    std::cout << "OBS: " << basis_name << std::endl;
    std::cout << "DFBS: " << df_basis_name << std::endl;
    std::cout << "AUXBS: " << aux_basis_name << std::endl;
    std::cout << "MO Block Size: " << mo_block_size << std::endl;
    std::cout << "AO Block Size: " << ao_block_size << std::endl;
  }

  char *xyz_file_buffer;
  utility::parallel_read_file(world, mol_file, xyz_file_buffer);
  std::stringstream xyz_file_stream;
  xyz_file_stream << xyz_file_buffer;
  delete[] xyz_file_buffer;

  auto mol = mpqc::molecule::Molecule(xyz_file_stream);
  auto clustered_mol = molecule::kmeans(mol.clusterables(), nclusters);

  auto repulsion_energy = clustered_mol.nuclear_repulsion();
  auto occ = clustered_mol.occupation(0);

  basis::BasisSet bs(basis_name);
  basis::Basis basis =
      basis::parallel_construct_basis(world, bs, clustered_mol);
  basis = reblock(basis, cc::reblock_basis, ao_block_size);

  basis::BasisSet dfbs(df_basis_name);
  basis::Basis df_basis =
      basis::parallel_construct_basis(world, dfbs, clustered_mol);
  df_basis = reblock(df_basis, cc::reblock_basis, ao_block_size);

  basis::BasisSet abs(aux_basis_name);
  basis::Basis abs_basis =
      basis::parallel_construct_basis(world, abs, clustered_mol);
  abs_basis = reblock(abs_basis, cc::reblock_basis, ao_block_size);

  basis::Basis ri_basis = basis.join(abs_basis);
  ri_basis = reblock(ri_basis, cc::reblock_basis, ao_block_size);

  utility::parallel_print_range_info(world, basis.create_trange1(),
                                     "OBS Basis");
  utility::parallel_print_range_info(world, df_basis.create_trange1(),
                                     "DF Basis");
  utility::parallel_print_range_info(world, abs_basis.create_trange1(),
                                     "AUX Basis");
  utility::parallel_print_range_info(world, ri_basis.create_trange1(),
                                     "RI Basis");

  auto bs_registry = std::make_shared<OrbitalBasisRegistry>();
  bs_registry->add(OrbitalIndex(L"κ"), basis);
  bs_registry->add(OrbitalIndex(L"Κ"), df_basis);
  bs_registry->add(OrbitalIndex(L"α"), abs_basis);
  bs_registry->add(OrbitalIndex(L"ρ"), ri_basis);

  f12::GTGParams gtg_params(1.0, 6);

  auto param = gtg_params.compute();

  if (world.rank() == 0) {
    for (auto &pair : param) {
      std::cout << pair.first << " " << pair.second << std::endl;
    }
  }

  libint2::initialize();

  integrals::AtomicIntegral<TA::TensorD, TA::SparsePolicy> ao_int(
      world, ta_pass_through,
      std::make_shared<molecule::Molecule>(clustered_mol), bs_registry, param);

  auto time0 = mpqc_time::fenced_now(world);
  // Overlap ints
  auto S = ao_int.compute(L"(κ|λ)");
  auto H = ao_int.compute(L"(κ|H|λ)");

  auto inv = ao_int.compute(L"( Κ | G| Λ )");
  auto eri3 = ao_int.compute(L"( Κ | G|κ1 λ1)");
  scf::DFFockBuilder<decltype(eri3)> builder(inv, eri3);

  const auto bs_array = utility::make_array(basis, basis);
  auto multi_pool =
      ints::make_1body_shr_pool("emultipole1", basis, clustered_mol);
  auto r_xyz = ints::sparse_xyz_integrals(world, multi_pool, bs_array);

  // doing four center HF
  auto db = scf::ESolveDensityBuilder(S, r_xyz, occ / 2, nclusters, 0.0,
                                      "cholesky inverse", false);
  scf::ClosedShellSCF scf(H, S, repulsion_energy, std::move(builder),
                          std::move(db));
  scf.solve(50, 1e-10);

  auto time1 = mpqc_time::fenced_now(world);
  auto hf_time = mpqc_time::duration_in_s(time0, time1);

  // obs fock build
  std::size_t all = S.trange().elements().extent()[0];

  // attention!! occ has to be blocked by 1!!
  auto tre = TRange1Engine(occ / 2, all, 1, mo_block_size, 0);

  auto F = scf.fock();
  ao_int.registry().insert(Formula(L"(μ|F|ν)[df]"), F);

  // mp2
  time0 = mpqc_time::fenced_now(world);

  // solve Coefficient
  std::size_t n_frozen_core = 0;
  auto F_eig = array_ops::array_to_eigen(F);
  auto S_eig = array_ops::array_to_eigen(S);
  Eig::GeneralizedSelfAdjointEigenSolver<decltype(S_eig)> es(F_eig, S_eig);
  Eig::VectorXd ens = es.eigenvalues().bottomRows(S_eig.rows() - n_frozen_core);

  Eig::MatrixXd C_all = es.eigenvectors();
  decltype(S_eig) C_occ = C_all.block(0, 0, S_eig.rows(), occ / 2);
  decltype(S_eig) C_occ_corr =
      C_all.block(0, n_frozen_core, S_eig.rows(), occ / 2 - n_frozen_core);
  decltype(S_eig) C_vir = C_all.rightCols(S_eig.rows() - occ / 2);

  auto tr_0 = eri3.trange().data().back();
  auto tr_all = tre.get_all_tr1();
  auto tr_i0 = tre.get_occ_tr1();
  auto tr_vir = tre.get_vir_tr1();

  utility::parallel_print_range_info(world, tr_i0, "Occ");
  utility::parallel_print_range_info(world, tr_vir, "Vir");
  utility::parallel_print_range_info(world, tr_all, "Obs");

  auto Ci = array_ops::eigen_to_array<TA::Tensor<double>>(world, C_occ_corr,
                                                          tr_0, tr_i0);
  auto Cv =
      array_ops::eigen_to_array<TA::Tensor<double>>(world, C_vir, tr_0, tr_vir);
  auto Call =
      array_ops::eigen_to_array<TA::Tensor<double>>(world, C_all, tr_0, tr_all);

  auto orbital_registry = OrbitalSpaceRegistry<decltype(Ci)>();
  using OrbitalSpace = OrbitalSpace<decltype(Ci)>;
  auto occ_space = OrbitalSpace(OrbitalIndex(L"m"), OrbitalIndex(L"κ"), Ci);
  orbital_registry.add(occ_space);

  auto corr_occ_space =
      OrbitalSpace(OrbitalIndex(L"i"), OrbitalIndex(L"κ"), Ci);
  orbital_registry.add(corr_occ_space);

  auto vir_space = OrbitalSpace(OrbitalIndex(L"a"), OrbitalIndex(L"κ"), Cv);
  orbital_registry.add(vir_space);

  auto obs_space = OrbitalSpace(OrbitalIndex(L"p"), OrbitalIndex(L"κ"), Call);
  orbital_registry.add(obs_space);

  auto sp_orbital_registry =
      std::make_shared<decltype(orbital_registry)>(orbital_registry);

  // clean atomic integral
  ao_int.registry().clear();

  auto lcao_factory = integrals::LCAOFactory<TA::TensorD, TA::SparsePolicy>(
      ao_int, sp_orbital_registry);
  //    lcao_factory.atomic_integral().registry().print_formula();

  // test mp2
  // df-mp2

  {
    auto mp2 = mbpt::MP2<TA::TensorD, TA::SparsePolicy>(
        lcao_factory, ens, std::make_shared<TRange1Engine>(tre));
    mp2.compute_df();
  }
  time1 = mpqc_time::fenced_now(world);
  auto mp2_time = mpqc_time::duration_in_s(time0, time1);

  //    lcao_factory.atomic_integral().registry().print_formula();
  time0 = mpqc_time::fenced_now(world);
  // CABS fock build

  // integral

  auto S_cabs = ao_int.compute(L"(α|β)");
  auto S_ribs = ao_int.compute(L"(ρ|σ)");
  auto S_obs_ribs = ao_int.compute(L"(μ|σ)");
  auto S_obs = scf.overlap();

  //    std::cout << S_obs << std::endl;
  //    std::cout << S_ribs << std::endl;

  // construct cabs
  decltype(S_obs) C_cabs, C_ri;
  {
    auto S_obs_eigen = array_ops::array_to_eigen(S_obs);
    //        MatrixD X_obs_eigen_inv =
    //        MatrixD(Eigen::LLT<MatrixD>(S_obs_eigen).matrixL()).inverse();
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> es(S_obs_eigen);
    MatrixD X_obs_eigen_inv = es.operatorInverseSqrt();

    auto S_ribs_eigen = array_ops::array_to_eigen(S_ribs);
    //        MatrixD X_ribs_eigen_inv =
    //        MatrixD(Eigen::LLT<MatrixD>(S_ribs_eigen).matrixL()).inverse();
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> es2(S_ribs_eigen);
    MatrixD X_ribs_eigen_inv = es2.operatorInverseSqrt();
    MatrixD S_obs_ribs_eigen = array_ops::array_to_eigen(S_obs_ribs);
    MatrixD S_obs_ribs_ortho_eigen =
        X_obs_eigen_inv.transpose() * S_obs_ribs_eigen * X_ribs_eigen_inv;
    Eigen::JacobiSVD<MatrixD> svd(S_obs_ribs_ortho_eigen, Eigen::ComputeFullV);
    MatrixD V_eigen = svd.matrixV();
    size_t nbf_ribs = S_obs_ribs_ortho_eigen.cols();
    auto nbf_cabs = nbf_ribs - svd.nonzeroSingularValues();
    MatrixD Vnull(nbf_ribs, nbf_cabs);
    Vnull = V_eigen.block(0, svd.nonzeroSingularValues(), nbf_ribs, nbf_cabs);
    MatrixD C_cabs_eigen = X_ribs_eigen_inv * Vnull;

    auto tr_cabs = S_cabs.trange().data()[0];
    auto tr_ribs = S_ribs.trange().data()[0];
    auto tr_cabs_mo =
        tre.compute_range(tr_cabs.elements().second, mo_block_size);
    auto tr_ribs_mo =
        tre.compute_range(tr_ribs.elements().second, mo_block_size);

    utility::parallel_print_range_info(world, tr_cabs_mo, "CABS MO");
    utility::parallel_print_range_info(world, tr_ribs_mo, "RIBS MO");

    C_cabs = array_ops::eigen_to_array<TA::TensorD>(world, C_cabs_eigen,
                                                    tr_ribs, tr_cabs_mo);
    C_ri = array_ops::eigen_to_array<TA::TensorD>(world, X_ribs_eigen_inv,
                                                  tr_ribs, tr_ribs_mo);

    auto C_cabs_space =
        OrbitalSpace(OrbitalIndex(L"a'"), OrbitalIndex(L"ρ"), C_cabs);
    auto C_ribs_space =
        OrbitalSpace(OrbitalIndex(L"P'"), OrbitalIndex(L"ρ"), C_ri);
    lcao_factory.orbital_space()->add(C_cabs_space);
    lcao_factory.orbital_space()->add(C_ribs_space);

    //        std::cout << C_cabs << std::endl;
    //        std::cout << C_ri << std::endl;
  }

  f12::MP2F12<TA::TensorD> mp2f12(lcao_factory,
                                  std::make_shared<TRange1Engine>(tre), ens);

  //    mp2f12.compute_mp2_f12();
  //    lcao_factory.registry().clear();
  //    ao_int.registry().clear();
  mp2f12.compute_mp2_f12_df();
  time1 = mpqc_time::fenced_now(world);
  auto mp2f12_time = mpqc_time::duration_in_s(time0, time1);

  //    ao_int.registry().print_formula(world);
  //    lcao_factory.registry().print_formula(world);

  utility::print_par(world, "HF Time: ", hf_time, " S\n");
  utility::print_par(world, "MP2 Time: ", mp2_time, " S\n");
  utility::print_par(world, "MP2F12 Time: ", mp2f12_time, " S\n");

  madness::finalize();
  libint2::finalize();
  return 0;
}
