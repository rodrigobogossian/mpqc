#include "../include/tbb.h"
#include "clustering_functions.h"
#include "common.h"
#include "cluster.h"

#include <cassert>
#include <numeric> 
#include <random>

namespace tcc {
namespace molecule {
namespace clustering {

double sum_cluster_distances(const std::vector<Cluster> &clusters) {
    return std::accumulate(clusters.begin(), clusters.end(), 0.0,
                           [](double d, const Cluster &c) {
        return d + c.sum_distances_from_center();
    });
}

kmeans::kmeans(unsigned long seed) : seed_{seed}, clusters_() {}

output_t kmeans::
operator()(input_t const &clusterables, unsigned long nclusters) {
    assert(clusterables.size() > nclusters);
    clusters_.resize(nclusters);
    initialize_clusters(clusterables);
    return cluster(clusterables);
}

void kmeans::initialize_clusters(const input_t &clusterables) {

    std::vector<double> weights(clusterables.size(), 1.0);
    std::mt19937 engine(seed_);

    auto end = clusters_.end();
    for (auto it = clusters_.begin(); it != end; ++it) {
        std::discrete_distribution<unsigned int> random_index(weights.begin(),
                                                              weights.end());

        auto center_guess = clusterables[random_index(engine)].center();
        it->init_center(center_guess);

        // For each clusterable
        tbb::affinity_partitioner ap;
        tbb::parallel_for(
            0ul, clusterables.size(),
            [&](unsigned long i) {
                const auto clusterable_center = clusterables[i].center();

                // Find the closes cluster that has been initialized.
                const auto cluster_center
                    = closest_cluster(clusters_.begin(), it, clusterable_center)
                          ->center();

                // Calculate weight = dist^2
                weights[i]
                    = diff_squaredNorm(clusterable_center, cluster_center);
            },
            ap);
    }

    attach_clusterables(clusterables);
}

void kmeans::attach_clusterables(const std::vector<Clusterable> &cs) {
    // Erase the ownership information in the cluster.
    tbb::parallel_for_each(clusters_.begin(), clusters_.end(),
                           [](Cluster &c) { c.clear(); });

    // for each clusterable
    tbb::spin_mutex cluster_mutex;
    tbb::parallel_for_each(cs.begin(), cs.end(), [&](const Clusterable &c) {
        // Find closest cluster
        auto iter
            = closest_cluster(clusters_.begin(), clusters_.end(), c.center());

        tbb::spin_mutex::scoped_lock lock(cluster_mutex);
        iter->add_clusterable(c);
    });

    tbb::parallel_for_each(clusters_.begin(), clusters_.end(),
                           [](Cluster &c) { c.guess_center(); });
}

std::vector<Cluster>::iterator
kmeans::closest_cluster(const std::vector<Cluster>::iterator begin,
                        const std::vector<Cluster>::iterator end,
                        position_t const &center) {
    return std::min_element(begin, end,
                            [&](const Cluster &a, const Cluster &b) {
        return diff_squaredNorm(a.center(), center)
               < diff_squaredNorm(b.center(), center);
    });
}


std::vector<Cluster::position_t>
kmeans::update_clusters(std::vector<Clusterable> const &clusterables) {
    // temp to hold the old clusters must allocate for parallel loop.
    std::vector<Clusterable::position_t> old_centers(clusters_.size());

    // store the old centers and guess new ones.
    tbb::affinity_partitioner ap;
    tbb::parallel_for(
        0ul, clusters_.size(),
        [&](unsigned long i) { old_centers[i] = clusters_[i].center(); }, ap);

    attach_clusterables(clusterables);

    return old_centers; // Let's go rvo!!;
}

bool
kmeans::kmeans_converged(const std::vector<Cluster::position_t> &old_centers) {

    return std::equal(
        old_centers.begin(), old_centers.end(), clusters_.begin(),
        [](const Cluster::position_t &old_center, const Cluster &new_cluster) {
            return 1e-15 < (old_center - new_cluster.center()).squaredNorm();
        });
}

output_t kmeans::cluster(const std::vector<Clusterable> &clusterables) {
    unsigned int niter = 100, iter = 0;

    // initialize the old centers and update the clusters
    auto old_centers = update_clusters(clusterables);

    while (!kmeans_converged(old_centers) && (niter > iter++)) {
        // save old centers and update the clusters at the same time.
        old_centers = update_clusters(clusterables);
    }

    return clusters_;
}
} // namespace clustering
} // namespace molecule
} // namespace tcc
