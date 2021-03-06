
#ifndef MPQC4_SRC_MPQC_CHEMISTRY_MOLECULE_COMMON_H_
#define MPQC4_SRC_MPQC_CHEMISTRY_MOLECULE_COMMON_H_

#include <vector>

#include <libint2/atom.h>

#include "mpqc/chemistry/molecule/cluster_concept.h"
#include "mpqc/chemistry/molecule/molecule_fwd.h"

namespace mpqc {

/// Function takes mpqc::molecule::Atom vector and converts it to a vector
/// of libint atoms.
std::vector<libint2::Atom> to_libint_atom(std::vector<Atom> const &);

namespace molecule {

/*! \brief Function to compute the center of mass of a collection
 *
 * Requires that the objects in the collection have both a mass and a
 * center_of_mass overload for free functions
 *
 * TODO Eventually add some static checking for the functions
 */
template <typename T>
Vector3d center_of_mass(std::vector<T> const &ts) {
  double total_mass = 0.0;
  Vector3d com = {0.0, 0.0, 0.0};

  // The overloads for mass and center_of_mass must be defined elsewhere
  // Center of mass formula: com = \frac{1}{total_mass}(\sum_k m_k * r_k)
  for (auto const &t : ts) {
    const auto t_mass = mass(t);
    total_mass += t_mass;
    com += t_mass * center_of_mass(t);
  }

  return com / total_mass;
}

template <typename T>
double sum_mass(std::vector<T> const &ts) {
  double total_mass = 0.0;
  for (auto const &t : ts) {
    total_mass += mass(t);
  }
  return total_mass;
}

template <typename T>
double sum_charge(std::vector<T> const &ts) {
  double total_charge = 0;
  for (auto const &t : ts) {
    total_charge += charge(t);
  }
  return total_charge;
}

template <typename T>
int64_t sum_atomic_number(std::vector<T> const &ts) {
  int64_t total_Z = 0;
  for (auto const &t : ts) {
    total_Z += total_atomic_number(t);
  }
  return total_Z;
}

template <typename T>
double sum_natoms(std::vector<T> const &ts) {
  double total_natoms = 0.0;
  for (auto const &t : ts) {
    total_natoms += natoms(t);
  }
  return total_natoms;
}

}  // namespace molecule
}  // namespace mpqc

#endif  // MPQC4_SRC_MPQC_CHEMISTRY_MOLECULE_COMMON_H_
