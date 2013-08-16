//
// kcluster.hpp
//
// Copyright (C) 2013 Drew Lewis
//
// Authors: Drew Lewis
// Maintainer: Drew Lewis and Edward Valeev
//
// This file is part of the MPQC Toolkit.
//
// The MPQC Toolkit is free software; you can redistribute it and/or modify
// it under the terms of the GNU Library General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// The MPQC Toolkit is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public License
// along with the MPQC Toolkit; see the file COPYING.LIB.  If not, write to
// the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
//
// The U.S. Government is granted a limited license as per AL 91-7.
//

#ifndef mpqc_interfaces_tiledarray_tiling_kcluster_hpp
#define mpqc_interfaces_tiledarray_tiling_kcluster_hpp

#include <chemsitry/molecule/atom.h>
#include <Eigen/Dense>
#include <vector>

namespace mpqc {
namespace tiling {

    /**
     * class holds the information about the differnt clusters in k-means
     * tiling.
     */
    class KCluster {
        using Atom = sc::Atom;
        using Vector3 = Eigen::Vector3d;

        /**
         * Constructor takes an Eigen::Vector3d which designates the center
         * of the cluster.
         */
        KCluster(const Vector3 &pos = Vector3(0,0,0)) : center_(pos){}
        {}

        KCluster& operator=(const KCluster &rhs){
            center_ = rhs.center_;
            return *this;
        }

        /// Adds an atom to the cluster.
        void add_atom(const Atom &atom){
            atoms_.push_back(atom);
        }

        /// Finds distance to any atom to the center of the cluster.
        double distance(const Atom &atom){
            Vector3 atom_vec(atom.xyz(0), atom.xyz(1), atom.xyz(2));
            // Computes the length of the vector to the atom from the center.
            return (atom_vec - center_).norm();
        }
        /// Returns the position vector of the center of the cluster.
        const Vector3& center() const { return center_; }

        /// Returns the centorid of the cluster.
        Vector3 centroid(){

            std::size_t n_atoms = n_atoms();
            Vector3 centroid(0,0,0);

            // Loop over all of the members of the cluster and total their
            // positions in each diminsion.
            for(auto i = 0; i < n_members; ++i){
                centroid[0] += members_[i].xyz(0);
                centroid[1] += members_[i].xyz(1);
                centroid[2] += members_[i].xyz(2);
            }

            // Get the average position in each dimension.
            centroid[0] = centroid[0]/n_members;
            centroid[1] = centroid[1]/n_members;
            centroid[2] = centroid[2]/n_members;

            return centroid;

        }

        /// Returns the number of atoms in cluster.
        std::size_t n_atoms(){
            return atoms_.size();
        }

    private:
        Vector3 center_;
        std::vector<Atom> atoms_;
    }; // KCluster

} // namespace tiling
} // namespace mpqc


#endif /* mpqc_interfaces_tiledarray_tiling_kcluster_hpp */