//
// Created by Chong Peng on 10/14/15.
//

#ifndef MPQC4_SRC_MPQC_CHEMISTRY_QC_LCAO_FACTORY_AO_FACTORY_H_
#define MPQC4_SRC_MPQC_CHEMISTRY_QC_LCAO_FACTORY_AO_FACTORY_H_

#include "mpqc/chemistry/qc/lcao/expression/permutation.h"
#include "mpqc/chemistry/qc/lcao/factory/factory.h"
#include "mpqc/chemistry/qc/lcao/factory/factory_utility.h"
#include "mpqc/chemistry/qc/lcao/factory/set_oper.h"
#include "mpqc/chemistry/qc/lcao/integrals/density_fitting/cadf_coeffs.h"
#include "mpqc/chemistry/qc/lcao/integrals/f12_utility.h"
#include "mpqc/chemistry/qc/lcao/integrals/integrals.h"
#include "mpqc/math/external/eigen/eigen.h"
#include "mpqc/math/linalg/sqrt_inv.h"
#include "mpqc/util/external/madworld/parallel_print.h"
#include "mpqc/util/core/exenv.h"
#include "mpqc/util/misc/time.h"

#include <madness/world/worldmem.h>

namespace mpqc {
namespace lcao {
namespace gaussian {

template <typename Tile, typename Policy>
class AOFactory;

template <typename Tile, typename Policy>
using AOFactoryBase = Factory<
    TA::DistArray<Tile, Policy>,
    DirectArray<Tile, Policy, DirectIntegralBuilder<Tile, libint2::Engine>>>;

template <typename Tile, typename Policy>
std::shared_ptr<AOFactory<Tile, Policy>> construct_ao_factory(
    const KeyVal& kv) {
  std::shared_ptr<AOFactory<Tile, Policy>> ao_factory;
  if (kv.exists_class("wfn_world:ao_factory")) {
    ao_factory = kv.class_ptr<AOFactory<Tile, Policy>>("wfn_world:ao_factory");
  } else {
    ao_factory = std::make_shared<AOFactory<Tile, Policy>>(kv);
    std::shared_ptr<DescribedClass> ao_factory_base = ao_factory;
    KeyVal& kv_nonconst = const_cast<KeyVal&>(kv);
    kv_nonconst.keyval("wfn_world").assign("ao_factory", ao_factory_base);
  }
  return ao_factory;
};

template <typename Tile, typename Policy>
AOFactory<Tile, Policy>& to_ao_factory(
    Factory<TA::DistArray<Tile, Policy>,
            DirectArray<Tile, Policy,
                        DirectIntegralBuilder<Tile, libint2::Engine>>>&
        factory) {
  return dynamic_cast<AOFactory<Tile, Policy>&>(factory);
};

template <typename Tile, typename Policy>
std::shared_ptr<AOFactory<Tile, Policy>> to_ao_factory(
    const std::shared_ptr<
        Factory<TA::DistArray<Tile, Policy>,
                DirectArray<Tile, Policy,
                            DirectIntegralBuilder<Tile, libint2::Engine>>>>&
        factory) {
  auto result = std::dynamic_pointer_cast<AOFactory<Tile, Policy>>(factory);
  TA_ASSERT(result != nullptr);
  return result;
};

/**
 * \brief Atomic Integral Class
 *
 *  This class computes atomic integral using  Formula
 *
 *  compute(formula) return TArray object
 *  (formula) return TArray expression
 *
 */

template <typename Tile, typename Policy>
class AOFactory : public AOFactoryBase<Tile, Policy> {
 public:
  using TArray = TA::DistArray<Tile, Policy>;
  using DirectTArray =
      DirectArray<Tile, Policy, DirectIntegralBuilder<Tile, libint2::Engine>>;
  using gtg_params_t = std::vector<std::pair<double, double>>;

  /// Op is a function pointer that convert TA::Tensor to Tile
  ///  using Op = Tile (*) (TA::TensorD&&);
  using Op = std::function<Tile(TA::TensorD&&)>;

 public:
  AOFactory() = default;

  // clang-format off
  /**
   * @brief The KeyVal constructor.
   *
   * @param kv the KeyVal object, it will be queried for all keywords of the Factory ctor as well as the
   * following additional keywords:
   *
   * @warning Those keys should be defined under WfnWorld object in order to get passed to AOFactory
   *
   *  | Keyword | Type | Default| Description |
   *  |---------|------|--------|-------------|
   *  |screen|string|schwarz|method of screening, qqr or schwarz |
   *  |screen_threshold| real |1e-12| screening threshold |
   *  |precision| real |std::numeric_limits<double>::epsilon() | integral precision |
   *  |iterative_inv_sqrt|bool|false| use iterative inverse square root |
   *  |f12_param|string|stg-6g[1]|Slater-type F12 correlation factor (defined if aux_basis exists in OrbitalBasisRegistry); valid format is \c stg-Ng[A] where \c N and \c A are nonzero integer and positive real parameters defining the Slater-type correlation factor as a linear combination of \c N Gaussian geminals: \f$ - \exp(- A r_{12})/A \approx \sum\limits_{i=1}^N c_i \exp(- \alpha_i r_{12}^2) \f$. @sa mpqc::lcao::f12::stg_ng_fit |
   */
  // clang-format on

  AOFactory(const KeyVal& kv);

  AOFactory(AOFactory&&) = default;
  AOFactory& operator=(AOFactory&&) = default;
  virtual ~AOFactory() noexcept {}

  /// @brief (contracted) Gaussian-types geminal parameters accessor
  /// @return Gaussian-type geminal parameters
  const gtg_params_t& gtg_params() const {
    TA_USER_ASSERT(not gtg_params_.empty(),
                   "Gaussian-type geminal not initialized");
    return gtg_params_;
  }

  using Factory<TArray, DirectTArray>::compute;
  using Factory<TArray, DirectTArray>::compute_direct;

  TArray compute(const Formula& formula) override;

  DirectTArray compute_direct(const Formula& formula) override;

  const std::string& screen() const { return screen_; }

  double screen_threshold() const { return screen_threshold_; }

 private:
  /// compute integrals that has two dimension
  TArray compute2(const Formula& formula_string);

  /// compute integrals that has three dimension
  TArray compute3(const Formula& formula_string);

  /// compute integrals that has four dimension
  TArray compute4(const Formula& formula_string);

  /// compute CADF fitting coefficients
  TArray compute_cadf_coeffs(const Formula& formula_string);

  /// compute integrals that has two dimension
  DirectTArray compute_direct2(const Formula& formula_string);

  /// compute integrals that has three dimension
  DirectTArray compute_direct3(const Formula& formula_string);

  /// compute integrals that has two dimension
  DirectTArray compute_direct4(const Formula& formula_string);

  ///
  /// Functions to parse Formula
  ///

  /// parse one body formula and set engine_pool and basis array
  void parse_one_body(
      const Formula& formula,
      std::shared_ptr<utility::TSPool<libint2::Engine>>& engine_pool,
      BasisVector& bases);

  /// parse two body two center formula and set two body kernel, basis array,
  /// max_nprim and max_am
  void parse_two_body_two_center(
      const Formula& formula,
      std::shared_ptr<utility::TSPool<libint2::Engine>>& engine_pool,
      BasisVector& bases);

  /// parse two body three center formula and set two body kernel, basis array,
  /// max_nprim and max_am
  void parse_two_body_three_center(
      const Formula& formula,
      std::shared_ptr<utility::TSPool<libint2::Engine>>& engine_pool,
      BasisVector& bases, std::shared_ptr<Screener>& p_screener);

  /// parse two body four center formula and set two body kernel, basis array,
  /// max_nprim and max_am
  void parse_two_body_four_center(
      const Formula& formula,
      std::shared_ptr<utility::TSPool<libint2::Engine>>& engine_pool,
      BasisVector& bases, std::shared_ptr<Screener>& p_screener);

  /// compute sparse array
  template <typename U = Policy>
  TA::DistArray<
      Tile, typename std::enable_if<std::is_same<U, TA::SparsePolicy>::value,
                                    TA::SparsePolicy>::type>
  compute_integrals(madness::World& world, ShrPool<libint2::Engine>& engine,
                    BasisVector const& bases,
                    std::shared_ptr<Screener> p_screen =
                        std::make_shared<Screener>(Screener{})) {
    auto result = sparse_integrals(world, engine, bases, p_screen, op_);
    return result;
  }

  /// compute dense array
  template <typename U = Policy>
  TA::DistArray<Tile,
                typename std::enable_if<std::is_same<U, TA::DensePolicy>::value,
                                        TA::DensePolicy>::type>
  compute_integrals(madness::World& world, ShrPool<libint2::Engine>& engine,
                    BasisVector const& bases,
                    std::shared_ptr<Screener> p_screen =
                        std::make_shared<Screener>(Screener{})) {
    auto result = dense_integrals(world, engine, bases, p_screen, op_);
    return result;
  }

  /// compute sparse array
  template <typename U = Policy>
  DirectArray<Tile,
              typename std::enable_if<std::is_same<U, TA::SparsePolicy>::value,
                                      TA::SparsePolicy>::type,
              DirectIntegralBuilder<Tile, libint2::Engine>>
  compute_direct_integrals(madness::World& world,
                           ShrPool<libint2::Engine>& engine,
                           BasisVector const& bases,
                           std::shared_ptr<Screener> p_screen =
                               std::make_shared<Screener>(Screener{}),
                           std::shared_ptr<const math::PetiteList> plist =
                               math::PetiteList::make_trivial()) {
    auto result =
        direct_sparse_integrals(world, engine, bases, p_screen, op_, plist);
    return result;
  }

  /// compute dense array
  template <typename U = Policy>
  DirectArray<Tile,
              typename std::enable_if<std::is_same<U, TA::DensePolicy>::value,
                                      TA::DensePolicy>::type,
              DirectIntegralBuilder<Tile, libint2::Engine>>
  compute_direct_integrals(madness::World& world,
                           ShrPool<libint2::Engine>& engine,
                           BasisVector const& bases,
                           std::shared_ptr<Screener> p_screen =
                               std::make_shared<Screener>(Screener{}),
                           std::shared_ptr<const math::PetiteList> plist =
                               math::PetiteList::make_trivial()) {
    auto result =
        direct_dense_integrals(world, engine, bases, p_screen, op_, plist);
    return result;
  }

 private:
  /// function to convert TA::TensorD to Tile
  Op op_;

  // TODO these specify operator params, need to abstract out better
  gtg_params_t gtg_params_;

  /// screen method
  std::string screen_;

  /// screen precision
  double screen_threshold_;

  /// integral precision
  double precision_;

  /// if do iterative inverse square root
  bool iterative_inv_sqrt_;
};

#if TA_DEFAULT_POLICY == 0
extern template class AOFactory<TA::TensorD, TA::DensePolicy>;
#elif TA_DEFAULT_POLICY == 1
extern template class AOFactory<TA::TensorD, TA::SparsePolicy>;
#endif

}  // namespace gaussian
}  // namespace lcao
}  // namespace mpqc

#include "ao_factory_impl.h"

#endif  // MPQC4_SRC_MPQC_CHEMISTRY_QC_LCAO_FACTORY_AO_FACTORY_H_
