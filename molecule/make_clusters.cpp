#include "cluster.h"
#include "molecule.h"
#include "make_clusters.h"

namespace tcc {
namespace molecule {
std::vector<std::shared_ptr<Cluster>>
attach_hydrogens_kmeans(Molecule const &m, unsigned long nclusters) {
    std::vector<std::shared_ptr<Cluster>> clusters;
    clusters.reserve(nclusters);

    for (auto &&cluster : m.attach_H_and_kmeans(nclusters)) {
        clusters.push_back(std::make_shared<Cluster>(std::move(cluster)));
    }

    auto sorter = [](std::shared_ptr<Cluster> const &a,
                     std::shared_ptr<Cluster> const &b) {
        position_t a_dist = a->center();
        position_t b_dist = b->center();
        if (!(a_dist.squaredNorm() == b_dist.squaredNorm())) {
            return a_dist.squaredNorm() < b_dist.squaredNorm();
        } else if (a_dist[0] == b_dist[0]) {
            return a_dist[0] < b_dist[0];
        } else if (a_dist[1] == b_dist[1]) {
            return a_dist[1] < b_dist[1];
        } else
            return a_dist[2] < b_dist[2];
    };
    std::sort(clusters.begin(), clusters.end(), sorter);

    return clusters;
}

std::vector<std::shared_ptr<Cluster>>
kmeans(Molecule const &m, unsigned long nclusters) {
    std::vector<std::shared_ptr<Cluster>> clusters;
    clusters.reserve(nclusters);

    for (auto &&cluster : m.kmeans(nclusters)) {
        clusters.push_back(std::make_shared<Cluster>(std::move(cluster)));
    }

    auto sorter = [](std::shared_ptr<Cluster> const &a,
                     std::shared_ptr<Cluster> const &b) {
        position_t a_dist = a->center();
        position_t b_dist = b->center();
        if (!(a_dist.squaredNorm() == b_dist.squaredNorm())) {
            return a_dist.squaredNorm() < b_dist.squaredNorm();
        } else if (a_dist[0] == b_dist[0]) {
            return a_dist[0] < b_dist[0];
        } else if (a_dist[1] == b_dist[1]) {
            return a_dist[1] < b_dist[1];
        } else
            return a_dist[2] < b_dist[2];
    };
    std::sort(clusters.begin(), clusters.end(), sorter);

    return clusters;
}

} // namespace molecule
} // namespace tcc
