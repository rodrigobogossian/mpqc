//
// clhf.cc --- implementation of the closed shell Hartree-Fock SCF class
//
// Copyright (C) 1996 Limit Point Systems, Inc.
//
// Author: Edward Seidl <seidl@janed.com>
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

#ifdef __GNUC__
#pragma implementation
#endif

#include <math.h>

#include <util/misc/timer.h>
#include <util/misc/formio.h>
#include <util/state/stateio.h>

#include <chemistry/qc/basis/petite.h>

#include <chemistry/qc/scf/clhf.h>
#include <chemistry/qc/scf/lgbuild.h>
#include <chemistry/qc/scf/ltbgrad.h>
#include <chemistry/qc/scf/clhftmpl.h>

///////////////////////////////////////////////////////////////////////////
// CLHF

#define CLASSNAME CLHF
#define HAVE_STATEIN_CTOR
#define HAVE_KEYVAL_CTOR
#define PARENTS public CLSCF
#include <util/class/classi.h>
void *
CLHF::_castdown(const ClassDesc*cd)
{
  void* casts[1];
  casts[0] = CLSCF::_castdown(cd);
  return do_castdowns(casts,cd);
}

CLHF::CLHF(StateIn& s) :
  SavableState(s),
  CLSCF(s)
{
}

CLHF::CLHF(const RefKeyVal& keyval) :
  CLSCF(keyval)
{
}

CLHF::~CLHF()
{
}

void
CLHF::save_data_state(StateOut& s)
{
  CLSCF::save_data_state(s);
}

int
CLHF::value_implemented() const
{
  return 1;
}

int
CLHF::gradient_implemented() const
{
  return 1;
}

void
CLHF::print(ostream&o) const
{
  CLSCF::print(o);
}

//////////////////////////////////////////////////////////////////////////////

void
CLHF::init_vector()
{
  CLSCF::init_vector();
  
  int nthread = threadgrp_->nthread();
  int int_store = integral()->storage_unused()/nthread;
  
  // initialize the two electron integral classes
  tbis_ = new RefTwoBodyInt[nthread];
  for (int i=0; i < nthread; i++) {
    tbis_[i] = integral()->electron_repulsion();
    tbis_[i]->set_integral_storage(int_store);
  }

}

void
CLHF::done_vector()
{
  CLSCF::done_vector();
  
  for (int i=0; i < threadgrp_->nthread(); i++) tbis_[i] = 0;
  delete[] tbis_;
  tbis_ = 0;
}

void
CLHF::ao_fock()
{
  int i;
  int nthread = threadgrp_->nthread();

  RefPetiteList pl = integral()->petite_list(basis());
  
  // calculate G.  First transform cl_dens_diff_ to the AO basis, then
  // scale the off-diagonal elements by 2.0
  tim_enter("setup");
  RefSymmSCMatrix dd = cl_dens_diff_;
  cl_dens_diff_ = pl->to_AO_basis(dd);
  cl_dens_diff_->scale(2.0);
  cl_dens_diff_->scale_diagonal(0.5);
  tim_exit("setup");

  // now try to figure out the matrix specialization we're dealing with
  // if we're using Local matrices, then there's just one subblock, or
  // see if we can convert G and P to local matrices

  if (debug_>1) {
    cl_gmat_.print("cl_gmat before build");
    cl_dens_diff_.print("cl_dens_diff before build");
  }

  if (local_ || local_dens_) {
    // grab the data pointers from the G and P matrices
    double *gmat, *pmat;
    tim_enter("local data");
    RefSymmSCMatrix gtmp = get_local_data(cl_gmat_, gmat, SCF::Accum);
    RefSymmSCMatrix ptmp = get_local_data(cl_dens_diff_, pmat, SCF::Read);
    tim_exit("local data");

    tim_enter("init pmax");
    signed char * pmax = init_pmax(pmat);
    tim_exit("init pmax");
  
    tim_enter("ao_gmat");
    LocalGBuild<LocalCLHFContribution> **gblds =
      new LocalGBuild<LocalCLHFContribution>*[nthread];
    LocalCLHFContribution **conts = new LocalCLHFContribution*[nthread];
    
    double **gmats = new double*[nthread];
    gmats[0] = gmat;
    
    RefGaussianBasisSet bs = basis();
    int ntri = i_offset(bs->nbasis());

    for (i=0; i < nthread; i++) {
      if (i) {
        gmats[i] = new double[ntri];
        memset(gmats[i], 0, sizeof(double)*ntri);
      }
      conts[i] = new LocalCLHFContribution(gmats[i], pmat);
      gblds[i] = new LocalGBuild<LocalCLHFContribution>(*conts[i], tbis_[i],
        pl, bs, scf_grp_, pmax, desired_value_accuracy()/100.0, nthread, i
        );

      threadgrp_->add_thread(i, gblds[i]);
    }

    tim_enter("start thread");
    if (threadgrp_->start_threads() < 0) {
      cerr << node0 << indent
           << "CLHF: error starting threads" << endl;
      abort();
    }
    tim_exit("start thread");

    tim_enter("stop thread");
    if (threadgrp_->wait_threads() < 0) {
      cerr << node0 << indent
           << "CLHF: error waiting for threads" << endl;
      abort();
    }
    tim_exit("stop thread");
      
    double tnint=0;
    for (i=0; i < nthread; i++) {
      tnint += gblds[i]->tnint;

      if (i) {
        for (int j=0; j < ntri; j++)
          gmat[j] += gmats[i][j];
        delete[] gmats[i];
      }

      delete gblds[i];
      delete conts[i];
    }

    delete[] gmats;
    delete[] gblds;
    delete[] conts;
    delete[] pmax;
      
    scf_grp_->sum(&tnint, 1, 0, 0);
    cout << node0 << indent << scprintf("%20.0f integrals\n", tnint);

    tim_exit("ao_gmat");

    // if we're running on multiple processors, then sum the G matrix
    tim_enter("sum");
    if (scf_grp_->n() > 1)
      scf_grp_->sum(gmat, i_offset(basis()->nbasis()));
    tim_exit("sum");

    // if we're running on multiple processors, or we don't have local
    // matrices, then accumulate gtmp back into G
    tim_enter("accum");
    if (!local_ || scf_grp_->n() > 1)
      cl_gmat_->convert_accumulate(gtmp);
    tim_exit("accum");
  }

  // for now quit
  else {
    cerr << node0 << indent << "Cannot yet use anything but Local matrices\n";
    abort();
  }
  
  tim_enter("symm");
  // get rid of AO delta P
  cl_dens_diff_ = dd;
  dd = cl_dens_diff_.clone();

  // now symmetrize the skeleton G matrix, placing the result in dd
  RefSymmSCMatrix skel_gmat = cl_gmat_.copy();
  skel_gmat.scale(1.0/(double)pl->order());
  if (debug_>1) {
    skel_gmat.print("skel_gmat before symmetrize");
  }
  pl->symmetrize(skel_gmat,dd);
  if (debug_>1) {
    dd.print("dd after symmetrize");
  }
  tim_exit("symm");
  
  // F = H+G
  cl_fock_.result_noupdate().assign(hcore_);
  cl_fock_.result_noupdate().accumulate(dd);
  accumddh_->accum(cl_fock_.result_noupdate());
  cl_fock_.computed()=1;
}

/////////////////////////////////////////////////////////////////////////////

void
CLHF::two_body_energy(double &ec, double &ex)
{
  tim_enter("clhf e2");
  ec = 0.0;
  ex = 0.0;

  if (local_ || local_dens_) {
    // grab the data pointers from the G and P matrices
    double *pmat;
    tim_enter("local data");
    RefSymmSCMatrix dens = ao_density();
    dens->scale(2.0);
    dens->scale_diagonal(0.5);
    RefSymmSCMatrix ptmp = get_local_data(dens, pmat, SCF::Read);
    tim_exit("local data");

    // initialize the two electron integral classes
    tbi_ = integral()->electron_repulsion();
    tbi_->set_integral_storage(0);

    tim_enter("init pmax");
    signed char * pmax = init_pmax(pmat);
    tim_exit("init pmax");
  
    LocalCLHFEnergyContribution lclc(pmat);
    RefPetiteList pl = integral()->petite_list();
    LocalGBuild<LocalCLHFEnergyContribution>
      gb(lclc, tbi_, pl, basis(), scf_grp_, pmax, desired_value_accuracy()/100.0);
    gb.run();

    delete[] pmax;

    tbi_ = 0;

    ec = lclc.ec;
    ex = lclc.ex;
  }
  else {
    cerr << node0 << indent << "Cannot yet use anything but Local matrices\n";
    abort();
  }
  tim_exit("clhf e2");
}

/////////////////////////////////////////////////////////////////////////////

void
CLHF::two_body_deriv(double * tbgrad)
{
  int i;
  int na3 = molecule()->natom()*3;
  int nthread = threadgrp_->nthread();

  tim_enter("setup");
  RefSCElementMaxAbs m = new SCElementMaxAbs;
  cl_dens_.element_op(m);
  double pmax = m->result();
  m=0;

  double **grads = new double*[nthread];
  RefTwoBodyDerivInt *tbis = new RefTwoBodyDerivInt[nthread];
  for (i=0; i < nthread; i++) { 
    tbis[i] = integral()->electron_repulsion_deriv();
    grads[i] = new double[na3];
    memset(grads[i], 0, sizeof(double)*na3);
  }
  
  RefPetiteList pl = integral()->petite_list();
  
  tim_change("contribution");
  
  // now try to figure out the matrix specialization we're dealing with.
  // if we're using Local matrices, then there's just one subblock, or
  // see if we can convert P to a local matrix
  if (local_ || local_dens_) {
    double *pmat;
    RefSymmSCMatrix ptmp = get_local_data(cl_dens_, pmat, SCF::Read);

    LocalCLHFGradContribution l(pmat);
    LocalTBGrad<LocalCLHFGradContribution> **tblds =
      new LocalTBGrad<LocalCLHFGradContribution>*[nthread];

    for (i=0; i < nthread; i++) {
      tblds[i] = new LocalTBGrad<LocalCLHFGradContribution>(
        l, tbis[i], pl, basis(), scf_grp_, grads[i], pmax,
        desired_gradient_accuracy(), nthread, i);
      threadgrp_->add_thread(i, tblds[i]);
    }

    tim_enter("start thread");
    if (threadgrp_->start_threads() < 0) {
      cerr << node0 << indent
           << "CLSCF: error starting threads" << endl;
      abort();
    }
    tim_exit("start thread");

    tim_enter("stop thread");
    if (threadgrp_->wait_threads() < 0) {
      cerr << node0 << indent
           << "CLSCF: error waiting for threads" << endl;
      abort();
    }
    tim_exit("stop thread");

    for (i=0; i < nthread; i++) {
      if (i) {
        for (int j=0; j < na3; j++)
          grads[0][j] += grads[i][j];

        delete[] grads[i];
      }

      delete tblds[i];
    }

    if (scf_grp_->n() > 1)
      scf_grp_->sum(grads[0], na3);

    for (i=0; i < na3; i++)
      tbgrad[i] += grads[0][i];
    
    delete[] grads[0];
    delete[] tblds;
    delete[] grads;
  }

  // for now quit
  else {
    cerr << node0 << indent
         << "CLHF::two_body_deriv: can't do gradient yet\n";
    abort();
  }

  for (i=0; i < nthread; i++)
    tbis[i] = 0;
  delete[] tbis;
  
  tim_exit("contribution");
}

/////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: c++
// c-file-style: "ETS"
// End:
