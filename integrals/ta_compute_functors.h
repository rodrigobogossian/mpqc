#pragma once
#ifndef TCC_INTEGRALS_TACOMPUTEFUNCTORS_H
#define TCC_INTEGRALS_TACOMPUTEFUNCTORS_H

#include "../include/libint.h"
#include "../include/btas.h"
#include "../basis/cluster_shells.h"
#include "../include/tiledarray.h"
#include "../basis/basis.h"
#include "integral_engine_pool.h"
#include "../molecule/cluster_collapse.h"

#include "../tensor/tile_pimpl_devel.h"

#include <tuple>

namespace tcc {
namespace integrals {
namespace compute_functors {
namespace ta_detail {

template <typename Engine, typename T, typename... Shells>
void ta_btas_compute_kernel(Engine &engine, btas::TensorView<T> &view,
                            Shells... shells) {

    auto buf = engine.compute(shells...);

    auto i = 0;
    auto end = view.end();
    for (auto it = view.begin(); it != end; ++it) {
        *it = buf[i++];
    }
}

template <typename T, unsigned long>
struct ta_integral_wrapper;

template <typename T>
struct ta_integral_wrapper<T, 2ul> {
    template <typename Engine>
    btas::Tensor<T>
    operator()(Engine engine,
               std::vector<basis::ClusterShells> const &clusters) const {

        btas::Tensor<T> tensor{clusters[0].flattened_nfunctions(),
                               clusters[1].flattened_nfunctions()};

        if (engine.integral_type() == libint2::OneBodyEngine::nuclear) {
            std::vector<std::pair<double, std::array<double, 3>>> q;
            for (auto const &shell_cluster : clusters) {
                for (auto const &atom :
                     molecule::collapse_to_atoms(shell_cluster.cluster())) {
                    auto c = atom.center();
                    q.push_back({static_cast<double>(atom.charge()),
                                 {{c[0], c[1], c[2]}}});
                }
            }
            engine.set_q(q);
        }

        auto sh1_start = 0;
        for (auto const &sh1 : clusters[0].flattened_shells()) {
            auto sh1_size = sh1.size();
            auto sh2_start = 0;
            for (auto const &sh2 : clusters[1].flattened_shells()) {
                auto sh2_size = sh2.size();

                auto lower_bound = {sh1_start, sh2_start};
                auto upper_bound = {sh1_start + sh1_size, sh2_start + sh2_size};

                auto view = btas::make_view(
                    tensor.range().slice(lower_bound, upper_bound),
                    tensor.storage());

                ta_btas_compute_kernel(engine, view, sh1, sh2);

                sh2_start += sh2_size;
            }
            sh1_start += sh1_size;
        }

        return tensor;
    }

}; // integralwrapper<T, 2ul> specialization

template <typename T>
struct ta_integral_wrapper<T, 3ul> {
    template <typename Engine>
    btas::Tensor<T>
    operator()(Engine engine,
               std::vector<basis::ClusterShells> const &clusters) const {

        btas::Tensor<T> tensor{clusters[0].flattened_nfunctions(),
                               clusters[1].flattened_nfunctions(),
                               clusters[2].flattened_nfunctions()};

        libint2::Shell unit{
            {0.0}, // exponent
            {{0, false, {1.0}}},
            {{0.0, 0.0, 0.0}} // placed at origin
        };
        unit.renorm();

        auto sh1_start = 0;
        for (auto const &sh1 : clusters[0].flattened_shells()) {
            auto sh1_size = sh1.size();
            auto sh2_start = 0;
            for (auto const &sh2 : clusters[1].flattened_shells()) {
                auto sh2_size = sh2.size();
                auto sh3_start = 0;
                for (auto const &sh3 : clusters[2].flattened_shells()) {
                    auto sh3_size = sh3.size();

                    auto lower_bound = {sh1_start, sh2_start, sh3_start};
                    auto upper_bound
                        = {sh1_start + sh1_size, sh2_start + sh2_size,
                           sh3_start + sh3_size};

                    auto view = btas::make_view(
                        tensor.range().slice(lower_bound, upper_bound),
                        tensor.storage());

                    ta_btas_compute_kernel(engine, view, sh1, unit, sh2, sh3 );

                    sh3_start += sh3_size;
                }
                sh2_start += sh2_size;
            }
            sh1_start += sh1_size;
        }

        return tensor;
    }

}; // integralwrapper<T, 2ul> specialization

template <typename T>
struct ta_integral_wrapper<T, 4ul> {
    template <typename Engine>
    btas::Tensor<T>
    operator()(Engine engine,
               std::vector<basis::ClusterShells> const &clusters) const {

        btas::Tensor<T> tensor{clusters[0].flattened_nfunctions(),
                               clusters[1].flattened_nfunctions(),
                               clusters[2].flattened_nfunctions(),
                               clusters[3].flattened_nfunctions()};

        auto sh1_start = 0;
        for (auto const &sh1 : clusters[0].flattened_shells()) {
            auto sh1_size = sh1.size();
            auto sh2_start = 0;

            for (auto const &sh2 : clusters[1].flattened_shells()) {
                auto sh2_size = sh2.size();
                auto sh3_start = 0;
                for (auto const &sh3 : clusters[2].flattened_shells()) {
                    auto sh3_size = sh3.size();
                    auto sh4_start = 0;

                    for (auto const &sh4 : clusters[3].flattened_shells()) {
                        auto sh4_size = sh4.size();

                        auto lower_bound
                            = {sh1_start, sh2_start, sh3_start, sh4_start};
                        auto upper_bound
                            = {sh1_start + sh1_size, sh2_start + sh2_size,
                               sh3_start + sh3_size, sh4_start + sh4_size};

                        auto view = btas::make_view(
                            tensor.range().slice(lower_bound, upper_bound),
                            tensor.storage());

                        ta_btas_compute_kernel(engine, view, sh1, sh2, sh3,
                                               sh4);

                        sh4_start += sh4_size;
                    }
                    sh3_start += sh3_size;
                }
                sh2_start += sh2_size;
            }
            sh1_start += sh1_size;
        }

        return tensor;
    }

}; // integralwrapper<T, 2ul> specialization
} // namespace ta_detail

template <typename BasisPtr, typename... Rest>
struct basis_ptr_type {
    using type = BasisPtr;
};

template <typename Index, typename... BasesPtrs>
std::vector<basis::ClusterShells>
get_shells(Index idx, BasesPtrs... bases_ptrs) {
    constexpr auto dims = sizeof...(BasesPtrs);
    using ptr_t = typename basis_ptr_type<BasesPtrs...>::type;
    std::array<ptr_t, dims> expanded_bases_ptrs{{bases_ptrs...}};

    std::vector<basis::ClusterShells> clusters;
    clusters.reserve(dims);

    for (auto i = 0ul; i < dims; ++i) {
        const auto cluster_number = idx[i];
        auto basis_ptr = expanded_bases_ptrs[i];
        clusters.push_back(basis_ptr->cluster_shells()[cluster_number]);
    }
    return clusters;
}

template <typename T>
struct TaTileFunctor {
    using TileType = TiledArray::Tensor<T>;

    template <typename Index, typename SharedEnginePool, typename... BasesPtrs>
    TileType
    operator()(TiledArray::Range range, Index index, SharedEnginePool engines,
               BasesPtrs... bases_ptrs) const {

        constexpr auto dims = sizeof...(BasesPtrs);
        auto btas_tensor = ta_detail::ta_integral_wrapper<T, dims>{}(
            engines->local(), get_shells(index, bases_ptrs...));

        // copy to TA
        TileType ta_tensor{range};
        auto data = ta_tensor.data();
        auto const size = ta_tensor.size();
        for (auto i = 0ul; i < size; ++i) {
            *(data + i) = btas_tensor.storage()[i];
        }

        return ta_tensor;
    }

}; // TaTileFunctor


} // namespace compute_functors
} // namespace integrals
} // namespace tcc

#endif /* end of include guard: TCC_INTEGRALS_TACOMPUTEFUNCTORS_H */
