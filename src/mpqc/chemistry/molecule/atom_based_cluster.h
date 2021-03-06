
#ifndef MPQC4_SRC_MPQC_CHEMISTRY_MOLECULE_ATOM_BASED_CLUSTER_H_
#define MPQC4_SRC_MPQC_CHEMISTRY_MOLECULE_ATOM_BASED_CLUSTER_H_

#include "mpqc/chemistry/molecule/atom_based_cluster_concept.h"
#include "mpqc/chemistry/molecule/molecule_fwd.h"

#include <iosfwd>
#include <vector>

namespace mpqc {
/*!
 * \ingroup Molecule
 *
 * @{
 */

/*!
 * \brief is the unit that holds a collection of atom based clusterables that go
 *together.
 *
 * AtomBasedCluster will hold a vector of clusterables that all belong together.
 * To update the center of the cluster compute_com must be called, this is
 * to avoid computing a new center of mass (COM) every time a clusterable is
 * added.
 */
class AtomBasedCluster {
 private:
  std::vector<AtomBasedClusterable> elements_;
  Vector3d com_ = {0, 0, 0};
  double mass_ = 0.0;
  int64_t total_atomic_number_ = 0.0;
  size_t natoms_ = 0.0;

  friend void update(AtomBasedCluster &abc,
                                const std::vector<Atom> &atoms, size_t &pos);

 public:
  AtomBasedCluster() = default;
  AtomBasedCluster(const AtomBasedCluster &c) = default;
  AtomBasedCluster &operator=(const AtomBasedCluster &c) = default;

  AtomBasedCluster(AtomBasedCluster &&c) = default;
  AtomBasedCluster &operator=(AtomBasedCluster &&c) = default;

  explicit AtomBasedCluster(std::vector<AtomBasedClusterable> const &elems)
      : elements_(elems) {
    update_cluster();
  }
  explicit AtomBasedCluster(std::vector<AtomBasedClusterable> &&elems)
      : elements_(std::move(elems)) {
    update_cluster();
  }

  // When constructed from list update immediately
  template <typename... Cs>
  explicit AtomBasedCluster(Cs... cs) : elements_{std::move(cs)...} {
    update_cluster();
  }

  template <typename T>
  void add_clusterable(T t) {
    elements_.emplace_back(std::move(t));
  }

  int64_t size() const { return elements_.size(); }

  int64_t total_atomic_number() const { return total_atomic_number_; }
  double mass() const { return mass_; }

  std::vector<Atom> atoms() const;
  size_t natoms() const;

  void clear() { elements_.clear(); }

  /**
   * @brief sets the center equal to a point.
   */
  void set_com(Vector3d point) { com_ = point; }

  /**
   * @brief will update the center based on the current elements.
   *
   * This is done as a separate step because it would be inefficent to update
   *after each addition of a AtomBasedClusterable
   */
  void update_cluster();

  inline Vector3d const &com() const { return com_; }

  /**
   * @brief begin returns the begin iterator to the vector of clusterables.
   */
  inline std::vector<AtomBasedClusterable>::const_iterator begin() const {
    return elements_.begin();
  }

  /**
   * @brief end returns the end iterator to the vector of clusterables
   */
  inline std::vector<AtomBasedClusterable>::const_iterator end() const {
    return elements_.end();
  }
};

// External interface
/*! \brief print the cluster by printing each of its elements
 *
 */
std::ostream &operator<<(std::ostream &, AtomBasedCluster const &);

/*! \brief Returns the Center of mass of the cluster.
 *
 * This function exists to allow interfacing with generic clustering code.
 * Overloading center to return the center of mass is in some sense specializing
 * center for atoms.
 */
inline Vector3d const &center(AtomBasedCluster const &c) { return c.com(); }

inline double mass(AtomBasedCluster const &c) { return c.mass(); }

inline int64_t total_atomic_number(AtomBasedCluster const &c) { return c.total_atomic_number(); }

inline Vector3d const &center_of_mass(AtomBasedCluster const &c) {
  return c.com();
}

inline size_t natoms(AtomBasedCluster const &c) { return c.mass(); }

/// converts an AtomBasedCluster to a vector of Atom's
/// @param abc the AtomBasedCluster object
/// @return the atoms sequence
std::vector<Atom> collapse_to_atoms(AtomBasedCluster const & abc);

/// updatesan AtomBasedCluster using a vector of Atom's
/// @param abc the AtomBasedCluster object
/// @param atoms the vector of Atom objects
/// @param pos the index of the next Atom object in Atoms to be used
void update(AtomBasedCluster& abc, const std::vector<Atom>& atoms, size_t& pos);

inline void set_center(AtomBasedCluster &c, Vector3d const &point) {
  c.set_com(point);
}

inline void remove_clusterables(AtomBasedCluster &c) { c.clear(); }

template <typename T>
inline void attach_clusterable(AtomBasedCluster &c, T t) {
  c.add_clusterable(std::move(t));
}

inline void update_center(AtomBasedCluster &c) { c.update_cluster(); }

/*! @} */

}  // namespace mpqc

#endif  // MPQC4_SRC_MPQC_CHEMISTRY_MOLECULE_ATOM_BASED_CLUSTER_H_
