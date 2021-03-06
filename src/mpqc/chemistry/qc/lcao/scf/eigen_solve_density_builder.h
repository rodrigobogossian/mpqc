
#ifndef MPQC4_SRC_MPQC_CHEMISTRY_QC_SCF_EIGEN_SOLVE_DENSITY_BUILDER_H_
#define MPQC4_SRC_MPQC_CHEMISTRY_QC_SCF_EIGEN_SOLVE_DENSITY_BUILDER_H_

#include "mpqc/chemistry/qc/lcao/scf/density_builder.h"
#include "mpqc/chemistry/qc/lcao/scf/orbital_localization.h"
#include <array>
#include <vector>

namespace mpqc {
namespace lcao {
namespace scf {

template<typename Tile, typename Policy>
class ESolveDensityBuilder : public DensityBuilder<Tile, Policy> {
 public:
  using array_type = typename DensityBuilder<Tile, Policy>::array_type;
 private:
  array_type S_;
  array_type M_inv_;
  std::vector<array_type> r_xyz_ints_;

  // these are provided if localize_ == false, may become optional in the future
  array_type C_;  //!< canonical orbitals in AO basis
  Eigen::VectorXd eps_;  //!< canonical orbital energies

  double TcutC_;
  std::shared_ptr<OrbitalLocalizer < Tile, Policy>> localizer_;
  bool localize_core_;
  int64_t n_coeff_clusters_;
  bool clustered_coeffs_;
  std::string metric_decomp_type_;
  int64_t nocc_;  //!< # of occupied orbitals
  int64_t ncore_;  //!< # of core orbitals (will be skipped in localization)

  double condition_num_thresh_ = 1e-10;

  double inverse_time_;
  std::vector<double> esolve_times_;
  std::vector<double> localization_times_;
  std::vector<double> clustering_times_;

  std::vector<std::array<double, 3>> coeff_storages_;
  std::vector<std::array<double, 3>> density_storages_;

 public:
  ESolveDensityBuilder() = default;
  ESolveDensityBuilder(ESolveDensityBuilder const &) = default;
  ESolveDensityBuilder(ESolveDensityBuilder &&) = default;
  ESolveDensityBuilder &operator=(ESolveDensityBuilder const &) = default;
  ESolveDensityBuilder &operator=(ESolveDensityBuilder &&) = default;
  ~ESolveDensityBuilder() noexcept = default;

  ESolveDensityBuilder(
      array_type const &S, std::vector<array_type> r_xyz, int64_t nocc,
      int64_t ncore, int64_t nclusters, double TcutC = 0.0,
      std::string const &metric_decomp_type = "cholesky_inverse",
      double s_tolerance = 1.0e8,
      std::shared_ptr<OrbitalLocalizer < Tile, Policy>>
  localizer = nullptr,
  bool localize_core = false,
  bool clustered_coeffs = false
  );

  std::pair<array_type, array_type> operator()(array_type const &F) override;

  inline void print_iter(std::string const &) override {}

  inline double condition_num_threshold() const {
    return condition_num_thresh_;
  }
  inline void condition_num_threshold(double thresh) {
    condition_num_thresh_ = thresh;
  }

  inline double TcutC() const { return TcutC_; }

  bool localize() const { return localizer_ != nullptr; }
  const Eigen::VectorXd &orbital_energies() const { return eps_; }
  array_type C() const { return C_; }
};

}  // namespace scf
}  // namespace lcao
}  // namespace mpqc

#include "eigen_solve_density_builder_impl.h"

#endif  // MPQC4_SRC_MPQC_CHEMISTRY_QC_SCF_EIGEN_SOLVE_DENSITY_BUILDER_H_
