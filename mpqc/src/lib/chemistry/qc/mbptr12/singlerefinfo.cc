//
// singlerefinfo.cc
//
// Copyright (C) 2005 Edward Valeev
//
// Author: Edward Valeev <evaleev@vt.edu>
// Maintainer: EV
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

#include <util/class/scexception.h>
#include <chemistry/qc/scf/scf.h>
#include <chemistry/qc/scf/hsosscf.h>
#include <chemistry/qc/basis/petite.h>
#include <chemistry/qc/mbptr12/singlerefinfo.h>

using namespace sc;

namespace {
  const unsigned int ClassVersion = 1;
};

static ClassDesc R12IntEvalInfo_cd(
  typeid(SingleRefInfo),"SingleRefInfo",ClassVersion,"virtual public SavableState",
  0, 0, create<SingleRefInfo>);

SingleRefInfo::SingleRefInfo(const Ref<SCF>& ref, unsigned int nfzc, unsigned int nfzv,
			     bool delayed_initialization) :
  ref_(ref), nfzc_(nfzc), nfzv_(nfzv), initialized_(false)
{
  if (!delayed_initialization)
    initialize();
}

SingleRefInfo::SingleRefInfo(StateIn& si) :
  SavableState(si)
{
  int initialized; si.get(initialized); initialized_ = (bool) initialized;
  ref_ << SavableState::restore_state(si);
  si.get(nfzc_);
  si.get(nfzv_);

  orbs_sb_ << SavableState::restore_state(si);
  orbs_ << SavableState::restore_state(si);
  docc_sb_ << SavableState::restore_state(si);
  docc_ << SavableState::restore_state(si);
  docc_act_ << SavableState::restore_state(si);
  socc_sb_ << SavableState::restore_state(si);
  socc_ << SavableState::restore_state(si);
  uocc_sb_ << SavableState::restore_state(si);
  uocc_ << SavableState::restore_state(si);
  uocc_act_ << SavableState::restore_state(si);
  for(int spin=0; spin<2; spin++) {
    spinspaces_[spin].orbs_sb_ << SavableState::restore_state(si);
    spinspaces_[spin].orbs_ << SavableState::restore_state(si);
    spinspaces_[spin].occ_sb_ << SavableState::restore_state(si);
    spinspaces_[spin].occ_ << SavableState::restore_state(si);
    spinspaces_[spin].occ_act_ << SavableState::restore_state(si);
    spinspaces_[spin].uocc_sb_ << SavableState::restore_state(si);
    spinspaces_[spin].uocc_ << SavableState::restore_state(si);
    spinspaces_[spin].uocc_act_ << SavableState::restore_state(si);
  }
}

SingleRefInfo::~SingleRefInfo()
{
}

void
SingleRefInfo::save_data_state(StateOut& so)
{
  so.put((int)initialized_);
  SavableState::save_state(ref_.pointer(),so);
  so.put(nfzc_);
  so.put(nfzv_);

  SavableState::save_state(orbs_sb_.pointer(),so);
  SavableState::save_state(orbs_.pointer(),so);
  SavableState::save_state(docc_sb_.pointer(),so);
  SavableState::save_state(docc_.pointer(),so);
  SavableState::save_state(docc_act_.pointer(),so);
  SavableState::save_state(socc_sb_.pointer(),so);
  SavableState::save_state(socc_.pointer(),so);
  SavableState::save_state(uocc_sb_.pointer(),so);
  SavableState::save_state(uocc_.pointer(),so);
  SavableState::save_state(uocc_act_.pointer(),so);
  for(int spin=0; spin<2; spin++) {
    SavableState::save_state(spinspaces_[spin].orbs_sb_.pointer(),so);
    SavableState::save_state(spinspaces_[spin].orbs_.pointer(),so);
    SavableState::save_state(spinspaces_[spin].occ_sb_.pointer(),so);
    SavableState::save_state(spinspaces_[spin].occ_.pointer(),so);
    SavableState::save_state(spinspaces_[spin].occ_act_.pointer(),so);
    SavableState::save_state(spinspaces_[spin].uocc_sb_.pointer(),so);
    SavableState::save_state(spinspaces_[spin].uocc_.pointer(),so);
    SavableState::save_state(spinspaces_[spin].uocc_act_.pointer(),so);
  }
}

void
SingleRefInfo::initialize()
{
  if (!initialized_) {
    if (!spin_unrestricted())
      init_spinindependent_spaces();
    init_spinspecific_spaces();
    initialized_ = true;
  }
}

const Ref<SCF>&
SingleRefInfo::ref() const
{
  return ref_;
}

bool
SingleRefInfo::spin_polarized() const
{
  // false for closed-shell, true for any open-shell (will use semicanonical orbitals for HSOSSCF).
  return ref()->spin_polarized();
}

bool
SingleRefInfo::spin_unrestricted() const
{
  // false for CLSCF, HSOSSCF, true for USCF
  return ref()->spin_unrestricted();
}

unsigned int
SingleRefInfo::nfzc() const
{
  return nfzc_;
}

unsigned int
SingleRefInfo::nfzv() const
{
  return nfzv_;
}

void
SingleRefInfo::init_spinspecific_spaces()
{
  const Ref<GaussianBasisSet> bs = ref()->basis();
  const Ref<Integral>& integral = ref()->integral();
  using std::vector;
  vector<double> aocc, bocc;
  const int nmo = ref()->alpha_eigenvectors().coldim().n();
  for(int mo=0; mo<nmo; mo++) {
    aocc.push_back(ref()->alpha_occupation(mo));
    bocc.push_back(ref()->beta_occupation(mo));
  }
  Ref<PetiteList> plist = ref()->integral()->petite_list();
  
  if (spin_polarized()) {
    RefSCMatrix alpha_evecs, beta_evecs;
    RefDiagSCMatrix alpha_evals, beta_evals;
    // alpha and beta orbitals are available for UHF
    if (ref()->spin_unrestricted()) {
      alpha_evecs = ref()->alpha_eigenvectors();
      beta_evecs = ref()->beta_eigenvectors();
      alpha_evals = ref()->alpha_eigenvalues();
      beta_evals = ref()->beta_eigenvalues();
    }
    // use semicanonical orbitals for ROHF
    else {
      Ref<HSOSSCF> hsosscf = dynamic_cast<HSOSSCF*>(ref().pointer());
      if (hsosscf.null())
        throw ProgrammingError("SingleRefInfo::init_spinspecific_spaces() -- spin-specific spaces not available for this reference function", __FILE__, __LINE__);
      alpha_evecs = hsosscf->alpha_semicanonical_eigenvectors();
      beta_evecs = hsosscf->beta_semicanonical_eigenvectors();
      alpha_evals = hsosscf->alpha_semicanonical_eigenvalues();
      beta_evals = hsosscf->beta_semicanonical_eigenvalues();
    }
#if 0
    alpha_evals.print("Alpha orbital energies");
    alpha_evecs.print("Alpha orbitals");
    beta_evals.print("Beta orbital energies");
    beta_evecs.print("Beta orbitals");
#endif
    spinspaces_[0].init(Alpha, bs, integral, alpha_evals, plist->evecs_to_AO_basis(alpha_evecs), aocc, nfzc(), nfzv());
    spinspaces_[1].init(Beta, bs, integral, beta_evals, plist->evecs_to_AO_basis(beta_evecs), bocc, nfzc(), nfzv());
  }
  else {
    for(int s=0; s<NSpinCases1; s++) {
      spinspaces_[s].orbs_sb_ = orbs_sb_;
      spinspaces_[s].orbs_ = orbs_;
      spinspaces_[s].occ_sb_ = docc_sb();
      spinspaces_[s].occ_ = docc();
      spinspaces_[s].occ_act_ = docc_act();
      spinspaces_[s].uocc_sb_ = uocc_sb_;
      spinspaces_[s].uocc_ = uocc_;
      spinspaces_[s].uocc_act_ = uocc_act_;
    }
  }
}

void
SingleRefInfo::init_spinindependent_spaces()
{
  const Ref<GaussianBasisSet> bs = ref()->basis();
  const RefSCMatrix evecs_so = ref()->eigenvectors();
  const RefDiagSCMatrix evals = ref()->eigenvalues();
  const Ref<Integral>& integral =  ref()->integral();
  Ref<PetiteList> plist = integral->petite_list();
  RefSCMatrix evecs_ao = plist->evecs_to_AO_basis(evecs_so);

  const int nmo = evecs_ao.coldim().n();
  std::vector<bool> docc_mask(nmo, false);
  std::vector<bool> socc_mask(nmo, false);
  std::vector<bool> uocc_mask(nmo, false);
  for (int i=0; i<nmo; i++) {
    if (ref()->occupation(i) == 2.0)
      docc_mask[i] = true;
    else if (ref()->occupation(i) == 1.0)
      socc_mask[i] = true;
    else
      uocc_mask[i] = true;
  }

  orbs_sb_ = new OrbitalSpace("p(sym)","symmetry-blocked MOs", evecs_ao, bs, integral, evals, 0, 0, OrbitalSpace::symmetry);
  orbs_ = new OrbitalSpace("p","energy-ordered MOs", evecs_ao, bs, integral, evals, 0, 0);
  // since occupations may not match orbital energies (i.e. some occupied orbitals may have higher energies than the unoccupied)
  // construct spaces using masks
  docc_sb_ = new MaskedOrbitalSpace("m(sym)","doubly-occupied symmetry-blocked MOs", orbs_sb_, docc_mask);
  docc_ = new OrbitalSpace("m","doubly-occupied energy-ordered MOs", docc_sb_->coefs(), docc_sb_->basis(),
                           docc_sb_->integral(), docc_sb_->evals(), 0, 0);
  docc_act_ = new OrbitalSpace("i","active doubly-occupied energy-ordered MOs", docc_sb_->coefs(), docc_sb_->basis(),
                               docc_sb_->integral(), docc_sb_->evals(), nfzc(), 0);
  socc_sb_ = new MaskedOrbitalSpace("x(sym)","singly-occupied symmetry-blocked MOs", orbs_sb_, socc_mask);
  socc_ = new OrbitalSpace("x","singly-occupied energy-ordered MOs", socc_sb_->coefs(), socc_sb_->basis(),
                           socc_sb_->integral(), socc_sb_->evals(), 0, 0);
  uocc_sb_ = new MaskedOrbitalSpace("e(sym)","unoccupied symmetry-blocked MOs", orbs_sb_, uocc_mask);
  uocc_ = new OrbitalSpace("e","unoccupied energy-ordered MOs", uocc_sb_->coefs(), uocc_sb_->basis(),
                           uocc_sb_->integral(), uocc_sb_->evals(), 0, 0);
  uocc_act_ = new OrbitalSpace("a","active unoccupied energy-ordered MOs", uocc_sb_->coefs(), uocc_sb_->basis(),
                               uocc_sb_->integral(), uocc_sb_->evals(), 0, nfzv());
}


const Ref<OrbitalSpace>&
SingleRefInfo::orbs_sb() const
{
  // can throw
  throw_if_spin_unrestricted();
  return orbs_sb_;
}

const Ref<OrbitalSpace>&
SingleRefInfo::orbs() const
{
  // can throw
  throw_if_spin_unrestricted();
  return orbs_;
}

const Ref<OrbitalSpace>&
SingleRefInfo::docc_sb() const
{
  // can throw
  throw_if_spin_polarized();
  return docc_sb_;
}

const Ref<OrbitalSpace>&
SingleRefInfo::docc() const
{
  // can throw
  throw_if_spin_polarized();
  return docc_;
}

const Ref<OrbitalSpace>&
SingleRefInfo::docc_act() const
{
  // can throw
  throw_if_spin_polarized();
  return docc_act_;
}
const Ref<OrbitalSpace>&
SingleRefInfo::socc() const
{
  // can throw
  throw_if_spin_polarized();
  return socc_;
}

const Ref<OrbitalSpace>&
SingleRefInfo::uocc_sb() const
{
  // can throw
  throw_if_spin_polarized();
  return uocc_sb_;
}

const Ref<OrbitalSpace>&
SingleRefInfo::uocc() const
{
  // can throw
  throw_if_spin_polarized();
  return uocc_;
}

const Ref<OrbitalSpace>&
SingleRefInfo::uocc_act() const
{
  // can throw
  throw_if_spin_polarized();
  return uocc_act_;
}

const Ref<OrbitalSpace>&
SingleRefInfo::orbs_sb(SpinCase1 s) const
{
  return spinspaces_[s].orbs_sb_;
}

const Ref<OrbitalSpace>&
SingleRefInfo::orbs(SpinCase1 s) const
{
  return spinspaces_[s].orbs_;
}

const Ref<OrbitalSpace>&
SingleRefInfo::occ_sb(SpinCase1 s) const
{
  return spinspaces_[s].occ_sb_;
}

const Ref<OrbitalSpace>&
SingleRefInfo::occ(SpinCase1 s) const
{
  return spinspaces_[s].occ_;
}

const Ref<OrbitalSpace>&
SingleRefInfo::occ_act(SpinCase1 s) const
{
  return spinspaces_[s].occ_act_;
}

const Ref<OrbitalSpace>&
SingleRefInfo::uocc_sb(SpinCase1 s) const
{
  return spinspaces_[s].uocc_sb_;
}

const Ref<OrbitalSpace>&
SingleRefInfo::uocc(SpinCase1 s) const
{
  return spinspaces_[s].uocc_;
}

const Ref<OrbitalSpace>&
SingleRefInfo::uocc_act(SpinCase1 s) const
{
  return spinspaces_[s].uocc_act_;
}

void
SingleRefInfo::throw_if_spin_polarized() const
{
  if (spin_polarized())
    throw ProgrammingError("SingleRefInfo -- spin-independent space is requested but the reference function is spin-polarized",
        __FILE__,__LINE__);
}

void
SingleRefInfo::throw_if_spin_unrestricted() const
{
  if (spin_unrestricted())
    throw ProgrammingError("SingleRefInfo -- spin-independent subspace is requested but the reference function is spin-unrestricted",
        __FILE__,__LINE__);
}

/////////////

void
SingleRefInfo::SpinSpaces::init(
        SpinCase1 spin,
        const Ref<GaussianBasisSet>& bs,
        const Ref<Integral>& integral,
        const RefDiagSCMatrix& evals,
        const RefSCMatrix& evecs,
        const std::vector<double>& occs,
        unsigned int nfzc,
        unsigned int nfzv)
{
  int nmo = occs.size();
  std::vector<bool> occ_mask(nmo, false);
  std::vector<bool> uocc_mask(nmo, false);
  for(int i=0; i<nmo; i++) {
    if (occs[i] == 1.0)
      occ_mask[i] = true;
    else
      uocc_mask[i] = true;
  }
  const std::string prefix(to_string(spin));
  using std::ostringstream;
  {
    ostringstream oss;
    oss << prefix << " symmetry-blocked MOs";
    std::string id = ParsedOrbitalSpaceKey::key(std::string("p(sym)"),spin);
    orbs_sb_ = new OrbitalSpace(id, oss.str(), evecs, bs, integral, evals, 0, 0, OrbitalSpace::symmetry);
  }
  {
    ostringstream oss;
    oss << prefix << " energy-ordered MOs";
    std::string id = ParsedOrbitalSpaceKey::key(std::string("p"),spin);
    orbs_ = new OrbitalSpace(id, oss.str(), evecs, bs, integral, evals, 0, 0);
  }
  {
    ostringstream oss;
    oss << prefix << " occupied symmetry-blocked MOs";
    std::string id = ParsedOrbitalSpaceKey::key(std::string("m(sym)"),spin);
    occ_sb_ = new MaskedOrbitalSpace(id, oss.str(), orbs_sb_, occ_mask);
  }
  {
    ostringstream oss;
    oss << prefix << " occupied MOs";
    std::string id = ParsedOrbitalSpaceKey::key(std::string("m"),spin);
    occ_ = new OrbitalSpace(id, oss.str(), occ_sb_->coefs(), occ_sb_->basis(),
                            occ_sb_->integral(), occ_sb_->evals(), 0, 0);
  }
  {
    ostringstream oss;
    oss << prefix << " active occupied MOs";
    std::string id = ParsedOrbitalSpaceKey::key(std::string("i"),spin);
    occ_act_ = new OrbitalSpace(id, oss.str(), occ_sb_->coefs(), occ_sb_->basis(),
                                occ_sb_->integral(), occ_sb_->evals(), nfzc, 0);
  }
  {
    ostringstream oss;
    oss << prefix << " unoccupied symmetry-blocked MOs";
    std::string id = ParsedOrbitalSpaceKey::key(std::string("e(sym)"),spin);
    uocc_sb_ = new MaskedOrbitalSpace(id, oss.str(), orbs_sb_, uocc_mask);
  }
  {
    ostringstream oss;
    oss << prefix << " unoccupied MOs";
    std::string id = ParsedOrbitalSpaceKey::key(std::string("e"),spin);
    uocc_ = new OrbitalSpace(id, oss.str(), uocc_sb_->coefs(), uocc_sb_->basis(),
                             uocc_sb_->integral(), uocc_sb_->evals(), 0, 0);
  }
  {
    ostringstream oss;
    oss << prefix << " active unoccupied MOs";
    std::string id = ParsedOrbitalSpaceKey::key(std::string("a"),spin);
    uocc_act_ = new OrbitalSpace(id, oss.str(),uocc_sb_->coefs(), uocc_sb_->basis(),
                                 uocc_sb_->integral(), uocc_sb_->evals(), 0, nfzv);
  }
}


