
#ifndef MPQC4_SRC_MPQC_CHEMISTRY_MOLECULE_CLUSTER_COLLAPSE_H_
#define MPQC4_SRC_MPQC_CHEMISTRY_MOLECULE_CLUSTER_COLLAPSE_H_

#include "mpqc/chemistry/molecule/atom.h"

#include <vector>

namespace mpqc {

/*! \brief collapse_to_atoms ends the recursive loop of the templated version of
  the function

  The return type is a vector because that simplifies the interface of the
  overload which does captures all types that are not Atoms.
*/
inline std::vector<Atom> collapse_to_atoms(const Atom &a) { return {a}; }

inline void update(Atom &a, const std::vector<Atom>& atoms, size_t& pos) {
  a = atoms.at(pos);
  ++pos;
}

/*! \brief collapse to atoms takes an iteratable list of types which provide an
 * atoms
 *  function.
 *
 *  The idea is that something like a vector of clusters of clusters will call
 *  call atoms on each cluster of clusters, which will internally call
 *  collapse_to_atoms on it's type recursing until the type being passed to
 *  collapse_to_atoms is just an atom.
 */
template <typename T>
std::vector<Atom> collapse_to_atoms(T const &t) {
  std::vector<Atom> atoms;
  for (const auto &element : t) {
    std::vector<Atom> temp_atoms = element.atoms();
    atoms.insert(atoms.end(), temp_atoms.begin(), temp_atoms.end());
  }
  return atoms;
}

}  // namespace mpqc

#endif  // MPQC4_SRC_MPQC_CHEMISTRY_MOLECULE_CLUSTER_COLLAPSE_H_
