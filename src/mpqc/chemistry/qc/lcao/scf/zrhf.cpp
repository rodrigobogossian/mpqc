#include "zrhf.h"

#include "mpqc/chemistry/qc/lcao/scf/pbc/periodic_soad.h"
#include "mpqc/chemistry/qc/lcao/scf/pbc/periodic_cond_ortho.h"

#include <clocale>
#include <sstream>

#include <tiledarray.h>

#include "mpqc/chemistry/qc/lcao/wfn/wfn.h"
#include "mpqc/util/external/madworld/parallel_file.h"
#include "mpqc/util/external/madworld/parallel_print.h"
#include "mpqc/util/keyval/keyval.h"
#include "mpqc/util/misc/formio.h"

namespace mpqc {
namespace lcao {

zRHF::zRHF(const KeyVal& kv) : PeriodicAOWavefunction(kv), kv_(kv) {}

void zRHF::init(const KeyVal& kv) {
  maxiter_ = kv.value<int64_t>("max_iter", 30);
  bool soad_guess = kv.value<bool>("soad_guess", true);
  print_detail_ = kv.value<bool>("print_detail", false);
  max_condition_num_ = kv.value<double>("max_condition_num", 1.0e8);

  auto& ao_factory = this->ao_factory();
  // retrieve world from periodic ao_factory
  auto& world = ao_factory.world();
  auto unitcell = ao_factory.unitcell();

  auto init_start = mpqc::fenced_now(world);

  if (world.rank() == 0) {
    std::cout << ao_factory << std::endl;
    std::cout << unitcell << std::endl;
  }

  // the unit cell must be electrically neutral
  const auto charge = 0;
  const auto nelectrons = unitcell.total_atomic_number() - charge;
  if (nelectrons % 2 != 0)
    throw InputError("zRHF requires an even number of electrons",
                         __FILE__, __LINE__, "unitcell");
  docc_ = nelectrons / 2;
  dcell_ = unitcell.dcell();

  // retrieve unitcell info from periodic ao_factory
  R_max_ = ao_factory.R_max();
  RJ_max_ = ao_factory.RJ_max();
  RD_max_ = ao_factory.RD_max();
  R_size_ = ao_factory.R_size();
  RJ_size_ = ao_factory.RJ_size();
  RD_size_ = ao_factory.RD_size();

  // read # kpoints from keyval
  nk_ = decltype(nk_)(kv.value<std::vector<int>>("k_points").data());
  k_size_ = 1 + detail::k_ord_idx(nk_(0) - 1, nk_(1) - 1, nk_(2) - 1, nk_);
  ExEnv::out0() << "zRHF computational parameters:" << std::endl;
  ExEnv::out0() << indent << "# of k points in each direction: ["
                << nk_.transpose() << "]" << std::endl;

  T_ = ao_factory.compute(L"<κ|T|λ>");  // Kinetic
  V_ = ao_factory.compute(L"<κ|V|λ>");  // Nuclear-attraction
  S_ = ao_factory.compute(L"<κ|λ>");    // Overlap in real space
  Sk_ = transform_real2recip(S_);       // Overlap in reciprocal space
  H_("mu, nu") =
      T_("mu, nu") + V_("mu, nu");  // One-body hamiltonian in real space

  // compute density matrix using soad/core guess
  if (!soad_guess) {
    if (world.rank() == 0) {
      std::cout << "\nUsing CORE guess for initial Fock ..." << std::endl;
    }
    F_ = H_;
  } else {
    F_ = periodic_fock_soad(world, unitcell, H_, ao_factory);
  }

  // transform Fock from real to reciprocal space
  Fk_ = transform_real2recip(F_);
  // compute orthogonalizer matrix
  X_ = utility::conditioned_orthogonalizer(Sk_, k_size_, max_condition_num_);
  // compute guess density
  D_ = compute_density();

  // set density in periodic ao_factory
  ao_factory.set_density(D_);

  auto init_end = mpqc::fenced_now(world);
  init_duration_ = mpqc::duration_in_s(init_start, init_end);
}

void zRHF::solve(double thresh) {
  auto& ao_factory = this->ao_factory();
  auto& world = ao_factory.world();

  auto iter = 0;
  auto rms = 0.0;
  TArray Ddiff;
  auto converged = false;
  auto ezrhf = 0.0;
  auto ediff = 0.0;

  // compute nuclear-repulsion energy
  const auto erep = ao_factory.unitcell().nuclear_repulsion_energy(RJ_max_);
  if (world.rank() == 0)
    std::cout << "\nNuclear Repulsion Energy: " << erep << std::endl;

  do {
    auto iter_start = mpqc::fenced_now(world);
    ++iter;

    // Save a copy of energy and density
    auto ezrhf_old = ezrhf;
    auto D_old = D_;

    if (print_detail_)
      if (world.rank() == 0) std::cout << "\nIteration: " << iter << "\n";

    // compute zRHF energy
    F_("mu, nu") += H_("mu, nu");
    std::complex<double> e_complex = F_("mu, nu") * D_("mu, nu");
    ezrhf = e_complex.real();

    auto j_start = mpqc::fenced_now(world);
    J_ = ao_factory.compute(L"(μ ν| J|κ λ)");  // Coulomb
    auto j_end = mpqc::fenced_now(world);
    j_duration_ += mpqc::duration_in_s(j_start, j_end);

    auto k_start = mpqc::fenced_now(world);
    K_ = ao_factory.compute(L"(μ ν| K|κ λ)");  // Exchange
    auto k_end = mpqc::fenced_now(world);
    k_duration_ += mpqc::duration_in_s(k_start, k_end);

    // F = H + 2J - K
    F_ = H_;
    F_("mu, nu") += 2.0 * J_("mu, nu") - K_("mu, nu");

    // transform Fock from real to reciprocal space
    auto trans_start = mpqc::fenced_now(world);
    Fk_ = transform_real2recip(F_);
    auto trans_end = mpqc::fenced_now(world);
    trans_duration_ += mpqc::duration_in_s(trans_start, trans_end);

    // compute new density
    auto d_start = mpqc::fenced_now(world);
    D_ = compute_density();
    // update density in periodic ao_factory
    ao_factory.set_density(D_);
    auto d_end = mpqc::fenced_now(world);
    d_duration_ += mpqc::duration_in_s(d_start, d_end);

    // compute difference with last iteration
    ediff = ezrhf - ezrhf_old;
    Ddiff("mu, nu") = D_("mu, nu") - D_old("mu, nu");
    rms = Ddiff("mu, nu").norm();
    if ((rms <= thresh) || fabs(ediff) <= thresh) converged = true;

    auto iter_end = mpqc::fenced_now(world);
    auto iter_duration = mpqc::duration_in_s(iter_start, iter_end);
    scf_duration_ += iter_duration;

    // Print out information
    if (print_detail_) {
      if (world.rank() == 0) {
        std::cout << "\nzRHF Energy: " << ezrhf << "\n"
                  << "Total Energy: " << ezrhf + erep << "\n"
                  << "Delta(E): " << ediff << "\n"
                  << "RMS(D): " << rms << "\n"
                  << "Coulomb Build Time: "
                  << mpqc::duration_in_s(j_start, j_end) << " s\n"
                  << "Exchange Build Time: "
                  << mpqc::duration_in_s(k_start, k_end) << " s\n"
                  << "Transform Fock (Real->Recip) Time: "
                  << mpqc::duration_in_s(trans_start, trans_end) << " s\n"
                  << "Density Time: " << mpqc::duration_in_s(d_start, d_end)
                  << " s\n"
                  << "Iteration Time: " << iter_duration << " s\n";
      }
    } else {
      std::string niter = "Iter", nEle = "E(HF)", nTot = "E(tot)",
                  nDel = "Delta(E)", nRMS = "RMS(D)", nT = "Time(s)";
      if (world.rank() == 0) {
        if (iter == 1)
          std::cout << mpqc::printf("\n\n %4s %20s %20s %20s %20s %20s\n",
                                    niter.c_str(), nEle.c_str(), nTot.c_str(),
                                    nDel.c_str(), nRMS.c_str(), nT.c_str());
        std::cout << mpqc::printf(
            " %4d %20.12f %20.12f %20.12f %20.12f %20.3f\n", iter, ezrhf,
            ezrhf + erep, ediff, rms, iter_duration);
      }
    }

  } while ((iter < maxiter_) && (!converged));

  // save total energy to energy no matter if zRHF converges
  energy_ = ezrhf + erep;

  if (!converged) {
    // TODO read a keyval value to determine
    if (1)
      ExEnv::out0() << "\nzRHF: SCF did not converge!\n\n";
    else
      throw MaxIterExceeded("zRHF: SCF did not converge", __FILE__, __LINE__,
                            maxiter_);
  } else {
      ExEnv::out0() << "\nPeriodic Hartree-Fock iterations have converged!\n";
  }

  if (world.rank() == 0) {
    std::cout << "\nTotal Periodic Hartree-Fock energy = " << energy_
              << std::endl;

    if (print_detail_) {
      Eigen::IOFormat fmt(5);
      std::cout << "\n k | orbital energies" << std::endl;
      for (auto k = 0; k < k_size_; ++k) {
        std::cout << k << " | " << eps_[k].real().transpose().format(fmt)
                  << std::endl;
      }
    }

    // print out timings
    std::cout << mpqc::printf("\nTime(s):\n");
    std::cout << mpqc::printf("\tInit:                %20.3f\n",
                              init_duration_);
    std::cout << mpqc::printf("\tCoulomb term:        %20.3f\n", j_duration_);
    std::cout << mpqc::printf("\tExchange term:       %20.3f\n", k_duration_);
    std::cout << mpqc::printf("\tReal->Recip trans:   %20.3f\n",
                              trans_duration_);
    std::cout << mpqc::printf("\tDiag + Density:      %20.3f\n", d_duration_);
    std::cout << mpqc::printf("\tTotal:               %20.3f\n\n",
                              scf_duration_);
  }
}

zRHF::TArray zRHF::compute_density() {
  auto& ao_factory = this->ao_factory();
  auto& world = ao_factory.world();

  eps_.resize(k_size_);
  C_.resize(k_size_);

  auto tr0 = Fk_.trange().data()[0];
  using ::mpqc::lcao::detail::extend_trange1;
  auto tr1 = extend_trange1(tr0, RD_size_);

  auto fock_eig = array_ops::array_to_eigen(Fk_);
  for (auto k = 0; k < k_size_; ++k) {
    // Get orthogonalizer
    auto X = X_[k];
    // Symmetrize Fock
    auto F = fock_eig.block(0, k * tr0.extent(), tr0.extent(), tr0.extent());
    Matrixz F_twice = F + F.transpose().conjugate();
    // When k=0 (gamma point), reverse phase factor of complex values
    if (k_size_ > 1 && k_size_ % 2 == 1 && k == ((k_size_ - 1) / 2))
      F_twice = reverse_phase_factor(F_twice);
    F = 0.5 * F_twice;

    // Orthogonalize Fock matrix: F' = Xt * F * X
    Matrixz Xt = X.transpose().conjugate();
    auto XtF = Xt * F;
    auto Ft = XtF * X;

    // Diagonalize F'
    Eigen::SelfAdjointEigenSolver<Matrixz> comp_eig_solver(Ft);
    eps_[k] = comp_eig_solver.eigenvalues().cast<std::complex<double>>();
    Matrixz Ctemp = comp_eig_solver.eigenvectors();

    // When k=0 (gamma point), reverse phase factor of complex eigenvectors
    if (k_size_ > 1 && k_size_ % 2 == 1 && k == ((k_size_ - 1) / 2))
      Ctemp = reverse_phase_factor(Ctemp);

    C_[k] = X * Ctemp;
  }

  Matrixz result_eig(tr0.extent(), tr1.extent());
  result_eig.setZero();
  for (auto R = 0; R < RD_size_; ++R) {
    auto vec_R = detail::direct_vector(R, RD_max_, dcell_);
    for (auto k = 0; k < k_size_; ++k) {
      auto vec_k = detail::k_vector(k, nk_, dcell_);
      auto C_occ = C_[k].leftCols(docc_);
      auto D_real = C_occ.conjugate() * C_occ.transpose();
      auto exponent =
          std::exp(I * vec_k.dot(vec_R)) / double(nk_(0) * nk_(1) * nk_(2));
      auto D_comp = exponent * D_real;
      result_eig.block(0, R * tr0.extent(), tr0.extent(), tr0.extent()) +=
          D_comp;
    }
  }

  auto result = array_ops::eigen_to_array<Tile, TA::SparsePolicy>(
      world, result_eig, tr0, tr1);
  return result;
}

zRHF::TArray zRHF::transform_real2recip(TArray& matrix) {
  TArray result;
  auto tr0 = matrix.trange().data()[0];
  auto tr1 = detail::extend_trange1(tr0, k_size_);
  auto& world = matrix.world();

  // Perform real->reciprocal transformation with Eigen
  // TODO: perform it with TA
  auto matrix_eig = array_ops::array_to_eigen(matrix);
  Matrixz result_eig(tr0.extent(), tr1.extent());
  result_eig.setZero();

  auto threshold = std::numeric_limits<double>::epsilon();
  for (auto R = 0; R < R_size_; ++R) {
    auto bmat =
        matrix_eig.block(0, R * tr0.extent(), tr0.extent(), tr0.extent());
    if (bmat.norm() < bmat.size() * threshold)
      continue;
    else {
      auto vec_R = detail::direct_vector(R, R_max_, dcell_);
      for (auto k = 0; k < k_size_; ++k) {
        auto vec_k = detail::k_vector(k, nk_, dcell_);
        auto exponent = std::exp(I * vec_k.dot(vec_R));
        result_eig.block(0, k * tr0.extent(), tr0.extent(), tr0.extent()) +=
            bmat * exponent;
      }
    }
  }

  result = array_ops::eigen_to_array<Tile, TA::SparsePolicy>(world, result_eig,
                                                             tr0, tr1);

  return result;
}

Matrixz zRHF::reverse_phase_factor(Matrixz& mat0) {
  Matrixz result(mat0);

  for (auto row = 0; row < mat0.rows(); ++row) {
    for (auto col = 0; col < mat0.cols(); ++col) {
      std::complex<double> comp0 = mat0(row, col);

      double norm = std::abs(comp0);
      if (norm == 0.0) {
        result(row, col) = comp0;
      } else {
        double real = comp0.real();
        double imag = comp0.imag();

        double phi = std::atan(imag / real);

        double R;
        if (std::cos(phi) != 0.0) {
          R = real / std::cos(phi);
        } else {
          R = imag / std::sin(phi);
        }

        std::complex<double> comp1 = comp0 * std::exp(-1.0 * I * phi);

        result(row, col) = comp1;
      }
    }
  }

  return result;
}

void zRHF::obsolete() { Wavefunction::obsolete(); }

bool zRHF::can_evaluate(Energy* energy) {
  // can only evaluate the energy
  return energy->order() == 0;
}

void zRHF::evaluate(Energy* result) {
  if(!this->computed()){
    init(kv_);
    solve(result->target_precision(0));
    this->computed_ = true;
    set_value(result, energy_);
  }
}


}  // namespace lcao
}  // namespace mpqc