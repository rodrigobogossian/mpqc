#ifndef MPQC4_SRC_MPQC_CHEMISTRY_QC_SCF_PBC_PERIODIC_FOUR_CENTER_FOCK_BUILDER_H_
#define MPQC4_SRC_MPQC_CHEMISTRY_QC_SCF_PBC_PERIODIC_FOUR_CENTER_FOCK_BUILDER_H_

#include "mpqc/chemistry/qc/lcao/factory/periodic_ao_factory.h"
#include "mpqc/chemistry/qc/lcao/scf/builder.h"

#include <mutex>

namespace mpqc {
namespace scf {

/// ReferencePeriodicFourCenterFockBuilder is a reference implemenation in zRHF
/// that uses 4-center (stored or direct) integrals.

/// @warning This is a very inefficient builder when used with direct integrals:
/// the sparsity of density matrix is not considered and sum over unit cells for
/// coulomb interaction is not parallelized. It is only useful for reference
/// computation
template <typename Tile, typename Policy, typename Factory>
class ReferencePeriodicFourCenterFockBuilder
    : public PeriodicFockBuilder<Tile, Policy> {
 public:
  using array_type = typename PeriodicFockBuilder<Tile, Policy>::array_type;

  ReferencePeriodicFourCenterFockBuilder(Factory &ao_factory)
      : ao_factory_(ao_factory) {}

  ~ReferencePeriodicFourCenterFockBuilder() {}

  array_type operator()(array_type const &D, double, bool) override {
    // feed density matrix to Factory
    ao_factory_.set_density(D);

    array_type J, K, G;
    J = ao_factory_.compute_direct(L"(μ ν| J|κ λ)");
    K = ao_factory_.compute_direct(L"(μ ν| K|κ λ)");

    G("mu, nu") = 2.0 * J("mu, nu") - K("mu, nu");

    return G;
  }

  void register_fock(const array_type &fock,
                     FormulaRegistry<array_type> &registry) override {
    registry.insert(Formula(L"(κ|F|λ)"), fock);
  }

 private:
  Factory &ao_factory_;
};

/// PeriodicFourCenterFockBuilder is an integral-direct implementation of
/// PeriodicFockBuilder with periodic Gaussian AO basis that uses 4-center
/// integrals. This builder takes advantage of shell-level screening and
/// parallelized summation over unit cells for coulomb interaction
template <typename Tile, typename Policy>
class PeriodicFourCenterFockBuilder
    : public PeriodicFockBuilder<Tile, Policy>,
      public madness::WorldObject<PeriodicFourCenterFockBuilder<Tile, Policy>> {
 public:
  using array_type = typename PeriodicFockBuilder<Tile, Policy>::array_type;
  using const_data_ptr = typename Tile::allocator_type::const_pointer;

  using WorldObject_ =
      madness::WorldObject<PeriodicFourCenterFockBuilder<Tile, Policy>>;
  using PeriodicFourCenterFockBuilder_ =
      PeriodicFourCenterFockBuilder<Tile, Policy>;

  using Engine = ::mpqc::lcao::gaussian::ShrPool<libint2::Engine>;
  using Qmatrix = ::mpqc::lcao::gaussian::Qmatrix;
  using Basis = ::mpqc::lcao::gaussian::Basis;
  using BasisVector = std::vector<Basis>;
  using Shell = typename ::mpqc::lcao::gaussian::Shell;
  using ShellVec = typename ::mpqc::lcao::gaussian::ShellVec;
  using shellpair_list_t = std::unordered_map<size_t, std::vector<size_t>>;
  using func_offset_list =
      std::unordered_map<size_t, std::tuple<size_t, size_t>>;

  PeriodicFourCenterFockBuilder(
      madness::World &world, std::shared_ptr<const Basis> bra_basis,
      std::shared_ptr<const Basis> ket_basis, Vector3d &dcell, Vector3i &R_max,
      Vector3i &RJ_max, Vector3i &RD_max, int64_t R_size, int64_t RJ_size,
      int64_t RD_size, bool compute_J, bool compute_K,
      std::string screen = "schwarz", double screen_threshold = 1.0e-20,
      double shell_pair_threshold = 1.0e-12)
      : WorldObject_(world),
        compute_J_(compute_J),
        compute_K_(compute_K),
        screen_(screen),
        screen_threshold_(screen_threshold),
        dcell_(dcell),
        R_max_(R_max),
        RJ_max_(RJ_max),
        RD_max_(RD_max),
        R_size_(R_size),
        RJ_size_(RJ_size),
        RD_size_(RD_size),
        bra_basis_(bra_basis),
        ket_basis_(ket_basis),
        shell_pair_threshold_(shell_pair_threshold) {
    assert(bra_basis_ != nullptr && "No bra basis is provided");
    assert(ket_basis_ != nullptr && "No ket basis is provided");
    assert((compute_J_ || compute_K_) && "No Coulomb && No Exchange");
    // WorldObject mandates this is called from the ctor
    WorldObject_::process_pending();

    // initialize compound bases, engines, and screeners
    init();
  }

  ~PeriodicFourCenterFockBuilder() {}

  array_type operator()(array_type const &D, double target_precision,
                        bool is_density_diagonal) override {
    // validate preconditions
    auto trange = D.trange();
    auto elements_range = trange.elements_range();
    auto tiles_range = trange.tiles_range();
    for (auto RJ = 0; RJ != RJ_size_; ++RJ) {
      auto ntilesRJ = basisRJ_[RJ]->nclusters();
      auto ntilesRD = basisRD_[RJ]->nclusters();
      auto nbf_RJ = basisRJ_[RJ]->nfunctions();
      auto nbf_RD = basisRD_[RJ]->nfunctions();
      assert(elements_range.extent(0) == nbf_RJ);
      assert(elements_range.extent(1) == nbf_RD);
      assert(tiles_range.extent(0) == ntilesRJ);
      assert(tiles_range.extent(1) == ntilesRD);
    }
    return compute_JK_abcd(D, target_precision, is_density_diagonal);
  }

  void register_fock(const array_type &fock,
                     FormulaRegistry<array_type> &registry) override {
    registry.insert(Formula(L"(κ|F|λ)"), fock);
  }

  array_type compute_JK_abcd(array_type const &D, double target_precision,
                             bool is_density_diagonal) const {
    // Copy D and make it replicated.
    array_type D_repl;
    D_repl("i,j") = D("i,j");
    D_repl.make_replicated();
    repl_pmap_D_ = D_repl.pmap();
    trange_D_ = D_repl.trange();

    // prepare input data
    auto &compute_world = this->get_world();
    const auto me = compute_world.rank();
    const auto nproc = compute_world.nproc();
    target_precision_ = target_precision;
    is_density_diagonal_ = is_density_diagonal;
    if (is_density_diagonal) assert(RD_size_ == 1 && "RD size is incorrect");

    // # of tiles per basis
    auto ntiles0 = bra_basis_->nclusters();
    auto ntiles1 = bra_basis_->nclusters() * R_size_;
    auto ntiles2 = ket_basis_->nclusters();
    auto ntiles3 = ket_basis_->nclusters() * RD_size_;

    auto t0 = mpqc::fenced_now(compute_world);

    // make shell block norm of D
    assert(RJ_size_ > 0 && RJ_size_ % 2 == 1);
    auto ref_uc = (RJ_size_ - 1) / 2;
    auto shblk_norm_D =
        compute_shellblock_norm(*ket_basis_, *(basisRD_[ref_uc]), D_repl);
    shblk_norm_D.make_replicated();  // make sure it is replicated

    // initialize engines
    {
      using ::mpqc::lcao::gaussian::make_engine_pool;
      auto oper_type = libint2::Operator::coulomb;
      const auto basis0 = *bra_basis_;
      const auto basisR = *basisR_;
      const auto basisRJ = *(basisRJ_[ref_uc]);
      const auto basisRD = *(basisRD_[ref_uc]);
      if (compute_J_) {
        j_engines_ = make_engine_pool(
            oper_type,
            utility::make_array_of_refs(basis0, basisR, basisRJ, basisRD),
            libint2::BraKet::xx_xx);
      }
      if (compute_K_) {
        k_engines_ = make_engine_pool(
            oper_type,
            utility::make_array_of_refs(basis0, basisRJ, basisR, basisRD),
            libint2::BraKet::xx_xx);
      }
    }

    J_num_ints_computed_ = 0;
    K_num_ints_computed_ = 0;

    auto empty = TA::Future<Tile>(Tile());

    for (auto tile2 = 0ul, tile0123 = 0ul; tile2 != ntiles2; ++tile2) {
      for (auto tile3 = 0ul; tile3 != ntiles3; ++tile3) {
        if (is_density_diagonal_ && tile3 != tile2) continue;
        const auto RD = tile3 / ntiles2;
        auto D_RJRD = (D_repl.is_zero({tile2, tile3}))
                          ? empty
                          : D_repl.find({tile2, tile3});
        auto norm_D_RJRD = (shblk_norm_D.is_zero({tile2, tile3}))
                               ? empty
                               : shblk_norm_D.find({tile2, tile3});
        if (D_RJRD.get().data() == nullptr ||
            norm_D_RJRD.get().data() == nullptr)
          continue;

        for (auto tile0 = 0ul; tile0 != ntiles0; ++tile0) {
          for (auto tile1 = 0ul; tile1 != ntiles1; ++tile1) {
            const auto R = tile1 / ntiles0;

            for (auto RJ = 0; RJ != RJ_size_; ++RJ, ++tile0123) {
              if (tile0123 % nproc == me)
                WorldObject_::task(
                    me, &PeriodicFourCenterFockBuilder_::compute_task_abcd,
                    D_RJRD, norm_D_RJRD, R, RJ, RD,
                    std::array<size_t, 4>{{tile0, tile1, tile2, tile3}});
            }
          }
        }
      }
    }

    compute_world.gop.fence();

    // cleanup
    j_engines_.reset();
    k_engines_.reset();

    // print out # of ints computed per MPI process
    ExEnv::out0() << "\nIntegrals per node:" << std::endl;
    for (auto i = 0; i < compute_world.nproc(); ++i) {
      if (me == i) {
        if (compute_J_) {
          ExEnv::outn() << indent << "Ints for J on node(" << i
                        << "): " << J_num_ints_computed_ << std::endl;
        }
        if (compute_K_) {
          ExEnv::outn() << indent << "Ints for K on node(" << i
                        << "): " << K_num_ints_computed_ << std::endl;
        }
      }
      compute_world.gop.fence();
    }
    ExEnv::out0() << std::endl;

    if (repl_pmap_D_->is_replicated() && compute_world.size() > 1) {
      for (const auto &local_tile : local_fock_tiles_) {
        const auto ij = local_tile.first;
        const auto proc01 = dist_pmap_fock_->owner(ij);
        WorldObject_::task(
            proc01, &PeriodicFourCenterFockBuilder_::accumulate_global_task,
            local_tile.second, ij);
      }
      local_fock_tiles_.clear();
      compute_world.gop.fence();

      typename Policy::shape_type shape;
      // compute the shape, if sparse
      if (!decltype(shape)::is_dense()) {
        // extract local contribution to the shape of G, construct global shape
        std::vector<std::pair<std::array<size_t, 2>, double>> global_tile_norms;
        for (const auto &global_tile : global_fock_tiles_) {
          const auto ij = global_tile.first;
          const auto i = ij / ntiles1;
          const auto j = ij % ntiles1;
          const auto ij_norm = global_tile.second.norm();
          global_tile_norms.push_back(
              std::make_pair(std::array<size_t, 2>{{i, j}}, ij_norm));
        }
        shape = decltype(shape)(compute_world, global_tile_norms, trange_fock_);
      }

      array_type G(compute_world, trange_fock_, shape, dist_pmap_fock_);
      for (const auto &global_tile : global_fock_tiles_) {
        if (!G.shape().is_zero(global_tile.first))
          G.set(global_tile.first, global_tile.second);
      }
      G.fill_local(0.0, true);
      global_fock_tiles_.clear();

      auto t1 = mpqc::fenced_now(compute_world);
      auto dur = mpqc::duration_in_s(t0, t1);
      ExEnv::out0() << "Total PeriodicFourCenterFock builder time: " << dur
                    << std::endl;
      return G;

    } else {
      typename Policy::shape_type shape;
      // compute the shape, if sparse
      if (!decltype(shape)::is_dense()) {
        // extract local contribution to the shape of G, construct global shape
        std::vector<std::pair<std::array<size_t, 2>, double>> local_tile_norms;
        for (const auto &local_tile_iter : local_fock_tiles_) {
          const auto ij = local_tile_iter.first;
          const auto i = ij / ntiles1;
          const auto j = ij % ntiles1;
          const auto ij_norm = local_tile_iter.second.norm();
          local_tile_norms.push_back(
              std::make_pair(std::array<size_t, 2>{{i, j}}, ij_norm));
        }
        shape = decltype(shape)(compute_world, local_tile_norms, trange_fock_);
      }

      array_type G(compute_world, trange_fock_, shape, dist_pmap_fock_);

      // copy results of local reduction tasks into G
      for (const auto &local_tile : local_fock_tiles_) {
        // if this tile was not truncated away
        if (!G.shape().is_zero(local_tile.first))
          G.set(local_tile.first, local_tile.second);
      }
      // set the remaining local tiles to 0 (this should only be needed for
      // dense policy)
      G.fill_local(0.0, true);
      local_fock_tiles_.clear();

      auto t1 = mpqc::fenced_now(compute_world);
      auto dur = mpqc::duration_in_s(t0, t1);
      ExEnv::out0() << "Total PeriodicFourCenterFock builder time: " << dur
                    << std::endl;

      return G;
    }
  }

 private:
  // set by ctor
  const bool compute_J_;
  const bool compute_K_;
  const std::string screen_;
  const double screen_threshold_;
  const double shell_pair_threshold_;
  const Vector3d dcell_;
  const Vector3i R_max_;
  const Vector3i RJ_max_;
  const Vector3i RD_max_;
  const int64_t R_size_;
  const int64_t RJ_size_;
  const int64_t RD_size_;
  std::shared_ptr<const Basis> bra_basis_;
  std::shared_ptr<const Basis> ket_basis_;

  // mutated by compute_ functions
  mutable std::shared_ptr<lcao::Screener> j_p_screener_;
  mutable std::shared_ptr<lcao::Screener> k_p_screener_;
  mutable std::shared_ptr<Basis> basisR_;
  mutable std::vector<std::shared_ptr<Basis>> basisRJ_;
  mutable std::vector<std::shared_ptr<Basis>> basisRD_;
  mutable madness::ConcurrentHashMap<std::size_t, Tile> local_fock_tiles_;
  mutable madness::ConcurrentHashMap<std::size_t, Tile> global_fock_tiles_;
  mutable TA::TiledRange trange_D_;
  mutable TA::TiledRange trange_fock_;
  mutable std::shared_ptr<TA::Pmap> repl_pmap_D_;
  mutable std::shared_ptr<TA::Pmap> dist_pmap_fock_;
  mutable double target_precision_ = 0.0;
  mutable Engine j_engines_;
  mutable Engine k_engines_;
  mutable std::atomic<size_t> J_num_ints_computed_{0};
  mutable std::atomic<size_t> K_num_ints_computed_{0};
  mutable std::map<int64_t, int64_t> translation_map_;
  mutable bool is_density_diagonal_;

  mutable shellpair_list_t sig_j_bra_shellpair_list_;
  mutable shellpair_list_t sig_j_ket_shellpair_list_;
  mutable shellpair_list_t sig_k_bra_shellpair_list_;
  mutable shellpair_list_t sig_k_ket_shellpair_list_;
  mutable std::unordered_map<size_t, size_t> basis0_shell_offset_map_;
  mutable std::unordered_map<size_t, size_t> j_basisR_shell_offset_map_;
  mutable std::unordered_map<size_t, size_t> j_basisRJ_shell_offset_map_;
  mutable std::unordered_map<size_t, size_t> j_basisRD_shell_offset_map_;
  mutable std::unordered_map<size_t, size_t> k_basisR_shell_offset_map_;
  mutable std::unordered_map<size_t, size_t> k_basisRJ_shell_offset_map_;
  mutable std::unordered_map<size_t, size_t> k_basisRD_shell_offset_map_;

  void init() {
    using ::mpqc::lcao::detail::direct_vector;
    using ::mpqc::lcao::gaussian::detail::shift_basis_origin;
    using ::mpqc::lcao::gaussian::make_engine_pool;

    // make compound basis set for bra1
    Vector3d zero_shift_base(0.0, 0.0, 0.0);
    basisR_ = shift_basis_origin(*bra_basis_, zero_shift_base, R_max_, dcell_);

    const auto basis0 = *bra_basis_;
    const auto basisR = *basisR_;

    auto oper_type = libint2::Operator::coulomb;

    for (auto RJ = 0; RJ < RJ_size_; ++RJ) {
      auto vec_RJ = direct_vector(RJ, RJ_max_, dcell_);
      // make compound basis sets for ket0 and ket1
      basisRJ_.emplace_back(shift_basis_origin(*ket_basis_, vec_RJ));
      basisRD_.emplace_back(
          shift_basis_origin(*ket_basis_, vec_RJ, RD_max_, dcell_));
    }

    auto &world = this->get_world();
    // initialize screener
    if (screen_ == "schwarz") {
      if (compute_J_) {
        // make Qmatrix for bra
        auto screen_engine = make_engine_pool(
            oper_type, utility::make_array_of_refs(basis0, basisR),
            libint2::BraKet::xx_xx);
        auto Qbra = std::make_shared<Qmatrix>(
            Qmatrix(world, screen_engine, basis0, basisR,
                    lcao::gaussian::detail::l2Norm));
        // make Qmatrix for ket
        // (ket0 ket1 | ket0 ket1) = (bra0 bra1 | bra0 bra1)
        // using translational symmetry
        if (bra_basis_ == ket_basis_ && R_max_ == RD_max_) {
          j_p_screener_ = std::make_shared<lcao::gaussian::SchwarzScreen>(
              lcao::gaussian::SchwarzScreen(Qbra, Qbra, screen_threshold_));
        } else {
          const auto ket_basis = *ket_basis_;
          const auto tmp_basis = *(
              shift_basis_origin(ket_basis, zero_shift_base, RD_max_, dcell_));
          auto tmp_eng = make_engine_pool(
              oper_type, utility::make_array_of_refs(ket_basis, tmp_basis),
              libint2::BraKet::xx_xx);
          auto Qket = std::make_shared<Qmatrix>(
              Qmatrix(world, tmp_eng, ket_basis, tmp_basis,
                      ::mpqc::lcao::gaussian::detail::l2Norm));
          j_p_screener_ = std::make_shared<lcao::gaussian::SchwarzScreen>(
              lcao::gaussian::SchwarzScreen(Qbra, Qket, screen_threshold_));
        }
      }
      if (compute_K_) {
        std::shared_ptr<Qmatrix> Qbra, Qket;
        // make Qmatrix for bra
        {
          const auto tmp_basis = *(shift_basis_origin(
              *ket_basis_, zero_shift_base, RJ_max_, dcell_));
          auto tmp_eng = make_engine_pool(
              oper_type, utility::make_array_of_refs(basis0, tmp_basis),
              libint2::BraKet::xx_xx);
          Qbra = std::make_shared<Qmatrix>(
              Qmatrix(world, tmp_eng, basis0, tmp_basis,
                      ::mpqc::lcao::gaussian::detail::l2Norm));
        }
        // make Qmatrix for ket
        {
          auto max_uc = R_max_ + RJ_max_ + RD_max_;
          translation_map_ = compute_translation_map(max_uc);

          const auto tmp_basis = *(
              shift_basis_origin(*ket_basis_, zero_shift_base, max_uc, dcell_));
          auto tmp_eng = make_engine_pool(
              oper_type, utility::make_array_of_refs(basis0, tmp_basis),
              libint2::BraKet::xx_xx);
          Qket = std::make_shared<Qmatrix>(
              Qmatrix(world, tmp_eng, basis0, tmp_basis,
                      ::mpqc::lcao::gaussian::detail::l2Norm));
        }
        k_p_screener_ = std::make_shared<lcao::gaussian::SchwarzScreen>(
            lcao::gaussian::SchwarzScreen(Qbra, Qket, screen_threshold_));
      }

    } else {
      throw InputError("Wrong screening method", __FILE__, __LINE__, "screen");
    }

    // compute significant shell pair list
    {
      basis0_shell_offset_map_ = compute_shell_offset(basis0);
      if (compute_J_) {
        sig_j_bra_shellpair_list_ = parallel_compute_shellpair_list(
            basis0, basisR, shell_pair_threshold_);
        j_basisR_shell_offset_map_ = compute_shell_offset(basisR);

        if (bra_basis_ == ket_basis_ && R_max_ == RD_max_) {
          sig_j_ket_shellpair_list_ = sig_j_bra_shellpair_list_;
          j_basisRJ_shell_offset_map_ = basis0_shell_offset_map_;
          j_basisRD_shell_offset_map_ = j_basisR_shell_offset_map_;
        } else {
          const auto ket_basis = *ket_basis_;
          const auto tmp_basis = *(
              shift_basis_origin(ket_basis, zero_shift_base, RD_max_, dcell_));
          sig_j_ket_shellpair_list_ = parallel_compute_shellpair_list(
              ket_basis, tmp_basis, shell_pair_threshold_);
          j_basisRJ_shell_offset_map_ = compute_shell_offset(ket_basis);
          j_basisRD_shell_offset_map_ = compute_shell_offset(tmp_basis);
        }
      }
      if (compute_K_) {
        const auto basisRJ = *(
            shift_basis_origin(*ket_basis_, zero_shift_base, RJ_max_, dcell_));
        sig_k_bra_shellpair_list_ = parallel_compute_shellpair_list(
            basis0, basisRJ, shell_pair_threshold_);
        k_basisRJ_shell_offset_map_ = compute_shell_offset(basisRJ);

        auto max_uc = R_max_ + RJ_max_ + RD_max_;
        translation_map_ = compute_translation_map(max_uc);
        const auto tmp_basis =
            *(shift_basis_origin(*ket_basis_, zero_shift_base, max_uc, dcell_));
        sig_k_ket_shellpair_list_ = parallel_compute_shellpair_list(
            basis0, tmp_basis, shell_pair_threshold_);
        k_basisR_shell_offset_map_ = compute_shell_offset(basis0);
        k_basisRD_shell_offset_map_ = compute_shell_offset(tmp_basis);
      }
    }

    // get TiledRange of Fock
    trange_fock_ = ::mpqc::lcao::gaussian::detail::create_trange(
        BasisVector{{basis0, basisR}});
    const auto tvolume = trange_fock_.tiles_range().volume();
    dist_pmap_fock_ = Policy::default_pmap(world, tvolume);
  }

  void accumulate_global_task(Tile arg_tile, long tile01) {
    // if reducer does not exist, create entry and store F, else accumulate F to
    // the existing contents
    typename decltype(global_fock_tiles_)::accessor acc;
    // try inserting, otherwise, accumulate
    if (!global_fock_tiles_.insert(
            acc, std::make_pair(tile01, arg_tile))) {  // CRITICAL SECTION
      // NB can't do acc->second += fock_matrix_tile to avoid spawning TBB
      // tasks from critical section
      const auto size = arg_tile.range().volume();
      TA::math::inplace_vector_op_serial(
          [](TA::detail::numeric_t<Tile> &l,
             const TA::detail::numeric_t<Tile> r) { l += r; },
          size, acc->second.data(), arg_tile.data());
    }
    acc.release();  // END OF CRITICAL SECTION
  }

  void accumulate_local_task(Tile fock_matrix_tile, long tile0, long tile1) {
    const auto ntiles1 = trange_fock_.dim(1).tile_extent();
    const auto tile01 = tile0 * ntiles1 + tile1;
    // if reducer does not exist, create entry and store F, else accumulate F to
    // the existing contents
    typename decltype(local_fock_tiles_)::accessor acc;
    // try inserting, otherwise, accumulate
    if (!local_fock_tiles_.insert(
            acc,
            std::make_pair(tile01, fock_matrix_tile))) {  // CRITICAL SECTION
      // NB can't do acc->second += fock_matrix_tile to avoid spawning TBB
      // tasks from critical section
      const auto size = fock_matrix_tile.range().volume();
      TA::math::inplace_vector_op_serial(
          [](TA::detail::numeric_t<Tile> &l,
             const TA::detail::numeric_t<Tile> r) { l += r; },
          size, acc->second.data(), fock_matrix_tile.data());
    }
    acc.release();  // END OF CRITICAL SECTION
  }

  void compute_task_abcd(Tile D_RJRD, Tile norm_D_RJRD, int64_t R, int64_t RJ,
                         int64_t RD, std::array<size_t, 4> tile_idx) {
    const auto tile0 = tile_idx[0];
    const auto tileR = tile_idx[1];
    const auto tileRJ = tile_idx[2];
    const auto tileRD = tile_idx[3];

    // get reference to basis sets
    const auto &basis0 = bra_basis_;
    const auto &basisR = basisR_;
    const auto &basisRJ = basisRJ_[RJ];
    const auto &basisRD = basisRD_[RJ];

    // shell clusters for this tile
    const auto &cluster0 = basis0->cluster_shells()[tile0];
    const auto &clusterR = basisR->cluster_shells()[tileR];
    const auto &clusterRJ = basisRJ->cluster_shells()[tileRJ];
    const auto &clusterRD = basisRD->cluster_shells()[tileRD];

    // number of shells in each cluster
    const auto nshells0 = cluster0.size();
    const auto nshellsR = clusterR.size();
    const auto nshellsRJ = clusterRJ.size();
    const auto nshellsRD = clusterRD.size();

    // 1-d tile ranges
    const auto &rng0 = trange_fock_.dim(0).tile(tile0);
    const auto &rngR = trange_fock_.dim(1).tile(tileR);
    const auto &rngRJ = trange_D_.dim(0).tile(tileRJ);
    const auto &rngRD = trange_D_.dim(1).tile(tileRD);
    const auto rngR_size = rngR.second - rngR.first;
    const auto rngRD_size = rngRD.second - rngRD.first;

    // 2-d tile ranges describing the Fock contribution blocks produced by this
    auto rng0R = TA::Range({rng0, rngR});

    // initialize contribution to the Fock matrices
    auto F0R = Tile(std::move(rng0R), 0.0);

    // grab ptrs to tile data to make addressing more efficient
    auto *F0R_ptr = F0R.data();
    const auto *D_RJRD_ptr = D_RJRD.data();
    const auto *norm_D_RJRD_ptr = norm_D_RJRD.data();
    assert(D_RJRD_ptr != nullptr);
    assert(norm_D_RJRD_ptr != nullptr);

    const auto nbf_bra_per_uc = bra_basis_->nfunctions();
    const auto nbf_ket_per_uc = ket_basis_->nfunctions();
    const auto ncluster_bra_per_uc = bra_basis_->cluster_shells().size();
    const auto ncluster_ket_per_uc = ket_basis_->cluster_shells().size();

    const auto R_stride = R * RJ_size_ * RD_size_;
    const auto RJ_stride = RJ * RD_size_;
    const auto old_uc_ord = R_stride + RJ_stride + RD;
    const auto new_uc_ord = translation_map_[old_uc_ord];

    // compute Coulomb and/or Exchange contributions to all Fock matrices
    {
      // compute Coulomb contributions to all Fock matrices
      if (compute_J_) {
        // index of first shell in this cluster
        const auto sh0_offset = basis0_shell_offset_map_[tile0];
        const auto shR_offset = j_basisR_shell_offset_map_[tileR];
        const auto shRJ_offset = j_basisRJ_shell_offset_map_[tileRJ];
        const auto shRD_offset = j_basisRD_shell_offset_map_[tileRD];

        // index of last shell in this cluster
        const auto sh0_max = sh0_offset + nshells0;
        const auto shR_max = shR_offset + nshellsR;
        const auto shRD_max = shRD_offset + nshellsRD;
        const auto shRJ_max = shRJ_offset + nshellsRJ;

        // determine if this task contains significant shell-pairs
        auto is_j_bra_significant = false;
        {
          auto sh0_in_basis = sh0_offset;
          for (; sh0_in_basis != sh0_max; ++sh0_in_basis) {
            for (const auto &shR_in_basis :
                 sig_j_bra_shellpair_list_[sh0_in_basis]) {
              if (shR_in_basis >= shR_offset && shR_in_basis < shR_max) {
                is_j_bra_significant = true;
                break;
              }
            }
            if (is_j_bra_significant) break;
          }
        }

        auto is_j_ket_significant = false;
        {
          auto shRJ_in_basis = shRJ_offset;
          for (; shRJ_in_basis != shRJ_max; ++shRJ_in_basis) {
            for (const auto &shRD_in_basis :
                 sig_j_ket_shellpair_list_[shRJ_in_basis]) {
              if (shRD_in_basis >= shRD_offset && shRD_in_basis < shRD_max) {
                is_j_ket_significant = true;
                break;
              }
            }
            if (is_j_ket_significant) break;
          }
        }

        if (is_j_bra_significant && is_j_ket_significant) {
          auto engine = j_engines_->local();
          const auto engine_precision = target_precision_;
          engine.set_precision(engine_precision);
          const auto &computed_shell_sets = engine.results();

          auto &screen = *(j_p_screener_);

          // compute offset list of cluster1 and cluster3
          auto offset_list_bra1 =
              compute_func_offset_list(clusterR, rngR.first);
          auto offset_list_ket1 =
              compute_func_offset_list(clusterRD, rngRD.first);

          // this is the index of the first basis functions for each shell *in
          // this shell cluster*
          auto cf0_offset = 0;
          // this is the index of the first basis functions for each shell *in
          // the
          // basis set*
          auto bf0_offset = rng0.first;
          size_t cf1_offset, bf1_offset, cf3_offset, bf3_offset;

          // loop over all shell sets
          for (auto sh0 = 0; sh0 != nshells0; ++sh0) {
            const auto &shell0 = cluster0[sh0];
            const auto nf0 = shell0.size();

            const auto sh0_in_basis = sh0 + sh0_offset;
            for (const auto &shR_in_basis :
                 sig_j_bra_shellpair_list_[sh0_in_basis]) {
              if (shR_in_basis < shR_offset || shR_in_basis >= shR_max)
                continue;

              const auto sh1 = shR_in_basis - shR_offset;
              std::tie(cf1_offset, bf1_offset) = offset_list_bra1[sh1];

              const auto &shell1 = clusterR[sh1];
              const auto nf1 = shell1.size();

              auto cf2_offset = 0;
              auto bf2_offset = rngRJ.first;

              for (auto sh2 = 0; sh2 != nshellsRJ; ++sh2) {
                const auto &shell2 = clusterRJ[sh2];
                const auto nf2 = shell2.size();

                const auto shRJ_in_basis = sh2 + shRJ_offset;
                for (const auto &shRD_in_basis :
                     sig_j_ket_shellpair_list_[shRJ_in_basis]) {
                  if (shRD_in_basis < shRD_offset || shRD_in_basis >= shRD_max)
                    continue;

                  const auto sh3 = shRD_in_basis - shRD_offset;
                  if (is_density_diagonal_ && sh3 != sh2) continue;

                  std::tie(cf3_offset, bf3_offset) = offset_list_ket1[sh3];

                  const auto &shell3 = clusterRD[sh3];
                  const auto nf3 = shell3.size();

                  const auto sh23 =
                      sh2 * nshellsRD + sh3;  // index of {sh2, sh3} in norm_D23
                  const auto Dnorm23 = norm_D_RJRD_ptr[sh23];

                  if (screen.skip(bf0_offset, bf1_offset, bf2_offset,
                                  bf3_offset, Dnorm23))
                    continue;

                  J_num_ints_computed_ += nf0 * nf1 * nf2 * nf3;

                  // compute shell set
                  engine.template compute2<libint2::Operator::coulomb,
                                           libint2::BraKet::xx_xx, 0>(
                      shell0, shell1, shell2, shell3);
                  const auto *eri_0123 = computed_shell_sets[0];

                  if (eri_0123 !=
                      nullptr) {  // if the shell set is not screened out

                    for (auto f0 = 0, f0123 = 0; f0 != nf0; ++f0) {
                      const auto cf0 = f0 + cf0_offset;  // basis function index
                                                         // in the tile (i.e.
                                                         // shell cluster)
                      for (auto f1 = 0; f1 != nf1; ++f1) {
                        const auto cf1 = f1 + cf1_offset;
                        const auto cf01 =
                            cf0 * rngR_size + cf1;  // index of {cf0,cf1} in F0R
                        for (auto f2 = 0; f2 != nf2; ++f2) {
                          const auto cf2 = f2 + cf2_offset;
                          for (auto f3 = 0; f3 != nf3; ++f3, ++f0123) {
                            const auto cf3 = f3 + cf3_offset;
                            const auto cf23 =
                                cf2 * rngRD_size +
                                cf3;  // index of {cf2,cf3} in D_RJRD

                            const auto value = eri_0123[f0123];

                            F0R_ptr[cf01] += 2.0 * D_RJRD_ptr[cf23] * value;
                          }
                        }
                      }
                    }
                  }
                }

                cf2_offset += nf2;
                bf2_offset += nf2;
              }
            }

            cf0_offset += nf0;
            bf0_offset += nf0;
          }
        }
      }

      if (compute_K_) {
        // index of first shell in this cluster
        const auto sh0_offset = basis0_shell_offset_map_[tile0];
        const auto shRJ_offset =
            k_basisRJ_shell_offset_map_[tileRJ +
                                        RJ * basisRJ->cluster_shells().size()];
        const auto shR_offset =
            k_basisR_shell_offset_map_[tileR % ncluster_bra_per_uc];
        const auto shRD_offset =
            k_basisRD_shell_offset_map_[tileRD % ncluster_ket_per_uc +
                                        new_uc_ord * ncluster_ket_per_uc];

        // index of last shell in this cluster
        const auto sh0_max = sh0_offset + nshells0;
        const auto shR_max = shR_offset + nshellsR;
        const auto shRJ_max = shRJ_offset + nshellsRJ;
        const auto shRD_max = shRD_offset + nshellsRD;

        // determine if this task contains significant shell-pairs
        auto is_k_bra_significant = false;
        {
          auto sh0_in_basis = sh0_offset;
          for (; sh0_in_basis != sh0_max; ++sh0_in_basis) {
            for (const auto &shRJ_in_basis :
                 sig_k_bra_shellpair_list_[sh0_in_basis]) {
              if (shRJ_in_basis >= shRJ_offset && shRJ_in_basis < shRJ_max) {
                is_k_bra_significant = true;
                break;
              }
            }
            if (is_k_bra_significant) break;
          }
        }

        auto is_k_ket_significant = false;
        {
          auto shR_in_basis = shR_offset;
          for (; shR_in_basis != shR_max; ++shR_in_basis) {
            for (const auto &shRD_in_basis :
                 sig_k_ket_shellpair_list_[shR_in_basis]) {
              if (shRD_in_basis >= shRD_offset && shRD_in_basis < shRD_max) {
                is_k_ket_significant = true;
                break;
              }
            }
            if (is_k_ket_significant) break;
          }
        }

        if (is_k_bra_significant && is_k_ket_significant) {
          auto engine = k_engines_->local();
          const auto engine_precision = target_precision_;
          engine.set_precision(engine_precision);
          const auto &computed_shell_sets = engine.results();

          auto &screen = *(k_p_screener_);

          // compute offset list of cluster1 and cluster3
          auto offset_list_bra1 =
              compute_func_offset_list(clusterRJ, rngRJ.first);
          auto offset_list_ket1 =
              compute_func_offset_list(clusterRD, rngRD.first);

          // this is the index of the first basis functions for each shell *in
          // this shell cluster*
          auto cf0_offset = 0;
          // this is the index of the first basis functions for each shell *in
          // the basis set*
          auto bf0_offset = rng0.first;
          size_t cf1_offset, bf1_offset, cf3_offset, bf3_offset;

          // pre-compute index of the first basis functions in screener bases
          const auto screen_bf1_offset = nbf_ket_per_uc * RJ;
          const auto screen_bf3_offset = new_uc_ord * nbf_ket_per_uc;

          // loop over all shell sets
          for (auto sh0 = 0; sh0 != nshells0; ++sh0) {
            const auto &shell0 = cluster0[sh0];
            const auto nf0 = shell0.size();

            const auto sh0_in_basis = sh0 + sh0_offset;
            for (const auto &shRJ_in_basis :
                 sig_k_bra_shellpair_list_[sh0_in_basis]) {
              if (shRJ_in_basis < shRJ_offset || shRJ_in_basis >= shRJ_max)
                continue;

              const auto sh1 = shRJ_in_basis - shRJ_offset;

              std::tie(cf1_offset, bf1_offset) = offset_list_bra1[sh1];

              const auto &shell1 = clusterRJ[sh1];
              const auto nf1 = shell1.size();

              const auto bf1_in_screener = bf1_offset + screen_bf1_offset;

              auto cf2_offset = 0;
              auto bf2_offset = rngR.first;

              for (auto sh2 = 0; sh2 != nshellsR; ++sh2) {
                const auto &shell2 = clusterR[sh2];
                const auto nf2 = shell2.size();

                const auto bf2_in_screener = bf2_offset % nbf_bra_per_uc;
                const auto shR_in_basis = sh2 + shR_offset;
                for (const auto &shRD_in_basis :
                     sig_k_ket_shellpair_list_[shR_in_basis]) {
                  if (shRD_in_basis < shRD_offset || shRD_in_basis >= shRD_max)
                    continue;

                  const auto sh3 = shRD_in_basis - shRD_offset;

                  if (is_density_diagonal_ && sh3 != sh1) continue;

                  std::tie(cf3_offset, bf3_offset) = offset_list_ket1[sh3];

                  const auto &shell3 = clusterRD[sh3];
                  const auto nf3 = shell3.size();

                  const auto bf3_in_screener =
                      bf3_offset % nbf_ket_per_uc + screen_bf3_offset;

                  const auto sh13 = sh1 * nshellsRD + sh3;
                  const auto Dnorm13 = norm_D_RJRD_ptr[sh13];

                  if (screen.skip(bf0_offset, bf1_in_screener, bf2_in_screener,
                                  bf3_in_screener, Dnorm13))
                    continue;

                  K_num_ints_computed_ += nf0 * nf1 * nf2 * nf3;

                  // compute shell set
                  engine.template compute2<libint2::Operator::coulomb,
                                           libint2::BraKet::xx_xx, 0>(
                      shell0, shell1, shell2, shell3);
                  const auto *eri_0123 = computed_shell_sets[0];

                  if (eri_0123 !=
                      nullptr) {  // if the shell set if not screened out

                    for (auto f0 = 0, f0123 = 0; f0 != nf0; ++f0) {
                      const auto cf0 = f0 + cf0_offset;  // basis function index
                                                         // in the tile (i.e.
                                                         // shell cluster)
                      for (auto f1 = 0; f1 != nf1; ++f1) {
                        const auto cf1 = f1 + cf1_offset;
                        for (auto f2 = 0; f2 != nf2; ++f2) {
                          const auto cf2 = f2 + cf2_offset;
                          const auto cf02 = cf0 * rngR_size +
                                            cf2;  // index of {cf0,cf2} in F0R
                          for (auto f3 = 0; f3 != nf3; ++f3, ++f0123) {
                            const auto cf3 = f3 + cf3_offset;
                            const auto cf13 =
                                cf1 * rngRD_size +
                                cf3;  // index of {cf1, cf3} in D_RJRD

                            const auto value = eri_0123[f0123];

                            F0R_ptr[cf02] -= D_RJRD_ptr[cf13] * value;
                          }
                        }
                      }
                    }
                  }
                }

                cf2_offset += nf2;
                bf2_offset += nf2;
              }
            }

            cf0_offset += nf0;
            bf0_offset += nf0;
          }
        }
      }
    }

    // accumulate the local contributions
    {
      PeriodicFourCenterFockBuilder_::accumulate_local_task(F0R, tile0, tileR);
    }
  }

  /*!
   * \brief This computes shell-block norm of density matrix \c D
   * \param bs Basis
   * \param D density matrix
   * \return
   */
  array_type compute_shellblock_norm(const Basis &bs0, const Basis &bs1,
                                     const array_type &D) const {
    auto &world = this->get_world();
    // make trange1
    auto make_shblk_trange1 = [](const Basis &bs) {
      const auto &shells_Vec = bs.cluster_shells();
      auto blocking = std::vector<int64_t>{0};
      for (const auto &shells : shells_Vec) {
        const auto nshell = shells.size();
        auto next = blocking.back() + nshell;
        blocking.emplace_back(next);
      }
      return TA::TiledRange1(blocking.begin(), blocking.end());
    };

    const auto tr0 = make_shblk_trange1(bs0);
    const auto tr1 = make_shblk_trange1(bs1);

    auto eig_D = ::mpqc::array_ops::array_to_eigen(D);
    // compute shell block norms
    const auto shells0 = bs0.flattened_shells();
    const auto shells1 = bs1.flattened_shells();
    const auto nshell0 = shells0.size();
    const auto nshell1 = shells1.size();
    RowMatrixXd norm_D(nshell0, nshell1);
    for (auto sh0 = 0, sh0_first = 0; sh0 != nshell0; ++sh0) {
      const auto sh0_size = shells0[sh0].size();
      for (auto sh1 = 0, sh1_first = 0; sh1 != nshell1; ++sh1) {
        const auto sh1_size = shells1[sh1].size();

        norm_D(sh0, sh1) = eig_D.block(sh0_first, sh1_first, sh0_size, sh1_size)
                               .template lpNorm<Eigen::Infinity>();

        sh1_first += sh1_size;
      }

      sh0_first += sh0_size;
    }

    return array_ops::eigen_to_array<Tile, Policy>(world, norm_D, tr0, tr1);
  }

  /*!
   * \brief This computes shell block norm of a density tile
   * \param arg_tile_ptr a const pointer to data of the tile
   * \param cluster0 a shell cluster (a.k.a. std::vector<Shell>)
   * \param cluster1 a shell cluster (a.k.a. std::vector<Shell>)
   * \param nf0 number of functions in \c cluster0
   * \param nf1 number of functions in \c cluster1
   * \return a tile filled with shell block norms
   */
  Tile compute_shellblock_norm_of_tile(const_data_ptr arg_tile_ptr,
                                       const ShellVec &cluster0,
                                       const ShellVec &cluster1,
                                       const size_t nf0,
                                       const size_t nf1) const {
    // # of shells in this shell cluster
    const auto nsh0 = cluster0.size();
    const auto nsh1 = cluster1.size();

    auto range01 = TA::Range({nsh0, nsh1});
    auto shblk_norm = Tile(std::move(range01), 0.0);
    auto *shblk_norm_ptr = shblk_norm.data();

    const auto arg_tile_eig =
        Eigen::Map<const RowMatrixXd>(arg_tile_ptr, nf0, nf1);

    for (auto s0 = 0, s0_first = 0, s01 = 0; s0 != nsh0; ++s0) {
      const auto s0_size = cluster0[s0].size();
      for (auto s1 = 0, s1_first = 0; s1 != nsh1; ++s1, ++s01) {
        const auto s1_size = cluster1[s1].size();
        shblk_norm_ptr[s01] =
            arg_tile_eig.block(s0_first, s1_first, s0_size, s1_size)
                .template lpNorm<Eigen::Infinity>();
        s1_first += s1_size;
      }
      s0_first += s0_size;
    }

    return shblk_norm;
  }

  /*!
   * \brief This computes non-negligible shell pair list; ; shells \c i and \c j
   * form a non-negligible pair if they share a center or the Frobenius norm of
   * their overlap is greater than threshold
   * \param basis1 a basis
   * \param basis2 a basis
   * \param threshold
   *
   * \return a list of pairs with
   * key: shell index
   * mapped value: a vector of shell indices
   */
  shellpair_list_t parallel_compute_shellpair_list(
      const Basis &basis1, const Basis &basis2,
      double threshold = 1e-12) const {
    using ::mpqc::lcao::gaussian::make_engine_pool;
    using ::mpqc::lcao::gaussian::detail::to_libint2_operator;
    // initialize engine
    auto engine_pool = make_engine_pool(
        libint2::Operator::overlap, utility::make_array_of_refs(basis1, basis2),
        libint2::BraKet::x_x);

    auto &world = this->get_world();
    std::mutex mx;
    shellpair_list_t result;

    const auto &shv1 = basis1.flattened_shells();
    const auto &shv2 = basis2.flattened_shells();
    const auto nsh1 = shv1.size();
    const auto nsh2 = shv2.size();

    auto compute = [&](int64_t input_s1) {

      auto n1 = shv1[input_s1].size();
      const auto engine_precision = target_precision_;
      auto engine = engine_pool->local();
      engine.set_precision(engine_precision);
      const auto &buf = engine.results();

      for (auto s2 = 0l; s2 != nsh2; ++s2) {
        auto on_same_center = (shv1[input_s1].O == shv2[s2].O);
        bool significant = on_same_center;
        if (!on_same_center) {
          auto n2 = shv2[s2].size();
          engine.compute1(shv1[input_s1], shv2[s2]);
          Eigen::Map<const RowMatrixXd> buf_mat(buf[0], n1, n2);
          auto norm = buf_mat.norm();
          significant = (norm >= threshold);
        }

        if (significant) {
          mx.lock();
          result[input_s1].emplace_back(s2);
          mx.unlock();
        }
      }
    };

    for (auto s1 = 0l; s1 != nsh1; ++s1) {
      result.insert(std::make_pair(s1, std::vector<size_t>()));
      world.taskq.add(compute, s1);
    }
    world.gop.fence();

    engine_pool.reset();

    // resort shell list in increasing order
    for (auto s1 = 0l; s1 != nsh1; ++s1) {
      auto &list = result[s1];
      std::sort(list.begin(), list.end());
    }

    return result;
  }

  /*!
   * \brief This computes non-negligible shell pair list; ; shells \c i and \c j
   * form a non-negligible pair if they share a center or the Frobenius norm of
   * their overlap is greater than threshold
   * \param shv1 a cluster (a.k.a. std::vector<Shell>)
   * \param shv2 a cluster (a.k.a. std::vector<Shell>)
   * \param threshold
   *
   * \return a list of pairs with
   * key: shell index
   * mapped value: a vector of shell indices
   */
  shellpair_list_t compute_shellpair_list(
      const ShellVec &shv1,
      const ShellVec &_shv2 = std::vector<Shell>({Shell()}),
      double threshold = 1e-12) const {
    const ShellVec &shv2 =
        ((_shv2.size() == 1 && _shv2[0] == Shell()) ? shv1 : _shv2);
    const auto nsh1 = shv1.size();
    const auto nsh2 = shv2.size();
    const auto shv1_equiv_shv2 = (&shv1 == &shv2);

    // determine max # of primitives in a shell cluster
    auto max_nprim = [](const ShellVec &shv) {
      size_t n = 0;
      for (auto shell : shv) n = std::max(shell.nprim(), n);
      return n;
    };
    const auto max_nprim_1 = max_nprim(shv1);
    const auto max_nprim_2 = max_nprim(shv2);

    // determine max angular momentum of a shell cluster
    auto max_l = [](const ShellVec &shv) {
      int l = 0;
      for (auto shell : shv)
        for (auto c : shell.contr) l = std::max(c.l, l);
      return l;
    };
    const auto max_l_1 = max_l(shv1);
    const auto max_l_2 = max_l(shv2);

    // initialize libint2 engine
    auto engine = libint2::Engine(libint2::Operator::overlap,
                                  std::max(max_nprim_1, max_nprim_2),
                                  std::max(max_l_1, max_l_2), 0);
    const auto &buf = engine.results();
    shellpair_list_t result;

    // compute non-negligible shell-pair list
    for (auto s1 = 0l, s12 = 0l; s1 != nsh1; ++s1) {
      result.insert(std::make_pair(s1, std::vector<size_t>()));
      auto n1 = shv1[s1].size();

      auto s2_max = shv1_equiv_shv2 ? s1 : nsh2 - 1;
      for (auto s2 = 0l; s2 <= s2_max; ++s2, ++s12) {
        auto on_same_center = (shv1[s1].O == shv2[s2].O);
        bool significant = on_same_center;
        if (!on_same_center) {
          auto n2 = shv2[s2].size();
          engine.compute(shv1[s1], shv2[s2]);
          Eigen::Map<const RowMatrixXd> buf_mat(buf[0], n1, n2);
          auto norm = buf_mat.norm();
          significant = (norm >= threshold);
        }

        if (significant) result[s1].emplace_back(s2);
      }
    }

    // resort shell list in increasing order
    for (auto s1 = 0l; s1 != nsh1; ++s1) {
      auto &list = result[s1];
      std::sort(list.begin(), list.end());
    }

    return result;
  }

  /*!
   * \brief This computes basis function offsets for every shell in a cluster
   * \param cluster a cluster (a.k.a. std::vector<Shell>)
   * \param bf_first basis function index of the first function in this \c
   * cluster
   *
   * \return a list of <key, mapped value> pairs with
   * key: shell index
   * mapped value: {cluster function offset, basis function offset} tuple
   */
  func_offset_list compute_func_offset_list(const ShellVec &cluster,
                                            const size_t bf_first) const {
    func_offset_list result;

    auto cf_offset = 0;
    auto bf_offset = bf_first;

    const auto nshell = cluster.size();
    for (auto s = 0; s != nshell; ++s) {
      const auto &shell = cluster[s];
      const auto nf = shell.size();
      result.insert(std::make_pair(s, std::make_tuple(cf_offset, bf_offset)));
      bf_offset += nf;
      cf_offset += nf;
    }

    return result;
  }

  /*!
   * \brief This computes unit cell ordinal index after translation
   * in Schwarz screening (ρ_R σ_Rj+Rd | ρ_R σ_Rj+Rd).
   * Translation is from ρ_R σ_Rj+Rd to ρ_0 σ_Rj+Rd-R.
   * \param max_uc should be Rj_max + Rd_max + R_max
   * \return a map of original unit cell idx to translated unit cell idx
   */
  std::map<int64_t, int64_t> compute_translation_map(Vector3i max_uc) const {
    using ::mpqc::lcao::detail::direct_3D_idx;
    using ::mpqc::lcao::detail::direct_ord_idx;

    std::map<int64_t, int64_t> result;
    for (auto R = 0; R != R_size_; ++R) {
      auto R_stride = R * RJ_size_ * RD_size_;
      auto R_3D = direct_3D_idx(R, R_max_);
      for (auto RJ = 0; RJ != RJ_size_; ++RJ) {
        auto RJ_stride = RJ * RD_size_;
        auto RJ_3D = direct_3D_idx(RJ, RJ_max_);
        for (auto RD = 0; RD != RD_size_; ++RD) {
          auto key = R_stride + RJ_stride + RD;
          auto RD_3D = direct_3D_idx(RD, RD_max_);

          auto translation_3D = RJ_3D + RD_3D - R_3D;
          auto translation_ord = direct_ord_idx(
              translation_3D(0), translation_3D(1), translation_3D(2), max_uc);
          result.insert(std::make_pair(key, translation_ord));
        }
      }
    }

    return result;
  }

  /*!
   * \brief This computes shell offsets for every cluster in a basis
   * \param basis
   * \return a list of <key, mapped value> pairs with
   * key: cluster index
   * mapped value: index of first shell in a cluster
   */
  std::unordered_map<size_t, size_t> compute_shell_offset(
      const Basis &basis) const {
    std::unordered_map<size_t, size_t> result;

    auto shell_offset = 0;
    const auto &cluster_shells = basis.cluster_shells();
    const auto nclusters = cluster_shells.size();
    for (auto c = 0; c != nclusters; ++c) {
      const auto nshells = cluster_shells[c].size();
      result.insert(std::make_pair(c, shell_offset));
      shell_offset += nshells;
    }

    return result;
  }
};

}  // namespace scf
}  // namespace mpqc

#endif  // MPQC4_SRC_MPQC_CHEMISTRY_QC_SCF_PBC_PERIODIC_FOUR_CENTER_FOCK_BUILDER_H_