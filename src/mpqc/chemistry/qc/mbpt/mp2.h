//
// Created by Chong Peng on 6/24/15.
//

#ifndef MPQC_CHEMISTRY_QC_MBPT_MP2_H
#define MPQC_CHEMISTRY_QC_MBPT_MP2_H

#include "mpqc/util/external/madworld/parallel_print.h"
#include "mpqc/chemistry/qc/wfn/trange1_engine.h"
#include <mpqc/chemistry/qc/integrals/lcao_factory.h>
#include <mpqc/chemistry/qc/scf/mo_build.h>
#include <mpqc/chemistry/qc/wfn/lcao_wfn.h>

using namespace mpqc;

namespace mpqc {
namespace mbpt {

namespace detail {

template <typename Tile>
struct Mp2Energy {
  using result_type = double;
  using argument_type = Tile;

  std::shared_ptr<Eigen::VectorXd> vec_;
  std::size_t n_occ_;
  std::size_t n_frozen_;

  Mp2Energy(std::shared_ptr<Eigen::VectorXd> vec, std::size_t n_occ,
            std::size_t n_frozen)
      : vec_(std::move(vec)), n_occ_(n_occ), n_frozen_(n_frozen) {}

  Mp2Energy(Mp2Energy const &) = default;

  result_type operator()() const { return 0.0; }

  result_type operator()(result_type const &t) const { return t; }

  void operator()(result_type &me, result_type const &other) const {
    me += other;
  }

  void operator()(result_type &me, argument_type const &tile) const {
    auto const &range = tile.range();
    auto const &vec = *vec_;
    auto const st = range.lobound_data();
    auto const fn = range.upbound_data();
    auto tile_idx = 0;

    auto sti = st[0];
    auto fni = fn[0];
    auto stj = st[1];
    auto fnj = fn[1];
    auto sta = st[2];
    auto fna = fn[2];
    auto stb = st[3];
    auto fnb = fn[3];

    for (auto i = sti; i < fni; ++i) {
      const auto e_i = vec[i + n_frozen_];
      for (auto j = stj; j < fnj; ++j) {
        const auto e_ij = e_i + vec[j + n_frozen_];
        for (auto a = sta; a < fna; ++a) {
          const auto e_ija = e_ij - vec[a + n_occ_];
          for (auto b = stb; b < fnb; ++b, ++tile_idx) {
            const auto e_iajb = e_ija - vec[b + n_occ_];
            me += 1.0 / (e_iajb)*tile.data()[tile_idx];
          }
        }
      }
    }
  }
};

} // end of namespce detail

class RMP2 : public qc::LCAOWavefunction<TA::TensorD, TA::SparsePolicy> {

public:

  /**
   * KeyVal constructor
   * @param kv
   *
   * keywords: takes all keywords from LCAOWavefunction
   * | KeyWord | Type | Default| Description |
   * |---------|------|--------|-------------|
   * | ref | Wavefunction | none | reference Wavefunction, RHF for example |
   */
  RMP2(const KeyVal& kv);
  virtual ~RMP2();

  double value() override;
  virtual double compute();
  void compute(qc::PropertyBase* pb) override;
  void obsolete() override;

protected:
  virtual void init();
  std::shared_ptr<qc::Wavefunction> ref_wfn_;
};

class RIRMP2 : public RMP2 {
public:

  /**
  * KeyVal constructor
  * @param kv
  *
  * keywords: takes all keywords from RMP2
  */
  RIRMP2(const KeyVal& kv);
  ~RIRMP2() = default;
  using RMP2::value;
  double compute() override;
};

}  // end of namespace mbpt
}  // end of namespace mpqc
#endif  // MPQC_CHEMISTRY_QC_MBPT_MP2_H