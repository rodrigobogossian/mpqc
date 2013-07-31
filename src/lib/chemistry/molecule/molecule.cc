//
// molecule.cc
//
// Copyright (C) 1996 Limit Point Systems, Inc.
//
// Author: Curtis Janssen <cljanss@limitpt.com>
// Maintainer: LPS
//
// This file is part of the SC Toolkit.
//
// The SC Toolkit is free software; you can redistribute it and/or modify
// it under the terms of the GNU Library General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// The SC Toolkit is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public License
// along with the SC Toolkit; see the file COPYING.LIB.  If not, write to
// the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
//
// The U.S. Government is granted a limited license as per AL 91-7.
//

#include <math.h>
#include <string.h>
#include <stdio.h>

#include <util/misc/scexception.h>
#include <util/misc/formio.h>
#include <util/state/stateio.h>
#include <chemistry/molecule/molecule.h>
#include <chemistry/molecule/formula.h>
#include <chemistry/molecule/localdef.h>
#include <math/scmat/cmatrix.h>
#include <algorithm>


using namespace std;
using namespace sc;

//////////////////////////////////////////////////////////////////////////
// Molecule

static ClassDesc Molecule_cd(
  typeid(Molecule),"Molecule",8,"public SavableState",
  create<Molecule>, create<Molecule>, create<Molecule>);

Molecule::Molecule():
  natoms_(0), atoms_()
{
  pg_ = new PointGroup;
  atominfo_ = new AtomInfo();
  geometry_units_ = new Units("bohr");
  nuniq_ = 0;
  equiv_ = 0;
  nequiv_ = 0;
  atom_to_uniq_ = 0;
  q_Z_ = atominfo_->string_to_Z("Q");
  include_q_ = false;
  include_qq_ = false;
  init_symmetry_info();
  std::fill(ref_origin_, ref_origin_+3, 0.0);
}

Molecule::Molecule(const Molecule& mol):
 natoms_(0), atoms_()
{
  nuniq_ = 0;
  equiv_ = 0;
  nequiv_ = 0;
  atom_to_uniq_ = 0;
  std::fill(ref_origin_, ref_origin_+3, 0.0);
  *this=mol;
}

Molecule::~Molecule()
{
  clear();
}

void
Molecule::clear()
{
  std::fill(ref_origin_, ref_origin_+3, 0.0);

  clear_symmetry_info();
}

void
Molecule::throw_if_atom_duplicated(int begin, double tol)
{
  for (int i=begin; i<natoms_; i++) {
      SCVector3 ri(atoms_[i].r());
      for (int j=0; j<i; j++) {
          SCVector3 rj(atoms_[j].r());
          if (ri.dist(rj) < tol) {
              throw InputError("duplicated atom coordinate",
                               __FILE__, __LINE__, 0, 0, class_desc());
            }
        }
    }
}

Molecule::Molecule(const Ref<KeyVal>&input):
 natoms_(0), atoms_()
{
  nuniq_ = 0;
  equiv_ = 0;
  nequiv_ = 0;
  atom_to_uniq_ = 0;
  std::fill(ref_origin_, ref_origin_+3, 0.0);

  KeyValValueboolean kvfalse(0);
  include_q_ = input->booleanvalue("include_q",kvfalse);
  include_qq_ = input->booleanvalue("include_qq",kvfalse);

  atominfo_ << input->describedclassvalue("atominfo");
  if (atominfo_.null()) atominfo_ = new AtomInfo;
  q_Z_ = atominfo_->string_to_Z("Q");
  if (input->exists("pdb_file")) {
      geometry_units_ = new Units("angstrom");
      std::string filename = input->stringvalue("pdb_file");
      read_pdb(filename.c_str());
    }
  else {
      // check for old style units input first
      if (input->booleanvalue("angstrom")
          ||input->booleanvalue("angstroms")
          ||input->booleanvalue("aangstrom")
          ||input->booleanvalue("aangstroms")) {
          geometry_units_ = new Units("angstrom");
        }
      // check for new style units input
      else {
          std::string tmp = input->stringvalue("unit");
          if (input->exists("unit") == false && input->exists("units") == true)
            tmp = input->stringvalue("units");
          geometry_units_ = new Units(tmp.c_str());
        }

      double conv = geometry_units_->to_atomic_units();

      // get the number of atoms and make sure that the geometry and the
      // atoms array have the same number of atoms.
      // right now we read in the unique atoms...then we will symmetrize.
      // the length of atoms must still equal the length of geometry, but
      // we'll try to set up atom_labels such that different lengths are
      // possible
      int natom = input->count("geometry");
      if (natom != input->count("atoms")) {
          std::cout << "I should be throwing" << std::endl;
          std::cout << "natom = " << natom << std::endl;
          std::cout << "atoms = " << input->count("atoms") << std::endl;
          throw InputError("size of \"geometry\" != size of \"atoms\"",
                           __FILE__, __LINE__, 0, 0, class_desc());
        }

      atoms_.reserve(natom);

      for (int i=0; i<natom; i++) {
          int ghost = input->booleanvalue("ghost",i);
          double charge = input->doublevalue("charge",i);
          int have_charge = input->error() == KeyVal::OK;
          if (ghost) {
              have_charge = 1;
              charge = 0.0;
            }
          const int fragment = input->intvalue("fragment",i);
          const int have_fragment = (input->error() == KeyVal::OK);
          add_atom(atominfo_->string_to_Z(input->stringvalue("atoms",i)),
                   input->doublevalue("geometry",i,0)*conv,
                   input->doublevalue("geometry",i,1)*conv,
                   input->doublevalue("geometry",i,2)*conv,
                   input->stringvalue("atom_labels",i),
                   input->doublevalue("mass",i),
                   have_charge, charge,
                   have_fragment, fragment
              );
        }
    }

  std::string symmetry = input->stringvalue("symmetry");
  double symtol = input->doublevalue("symmetry_tolerance",
                                     KeyValValuedouble(1.0e-4));
  nuniq_ = 0;
  equiv_ = 0;
  nequiv_ = 0;
  atom_to_uniq_ = 0;
  if (symmetry == "auto") {
      set_point_group(highest_point_group(symtol), symtol*10.0);
    }
  else {
      pg_ = new PointGroup(input);

      const double conv = geometry_units_->to_atomic_units();
      // translate to the origin of the symmetry frame
      double r[3];
      for (int i=0; i<3; i++) {
          r[i] = -pg_->origin()[i] * conv;
          pg_->origin()[i] = 0;
        }
      translate(r);

      if (input->booleanvalue("redundant_atoms")) {
          init_symmetry_info();
          cleanup_molecule(symtol);
        }
      else {
          symmetrize();
          // In case we were given redundant atoms, clean up
          // the geometry so the symmetry is exact.
          cleanup_molecule(symtol);
        }
    }
}

Molecule&
Molecule::operator=(const Molecule& mol)
{
  clear();

  pg_ = new PointGroup(*(mol.pg_.pointer()));
  atominfo_ = mol.atominfo_;
  geometry_units_ = new Units(mol.geometry_units_->string_rep());

  q_Z_ = mol.q_Z_;
  include_q_ = mol.include_q_;
  include_qq_ = mol.include_qq_;
  q_atoms_ = mol.q_atoms_;
  non_q_atoms_ = mol.non_q_atoms_;

  natoms_ = mol.natoms_;
  atoms_ = mol.atoms_;

  std::copy(mol.ref_origin_, mol.ref_origin_+3, ref_origin_);

  init_symmetry_info();

  return *this;
}

void
Molecule::add_atom(int Z,double x,double y,double z,
                   const std::string &label,double mass,
                   int have_charge, double charge,
                   int have_fragment, int fragment)
{
  atoms_.push_back(Atom(Z,x,y,z,label,mass, have_charge, charge, have_fragment,
                        fragment));
  (Z == q_Z_) ? q_atoms_.push_back(natoms_) : 
                non_q_atoms_.push_back(natoms_);
  natoms_++;

  throw_if_atom_duplicated(natoms_-1);
}

// Used to make input files.
void
Molecule::print_parsedkeyval(ostream& os,
                             int print_pg,
                             int print_unit,
                             int number_atoms) const
{
  int i;

  double conv = geometry_units_->from_atomic_units();

  if (print_pg) pg_->print(os);
  if (print_unit && geometry_units_->string_rep()) {
      os << indent
         << "unit = \"" << geometry_units_->string_rep() << "\""
         << endl;
    }
  os << indent << "{";

  if (number_atoms) os << scprintf("%3s", "n");

  os << scprintf(" %5s", "atoms");

  // Took out if(label_)
  bool label_ = any_atom_has_label();
  if(label_) os << scprintf(" %11s", "atom_labels");

  bool charges_ = any_atom_has_charge();
  int int_charges = 1;
  if(charges_){
      for (i=0;i<natom();i++){
          if (atoms_[i].charge() != int( atoms_[i].charge()) )
              int_charges=0;
      }
      if (int_charges) {
          os << scprintf(" %7s", "charge");
      }
      else {
          os << scprintf(" %17s", "charge");
      }
  }

  bool fragment_ = any_atom_has_fragment();
  if(fragment_) os << scprintf(" %8s", "fragment");

  os << scprintf("  %16s", "")
     << scprintf(" %16s", "geometry   ")
     << scprintf(" %16s ", "");
  os << "}={" << endl;


  for (i=0; i<natom(); i++) {
      os << indent;
      if (number_atoms) os << scprintf(" %3d", i+1);
      std::string symbol(atom_symbol(i));
      os << scprintf(" %5s", symbol.c_str());
      if (label_) {
          const char *lab = atoms_[i].label().c_str();
          if (lab == 0) lab = "";
          char  *qlab = new char[strlen(lab)+3];
          strcpy(qlab,"\"");
          strcat(qlab,lab);
          strcat(qlab,"\"");
          os << scprintf(" %11s",qlab);
          delete[] qlab;
        }
      if (charges_) {
          if (int_charges) os << scprintf(" %7.4f", atoms_[i].charge());
          else os << scprintf(" %17.15f", atoms_[i].charge());
        }
      if (fragment_) {
          os << scprintf(" %8d", atoms_[i].fragment() );
      }
      os << scprintf(" [% 16.10f", conv * r(i,0))
         << scprintf(" % 16.10f", conv * r(i,1))
         << scprintf(" % 16.10f]", conv * r(i,2))
         << endl;
    }
  os << indent << "}" << endl;
}

void
Molecule::print(ostream& os) const
{
  int i;

  MolecularFormula *mf = new MolecularFormula(this);
  os << indent
     << "Molecular formula: " << mf->formula() << endl;
  delete mf;

  os << indent << "molecule<Molecule>: (" << endl;
  os << incindent;
  print_parsedkeyval(os);
  os << decindent;
  os << indent << ")" << endl;

  os << indent << "Atomic Masses:" << endl;
  for (i=0; i<natom(); i+=5) {
      os << indent;
      for (int j=i; j<i+5 && j<natom(); j++) {
          os << scprintf(" %10.5f", mass(j));
        }
      os << endl;
    }

  os << indent << "Reference origin = "
     << scprintf(" [% 16.10f", ref_origin_[0])
     << scprintf(" % 16.10f", ref_origin_[1])
     << scprintf(" % 16.10f]", ref_origin_[2])
     << endl;
}

int
Molecule::atom_label_to_index(const std::string &l) const
{
  int i;
  for (i=0; i<natom(); i++) {
      if (label(i) && l == label(i)) return i;
    }
  return -1;
}

// only for use with sort_by_distance()
struct dist_sort{

   dist_sort(const SCVector3 &v = SCVector3(0,0,0)) : v_(v) {}

   bool operator() (const Atom a1, const Atom a2) const {
       const SCVector3 v1(a1.r());
       const SCVector3 v2(a2.r());
       double d1 = v_.dist(v1);
       double d2 = v_.dist(v2);
       return ((d1 < d2) ||
               (d1 == d2 && v1[0] < v2[0]) ||
               (d1 == d2 && v1[0] == v2[0] && v1[1] < v2[1]) ||
               (d1 == d2 && v1[0] == v2[0] && v1[1] == v2[1] && v1[2] < v2[2])
              );
   }

private:
   SCVector3 v_;
};

void
Molecule::sort_by_distance(){
  std::sort( atoms_.begin(), atoms_.end(), dist_sort());
}

double*
Molecule::charges() const
{
  double*result = new double[natoms_];

  for(int a = 0; a < natoms_; ++a){
      result[a] = atoms_[a].have_charge() ? atoms_[a].charge() : atoms_[a].Z();
  }

  return result;
}

double
Molecule::charge(int iatom) const
{
  return atoms_[iatom].have_charge() ? atoms_[iatom].charge() : atoms_[iatom].Z();
}

bool
Molecule::is_Q(int iatom) const
{
  return Z_[iatom] == q_Z_;
}

int
Molecule::fragment(int iatom) const
{
  return atoms_[iatom].have_fragment() ? atoms_[iatom].fragment() : 0;
}

double
Molecule::nuclear_charge() const
{
  double c = 0.0;
  if (include_q_) {
    for (int i=0; i<natom(); i++) {
      c += charge(i);
    }
  }
  else {
    for (int ii=0; ii<non_q_atoms_.size(); ii++) {
      c += charge(non_q_atoms_[ii]);
    }
  }
  return c;
}

void Molecule::save_data_state(StateOut& so)
{
  so.put(include_q_);
  so.put(include_qq_);
  so.put(atoms_);
  so.put(natoms_);

  SavableState::save_state(pg_.pointer(),so);
  SavableState::save_state(geometry_units_.pointer(),so);
  SavableState::save_state(atominfo_.pointer(),so);

  so.put_array_double(ref_origin_,3);
}

Molecule::Molecule(StateIn& si):
  SavableState(si),
  natoms_(0), atoms_()
{
  if (si.version(::class_desc<Molecule>()) < 4) {
      throw FileOperationFailed("cannot restore from old molecules",
                                __FILE__, __LINE__, 0,
                                FileOperationFailed::Corrupt,
                                class_desc());
    }
  if (si.version(::class_desc<Molecule>()) < 6) {
    include_q_ = false;
    include_qq_ = false;
    }
  else {
    si.get(include_q_);
    si.get(include_qq_);
  }
  si.get(atoms_);
  si.get(natoms_);
  pg_ << SavableState::restore_state(si);
  geometry_units_ << SavableState::restore_state(si);
  atominfo_ << SavableState::restore_state(si);
  q_Z_ = atominfo_->string_to_Z("Q");

  if (si.version(::class_desc<Molecule>()) > 7) {
    si.get_array_double(ref_origin_, 3);
  }
  else {
    std::fill(ref_origin_, ref_origin_+3, 0.0);
  }

  for (int i=0; i<natoms_; i++) {
      if (atoms_[i].Z() == q_Z_) {
          q_atoms_.push_back(i);
        }
      else {
          non_q_atoms_.push_back(i);
        }
    }

  nuniq_ = 0;
  equiv_ = 0;
  nequiv_ = 0;
  atom_to_uniq_ = 0;
  init_symmetry_info();

}

int
Molecule::atom_to_unique_offset(int iatom) const
{
  int iuniq = atom_to_uniq_[iatom];
  int nequiv = nequiv_[iuniq];
  for (int i=0; i<nequiv; i++) {
      if (equiv_[iuniq][i] == iatom) return i;
    }
  ExEnv::errn() << "Molecule::atom_to_unique_offset: internal error"
               << endl;
  return -1;
}

void
Molecule::set_point_group(const Ref<PointGroup>&ppg, double tol)
{
  ExEnv::out0() << indent
       << "Molecule: setting point group to " << ppg->symbol()
       << endl;
  pg_ = new PointGroup(*ppg.pointer());

  double r[3];
  for (int i=0; i<3; i++) {
      r[i] = -pg_->origin()[i];
      pg_->origin()[i] = 0;
    }
  translate(r);

  clear_symmetry_info();
  init_symmetry_info();

  cleanup_molecule(tol);
}

const Ref<PointGroup>&
Molecule::point_group() const
{
  return pg_;
}

SCVector3
Molecule::center_of_mass() const
{
  SCVector3 ret;
  double M;

  ret = 0.0;
  M = 0.0;

  for (int i=0; i < natom(); i++) {
    double m = mass(i);
    ret += m * SCVector3(r(i));
    M += m;
  }

  ret *= 1.0/M;

  return ret;
}

double
Molecule::nuclear_repulsion_energy()
{
  double e=0.0;

  // non_q non_q terms
  for (int ii=1; ii < non_q_atoms_.size(); ii++) {
      int i = non_q_atoms_[ii];
      SCVector3 ai(r(i));
      double Zi = charge(i);

      for (int jj=0; jj < ii; jj++) {
          int j = non_q_atoms_[jj];
          SCVector3 aj(r(j));
          e += Zi * charge(j) / ai.dist(aj);
        }
    }

  // non_q q terms
  for (int ii=0; ii < q_atoms_.size(); ii++) {
      int i = q_atoms_[ii];
      SCVector3 ai(r(i));
      double Zi = charge(i);

      for (int jj=0; jj < non_q_atoms_.size(); jj++) {
          int j = non_q_atoms_[jj];
          SCVector3 aj(r(j));
          e += Zi * charge(j) / ai.dist(aj);
        }
    }

  if (include_qq_) {
      // q q terms
      for (int ii=1; ii < q_atoms_.size(); ii++) {
          int i = q_atoms_[ii];
          SCVector3 ai(r(i));
          double Zi = charge(i);

          for (int jj=0; jj < ii; jj++) {
              int j = q_atoms_[jj];
              SCVector3 aj(r(j));
              e += Zi * charge(j) / ai.dist(aj);
            }
        }
    }

  return e;
}

void
Molecule::nuclear_repulsion_1der(int center, double xyz[3])
{
  int i,j,k;
  double rd[3],r2;
  double factor;

  xyz[0] = 0.0;
  xyz[1] = 0.0;
  xyz[2] = 0.0;

  SCVector3 r_center(r(center));
  double Z_center = charge(center);
  bool center_is_Q = (atom_symbol(center) == "Q");

  // this handles center = Q or non_Q and atom = non_Q
  for (int ii=0; ii < non_q_atoms_.size(); ii++) {
    int i = non_q_atoms_[ii];
    if (i == center) continue;
    SCVector3 r_i(r(i));

    r2 = 0.0;
    for (k=0; k < 3; k++) {
      rd[k] = r_center[k] - r_i[k];
      r2 += rd[k]*rd[k];
    }
    factor = - Z_center * charge(i) * pow(r2,-1.5);
    for (k=0; k<3; k++) {
      xyz[k] += factor * rd[k];
    }
  }

  // this handles center = Q or non_Q and atom = Q
  for (int ii=0; ii < q_atoms_.size(); ii++) {
    int i = q_atoms_[ii];
    if (i == center || (!include_qq_ && center_is_Q)) continue;
    SCVector3 r_i(r(i));

    r2 = 0.0;
    for (k=0; k < 3; k++) {
      rd[k] = r_center[k] - r_i[k];
      r2 += rd[k]*rd[k];
    }
    factor = - Z_center * charge(i) * pow(r2,-1.5);
    for (k=0; k<3; k++) {
      xyz[k] += factor * rd[k];
    }
  }
}

void
Molecule::nuclear_charge_efield(const double *charges,
                                const double *position, double *efield)
{
  double tmp;
  double rd[3];

  for (int i=0; i<3; i++) efield[i] = 0.0;

  if (include_q_) {
    for (int i=0; i<natoms_; i++) {
      SCVector3 a(r(i));
      tmp = 0.0;
      for (int j=0; j<3; j++) {
        rd[j] = position[j] - a[j];
        tmp += rd[j]*rd[j];
      }
      tmp = charges[i]/(tmp*sqrt(tmp));
      for (int j=0; j<3; j++) {
        efield[j] +=  rd[j] * tmp;
      }
    }
  }
  else {
    for (int ii=0; ii<non_q_atoms_.size(); ii++) {
      int i = non_q_atoms_[ii];
      SCVector3 a(r(i));
      tmp = 0.0;
      for (int j=0; j<3; j++) {
        rd[j] = position[j] - a[j];
        tmp += rd[j]*rd[j];
      }
      tmp = charges[i]/(tmp*sqrt(tmp));
      for (int j=0; j<3; j++) {
        efield[j] +=  rd[j] * tmp;
      }
    }
  }
}

void
Molecule::nuclear_efield(const double *position, double *efield)
{
  double tmp;
  double rd[3];

  for (int i=0; i<3; i++) efield[i] = 0.0;

  if (include_q_) {
    for (int i=0; i<natoms_; i++) {
      SCVector3 a(r(i));
      tmp = 0.0;
      for (int j=0; j<3; j++) {
        rd[j] = position[j] - a[j];
        tmp += rd[j]*rd[j];
      }
      tmp = charge(i)/(tmp*sqrt(tmp));
      for (int j=0; j<3; j++) {
        efield[j] +=  rd[j] * tmp;
      }
    }
  }
  else {
    for (int ii=0; ii<non_q_atoms_.size(); ii++) {
      int i = non_q_atoms_[ii];
      SCVector3 a(r(i));
      tmp = 0.0;
      for (int j=0; j<3; j++) {
        rd[j] = position[j] - a[j];
        tmp += rd[j]*rd[j];
      }
      tmp = charge(i)/(tmp*sqrt(tmp));
      for (int j=0; j<3; j++) {
        efield[j] +=  rd[j] * tmp;
      }
    }
  }
}

int
Molecule::atom_at_position(double *v, double tol) const
{
  SCVector3 p(v);
  for (int i=0; i < natom(); i++) {
      SCVector3 ai(r(i));
      if (p.dist(ai) < tol) return i;
    }
  return -1;
}

void
Molecule::symmetrize(const Ref<PointGroup> &pg, double tol)
{
  pg_ = new PointGroup(pg);

  // translate to the origin of the symmetry frame
  double r[3];
  for (int i=0; i<3; i++) {
      r[i] = -pg_->origin()[i];
      pg_->origin()[i] = 0;
    }
  translate(r);

  symmetrize(tol);
}

// We are given a molecule which may or may not have just the symmetry
// distinct atoms in it.  We have to go through the existing set of atoms,
// perform each symmetry operation in the point group on each of them, and
// then add the new atom if it isn't in the list already

void
Molecule::symmetrize(double tol)
{
  // if molecule is c1, don't do anything
  if (!strcmp(this->point_group()->symbol(),"c1")) {
    init_symmetry_info();
    return;
    }

  clear_symmetry_info();

  Molecule *newmol = new Molecule(*this);

  CharacterTable ct = this->point_group()->char_table();

  SCVector3 np;
  SymmetryOperation so;

  for (int i=0; i < natom(); i++) {
    SCVector3 ac(r(i));

    for (int g=0; g < ct.order(); g++) {
      so = ct.symm_operation(g);
      for (int ii=0; ii < 3; ii++) {
        np[ii]=0;
        for (int jj=0; jj < 3; jj++) np[ii] += so(ii,jj) * ac[jj];
      }

      int atom = newmol->atom_at_position(np.data(), tol);
      if (atom < 0) {
        const char* c_lbl = label(i);
        const std::string lbl = (c_lbl ? c_lbl : "");
        newmol->add_atom(atoms_[i].Z(),np[0],np[1],np[2],lbl);
      }
      else {
        if (Z(i) != newmol->Z(atom)
            || fabs(mass(i)-newmol->mass(atom))>1.0e-10) {
            throw ToleranceExceeded("symmetrize: atom mismatch",
                                    __FILE__, __LINE__,
                                    1.0e-10, fabs(mass(i)-newmol->mass(atom)),
                                    class_desc());
        }
      }
    }
  }

  Ref<Units> saved_units = geometry_units_;
  *this = *newmol;
  geometry_units_ = saved_units;
  delete newmol;

  init_symmetry_info();
}

void
Molecule::translate(const double *r)
{
  for (int i=0; i < natom(); i++) {
    atoms_[i].xyz(0) += r[0];
    atoms_[i].xyz(1) += r[1];
    atoms_[i].xyz(2) += r[2];
  }
  for(int xyz=0; xyz<3; ++xyz) ref_origin_[xyz] += r[xyz];
}

// move the molecule to the center of mass
void
Molecule::move_to_com()
{
  SCVector3 com = -center_of_mass();
  translate(com.data());
}

// find the 3 principal coordinate axes, and rotate the molecule to be
// aligned along them.  also rotate the symmetry frame contained in point_group
void
Molecule::transform_to_principal_axes(int trans_frame)
{
  // mol_move_to_com(mol);

  double *inert[3], inert_dat[9], *evecs[3], evecs_dat[9];
  double evals[3];

  int i,j,k;
  for (i=0; i < 3; i++) {
    inert[i] = &inert_dat[i*3];
    evecs[i] = &evecs_dat[i*3];
  }
  memset(inert_dat,'\0',sizeof(double)*9);
  memset(evecs_dat,'\0',sizeof(double)*9);

  for (i=0; i < natom(); i++) {
    SCVector3 ac(r(i));
    double m=mass(i);
    inert[0][0] += m * (ac[1]*ac[1] + ac[2]*ac[2]);
    inert[1][0] -= m * ac[0]*ac[1];
    inert[1][1] += m * (ac[0]*ac[0] + ac[2]*ac[2]);
    inert[2][0] -= m * ac[0]*ac[2];
    inert[2][1] -= m * ac[1]*ac[2];
    inert[2][2] += m * (ac[0]*ac[0] + ac[1]*ac[1]);
  }

  inert[0][1] = inert[1][0];
  inert[0][2] = inert[2][0];
  inert[1][2] = inert[2][1];

 // cleanup inert
  for (i=0; i < 3; i++) {
    for (int j=0; j <= i; j++) {
      if (fabs(inert[i][j]) < 1.0e-5) {
        inert[i][j]=inert[j][i]=0.0;
      }
    }
  }

  cmat_diag(inert, evals, evecs, 3, 1, 1e-14);

 // cleanup evecs
  for (i=0; i < 3; i++) {
    for (int j=0; j < 3; j++) {
      if (fabs(evecs[i][j]) < 1.0e-5) {
        evecs[i][j]=0.0;
      }
    }
  }

  for (i=0; i<natom(); i++) {
      double a[3];
      a[0] = r(i,0); a[1] = r(i,1); a[2] = r(i,2);
      for (j=0; j<3; j++) {
          double e = 0.0;
          for (k=0; k<3; k++) {
              e += a[k] * evecs[k][j];
            }
          atoms_[i].xyz(j) = e;
        }
    }

  if (!trans_frame) return;

  SymmetryOperation tso=point_group()->symm_frame();

  for (i=0; i < 3; i++) {
    for (int j=0; j < 3; j++) {
      double t=0;
      for (int k=0; k < 3; k++) t += tso[k][j]*evecs[k][i];
      pg_->symm_frame()[i][j] = t;
    }
  }
}

void
Molecule::transform_to_symmetry_frame()
{
  int i,j,k;
  double t[3][3];

  SymmetryOperation tso=point_group()->symm_frame();

  for (i=0; i<3; i++) {
      for (j=0; j<3; j++) {
          t[i][j] = tso[i][j];
        }
    }

  for (i=0; i<natom(); i++) {
      double a[3];
      a[0] = r(i,0); a[1] = r(i,1); a[2] = r(i,2);
      for (j=0; j<3; j++) {
          double e = 0.0;
          for (k=0; k<3; k++) {
              e += a[k] * t[k][j];
            }
          atoms_[i].xyz(j) = e;
        }
    }

  for (i=0; i<3; i++) {
      for (j=0; j<3; j++) {
          double e=0;
          for (k=0; k<3; k++) e += tso[k][j]*t[k][i];
          pg_->symm_frame()[i][j] = e;
        }
    }
}

// given a molecule, make sure that equivalent centers have coordinates
// that really map into each other

void
Molecule::cleanup_molecule(double tol)
{
  // if symmetry is c1, do nothing else
  if (!strcmp(point_group()->symbol(),"c1")) return;

  int i;
  SCVector3 up,np,ap;
  SymmetryOperation so;
  CharacterTable ct = point_group()->char_table();

  // first clean up the unique atoms by replacing each coordinate with the
  // average of coordinates obtained by applying all symmetry operations to
  // the original atom, iff the new atom ends up near the original atom
  for (i=0; i < nunique(); i++) {
      // up will store the original coordinates of unique atom i
      up = r(unique(i));
      // ap will hold the average coordinate (times the number of coordinates)
      // initialize it to the E result
      ap = up;
      int ncoor = 1;
      // loop through all sym ops except E
      for (int g=1; g < ct.order(); g++) {
          so = ct.symm_operation(g);
          for (int ii=0; ii < 3; ii++) {
              np[ii]=0;
              for (int jj=0; jj < 3; jj++) np[ii] += so(ii,jj) * up[jj];
            }
          if (np.dist(up) < 0.1) {
              for (int jj=0; jj < 3; jj++) ap[jj] += np[jj];
              ncoor++;
            }
        }
      // replace the unique coordinate with the average coordinate
      atoms_[unique(i)].xyz(0) = ap[0] / ncoor;
      atoms_[unique(i)].xyz(1) = ap[1] / ncoor;
      atoms_[unique(i)].xyz(2) = ap[2] / ncoor;
    }

  // find the atoms equivalent to each unique atom and eliminate
  // numerical errors that may be in the equivalent atom's coordinates

  // loop through unique atoms
  for (i=0; i < nunique(); i++) {
      // up will store the coordinates of unique atom i
      up = r(unique(i));

      // loop through all sym ops except E
      for (int g=1; g < ct.order(); g++) {
          so = ct.symm_operation(g);
          for (int ii=0; ii < 3; ii++) {
              np[ii]=0;
              for (int jj=0; jj < 3; jj++) np[ii] += so(ii,jj) * up[jj];
            }

          // loop through equivalent atoms
          int found = 0;
          for (int j=0; j < natom(); j++) {
              // see if j is generated from i
              if (np.dist(SCVector3(r(j))) < tol) {
                  atoms_[j].xyz(0) = np[0];
                  atoms_[j].xyz(1) = np[1];
                  atoms_[j].xyz(2) = np[2];
                  found = 1;
                }
            }
          if (!found) {
              SCException ex("cleanup: couldn't find atom",
                             __FILE__, __LINE__, class_desc());
              try {
                  ex.elaborate()
                      << "couldn't find atom at " << np << endl
                      << "transforming uniq atom " << i << " at " << up << endl
                      << "with symmetry op " << g << ":" << endl;
                  so.print(ex.elaborate());
                }
              catch (...) {}
              throw ex;
            }
        }
    }

}

///////////////////////////////////////////////////////////////////
// Compute the principal axes and the principal moments of inertia
///////////////////////////////////////////////////////////////////

void
Molecule::principal_moments_of_inertia(double *evals, double **evecs) const
{

  // The principal moments of inertia are computed in amu*angstrom^2
  // evals: principal moments of inertia
  // evecs: principal axes (optional argument)

  Ref<Units> units = new Units("angstroms * angstroms");
  double au_to_angs = units->from_atomic_units();

  double *inert[3];  // inertia tensor

  int i, j;
  int delete_evecs = 0;

  // (allocate and) initialize evecs, evals, and inert
  if (!evecs) {
    evecs = new double*[3];
    for (i=0; i<3; i++) evecs[i] = new double[3];
    delete_evecs = 1;
    }
  for (i=0; i<3; i++) {
    inert[i] = new double[3];
    memset(inert[i],'\0',sizeof(double)*3);
    memset(evecs[i],'\0',sizeof(double)*3);
    }
  memset(evals,'\0',sizeof(double)*3);

  SCVector3 com = center_of_mass();

  // compute inertia tensor
  SCVector3 ac;
  for (i=0; i<natom(); i++) {
    ac = r(i);
    // compute moments of inertia wrt center of mass
    for (j=0; j<3; j++) ac(j) -= com(j);
    double m=au_to_angs*mass(i);
    inert[0][0] += m * (ac[1]*ac[1] + ac[2]*ac[2]);
    inert[1][0] -= m * ac[0]*ac[1];
    inert[1][1] += m * (ac[0]*ac[0] + ac[2]*ac[2]);
    inert[2][0] -= m * ac[0]*ac[2];
    inert[2][1] -= m * ac[1]*ac[2];
    inert[2][2] += m * (ac[0]*ac[0] + ac[1]*ac[1]);
    }
  inert[0][1] = inert[1][0];
  inert[0][2] = inert[2][0];
  inert[1][2] = inert[2][1];

  cmat_diag(inert, evals, evecs, 3, 1, 1e-14);

  if (delete_evecs) {
    for (i=0; i<3; i++) delete[] evecs[i];
    delete[] evecs;
    }
  for (i=0; i<3; i++) {
    delete[] inert[i];
    }
}

int
Molecule::n_core_electrons()
{
  int i,n=0;
  for (i=0; i<natom(); i++) {
      if (charge(i) == 0.0) continue;
      int z = atoms_[i].Z();
      if (z > 2) n += 2;
      if (z > 10) n += 8;
      if (z > 18) n += 8;
      if (z > 30) n += 10;
      if (z > 36) n += 8;
      if (z > 48) n += 10;
      if (z > 54) n += 8;
      if (z > 72) {
          throw LimitExceeded<int>("n_core_electrons: atomic number too large",
                                   __FILE__, __LINE__, 72, z, class_desc());
        }
    }
  return n;
}

int
Molecule::max_z()
{
  int i, maxz=0;
  for (i=0; i<natom(); i++) {
<<<<<<< local
      int z = atoms_[i].Z();
=======
      int z = Z_[i];
>>>>>>> other
      if (z != q_Z_ && z > maxz)
<<<<<<< local
          maxz = z;
  }
=======
        maxz = z;
    }
>>>>>>> other
  return maxz;
}

void
Molecule::read_pdb(const char *filename)
{
  clear();
  ifstream in(filename);
  Ref<Units> units = new Units("angstrom");
  while (in.good()) {
      const int max_line = 80;
      char line[max_line];
      in.getline(line,max_line);
      char *endofline = (char*) memchr(line, 0, max_line);
      if (endofline) memset(endofline, ' ', &line[max_line-1] - endofline);
      if (!in.good()) break;
      if (strncmp(line,"ATOM  ",6) == 0
          ||strncmp(line,"HETATM",6) == 0) {

          char element[3];
          strncpy(element,&line[76],2); element[2] = '\0';
          char name[5];
          strncpy(name,&line[12],4); name[4] = '\0';
          char resSeq[5];
          strncpy(resSeq,&line[23],4); resSeq[4] = '\0';

          if (element[0]==' '&&element[1]==' ') {
              // no element was given so get the element from the atom name
              if (name[0]!=' '&&name[3]!=' ') {
                  // some of the atom label may have been
                  // pushed over into the element fields
                  // so check the residue
                  char resName[4];
                  strncpy(resName,&line[17],3); resName[3] = '\0';
                  if (strncmp(line,"ATOM  ",6)==0&&(0
                      ||strcmp(resName,"ALA")==0||strcmp(resName,"A  ")==0
                      ||strcmp(resName,"ARG")==0||strcmp(resName,"R  ")==0
                      ||strcmp(resName,"ASN")==0||strcmp(resName,"N  ")==0
                      ||strcmp(resName,"ASP")==0||strcmp(resName,"D  ")==0
                      ||strcmp(resName,"ASX")==0||strcmp(resName,"B  ")==0
                      ||strcmp(resName,"CYS")==0||strcmp(resName,"C  ")==0
                      ||strcmp(resName,"GLN")==0||strcmp(resName,"Q  ")==0
                      ||strcmp(resName,"GLU")==0||strcmp(resName,"E  ")==0
                      ||strcmp(resName,"GLX")==0||strcmp(resName,"Z  ")==0
                      ||strcmp(resName,"GLY")==0||strcmp(resName,"G  ")==0
                      ||strcmp(resName,"HIS")==0||strcmp(resName,"H  ")==0
                      ||strcmp(resName,"ILE")==0||strcmp(resName,"I  ")==0
                      ||strcmp(resName,"LEU")==0||strcmp(resName,"L  ")==0
                      ||strcmp(resName,"LYS")==0||strcmp(resName,"K  ")==0
                      ||strcmp(resName,"MET")==0||strcmp(resName,"M  ")==0
                      ||strcmp(resName,"PHE")==0||strcmp(resName,"F  ")==0
                      ||strcmp(resName,"PRO")==0||strcmp(resName,"P  ")==0
                      ||strcmp(resName,"SER")==0||strcmp(resName,"S  ")==0
                      ||strcmp(resName,"THR")==0||strcmp(resName,"T  ")==0
                      ||strcmp(resName,"TRP")==0||strcmp(resName,"W  ")==0
                      ||strcmp(resName,"TYR")==0||strcmp(resName,"Y  ")==0
                      ||strcmp(resName,"VAL")==0||strcmp(resName,"V  ")==0
                      ||strcmp(resName,"A  ")==0
                      ||strcmp(resName,"+A ")==0
                      ||strcmp(resName,"C  ")==0
                      ||strcmp(resName,"+C ")==0
                      ||strcmp(resName,"G  ")==0
                      ||strcmp(resName,"+G ")==0
                      ||strcmp(resName,"I  ")==0
                      ||strcmp(resName,"+I ")==0
                      ||strcmp(resName,"T  ")==0
                      ||strcmp(resName,"+T ")==0
                      ||strcmp(resName,"U  ")==0
                      ||strcmp(resName,"+U ")==0
                      )) {
                      // there no two letter elements for these cases
                      element[0] = name[0];
                      element[1] = '\0';
                    }
                }
              else {
                  strncpy(element,name,2); element[2] = '\0';
                }
            }
          if (element[0] == ' ') {
              element[0] = element[1];
              element[1] = '\0';
            }

          int Z = atominfo_->string_to_Z(element);

          int have_fragment = 0;
          int fragment;
          for(int c=0; c<5; ++c) {
            if (resSeq[c] == ' ')
              continue;
            else {
              have_fragment = 1;
              fragment = atoi(resSeq+c);
            }
          }

          char field[9];
          strncpy(field,&line[30],8); field[8] = '\0';
          double x = atof(field);
          strncpy(field,&line[38],8); field[8] = '\0';
          double y = atof(field);
          strncpy(field,&line[46],8); field[8] = '\0';
          double z = atof(field);
          add_atom(Z,
                   x*units->to_atomic_units(),
                   y*units->to_atomic_units(),
                   z*units->to_atomic_units(),
                   "", 0.0, 0, 0.0, have_fragment, fragment);
        }
      else {
        // skip to next record
        }
    }
}

void
Molecule::print_pdb(ostream& os, char *title) const
{
  Ref<Units> u = new Units("angstrom");
  double bohr = u->from_atomic_units();

  if (title)
      os << scprintf("%-10s%-60s\n","COMPND",title);
  else
      os << scprintf("%-10s%-60s\n","COMPND","Title");

  if (title)
      os << scprintf("REMARK   %s\n", title);

  int i;
  for (i=0; i < natom(); i++) {
    char symb[4];
    std::string symbol(atom_symbol(i));
    sprintf(symb,"%s1",symbol.c_str());

    // if fragment representation exceeds 4 characters reserved for resSeq, set it to -1
    int frag = fragment(i);
    {
      std::ostringstream oss;
      oss << frag;
      if (oss.str().size() > 4)
        frag = -1;
    }

    os << scprintf(
        "HETATM%5d  %-3s UNK  %4d    %8.3f%8.3f%8.3f  0.00  0.00   0\n",
        i+1, symb, frag, r(i,0)*bohr, r(i,1)*bohr, r(i,2)*bohr);
  }

  for (i=0; i < natom(); i++) {
    double at_rad_i = atominfo_->atomic_radius(atoms_[i].Z());
    SCVector3 ai(r(i));

    os << scprintf("CONECT%5d",i+1);

    for (int j=0; j < natom(); j++) {

      if (j==i) continue;

      double at_rad_j = atominfo_->atomic_radius(atoms_[j].Z());
      SCVector3 aj(r(j));

      if (ai.dist(aj) < 1.1*(at_rad_i+at_rad_j))
          os << scprintf("%5d",j+1);
    }

    os << endl;
  }

  os << "END" << endl;
  os.flush();
}

double
Molecule::mass(int atom) const
{
  return atoms_[atom].mass() ? atoms_[atom].mass() :
                               atominfo_->mass(atoms_[atom].Z());
}

const char *
Molecule::label(int atom) const
{
  return !atoms_[atom].label().empty() ? atoms_[atom].label().c_str() : 0;
}

std::string
Molecule::atom_name(int iatom) const
{
  return atominfo_->name(atoms_[iatom].Z());
}

std::string
Molecule::atom_symbol(int iatom) const
{
  return atominfo_->symbol(atoms_[iatom].Z());
}

bool
Molecule::any_atom_has_charge() const {
   for(std::size_t i = 0; i < natoms_; ++i){
       if(atoms_[i].have_charge()) return true;
   }
   return false;
}

bool
Molecule::any_atom_has_fragment() const {
   for(std::size_t i = 0; i < natoms_; ++i){
       if(atoms_[i].have_fragment()) return true;
   }
   return false;
}

bool
Molecule::any_atom_has_label() const {
   for(std::size_t i = 0; i < natoms_; ++i){
       if(!atoms_[i].label().empty()) return true;
   }
   return false;
}


/////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: c++
// c-file-style: "CLJ"
// End:
