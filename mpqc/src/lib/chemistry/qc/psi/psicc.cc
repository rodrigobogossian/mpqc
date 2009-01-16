//
// psicc.cc
//
// Copyright (C) 2007 Edward Valeev
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

#ifdef __GNUC__
#pragma implementation
#endif

#include <assert.h>
#include <psifiles.h>
#include <ccfiles.h>
#include <libdpd/dpd.h>

#include <chemistry/qc/mbptr12/orbitalspace.h>
#include <chemistry/qc/mbptr12/pairiter.impl.h>
#include <chemistry/qc/mbptr12/print.h>
#include <chemistry/qc/psi/psiwfn.h>
#include <chemistry/qc/psi/psicc.h>

using namespace std;
using namespace sc;

namespace {

  /// convert a std::vector<Source> to a C-type array. Container::size() must be defined
  template <typename Result, typename Source>
  Result*
    to_C(const std::vector<Source>& Cpp) {
      const unsigned int size = Cpp.size();
      Result* C = new Result[size];
      std::copy(Cpp.begin(),Cpp.end(),C);
      return C;
    }

  /// compare 2 2-body tensors
  template <sc::fastpairiter::PairSymm BraSymm, sc::fastpairiter::PairSymm KetSymm>
  void
  compare_tbint_tensors(const RefSCMatrix& T2, const RefSCMatrix& T2_ref,
                        unsigned int nbra1, unsigned int nbra2,
                        unsigned int nket1, unsigned int nket2,
                        double zero);
}

namespace sc {

  //////////////////////////////////////////////////////////////////////////

  static ClassDesc PsiCC_cd(typeid(PsiCC), "PsiCC", 1,
                            "public PsiCorrWavefunction", 0, create<PsiCC>,
                            create<PsiCC>);

  bool PsiCC::test_t2_phases_ = false;

  PsiCC::PsiCC(const Ref<KeyVal>&keyval) :
    PsiCorrWavefunction(keyval) {
  }

  PsiCC::~PsiCC() {
  }

  PsiCC::PsiCC(StateIn&s) :
    PsiCorrWavefunction(s) {
  }

  void PsiCC::save_data_state(StateOut&s) {
    PsiCorrWavefunction::save_data_state(s);
  }

  void PsiCC::dpd_start() {
    const int cachelev = 2;
    int* cachefiles = init_int_array(PSIO_MAXUNIT);
    int** cachelist = init_int_matrix(32,32);

    const Ref<OrbitalSpace>& aocc = occ_act_sb(Alpha);
    const Ref<OrbitalSpace>& avir = vir_act_sb(Alpha);

    int* aoccpi = to_C<int>(aocc->block_sizes());
    int* avirpi = to_C<int>(avir->block_sizes());
    int* aocc_sym = to_C<int>(aocc->orbsym());
    int* avir_sym = to_C<int>(avir->orbsym());

    // UHF/ROHF (ROHF always expected in semicanonical orbitals)
    if (reference()->reftype() != PsiSCF::rhf) {
      const Ref<OrbitalSpace>& bocc = occ_act_sb(Beta);
      const Ref<OrbitalSpace>& bvir = vir_act_sb(Beta);

      int* boccpi = to_C<int>(bocc->block_sizes());
      int* bvirpi = to_C<int>(bvir->block_sizes());
      int* bocc_sym = to_C<int>(bocc->orbsym());
      int* bvir_sym = to_C<int>(bvir->orbsym());

      dpd_init(0, nirrep(), 200000000, 0, cachefiles, cachelist,
               NULL, 4, aoccpi, aocc_sym, avirpi, avir_sym,
               boccpi, bocc_sym, bvirpi, bvir_sym);
    }
    // RHF
    else {
      dpd_init(0, nirrep(), 200000000, 0, cachefiles, cachelist,
               NULL, 2, aoccpi, aocc_sym, avirpi, avir_sym);
    }

    psi::PSIO& psio = exenv()->psio();
    for(int i=CC_MIN; i <= CC_MAX; i++) psio.open(i,1);
}

  void PsiCC::dpd_stop() {
    psi::PSIO& psio = exenv()->psio();
    for(int i=CC_MIN; i <= CC_MAX; i++) psio.close(i,1);
    dpd_close(0);
  }

  const RefSCMatrix&PsiCC::T1(SpinCase1 spin1) {
    if (T1_[spin1].nonnull())
      return T1_[spin1];
    PsiSCF::RefType reftype = reference_->reftype();

    // Grab T matrices
    const char* kwd = (spin1 == Beta && reftype != PsiSCF::rhf) ? "tia" : "tIA";
    T1_[spin1] = T1(spin1, kwd);
    if (debug() >= DefaultPrintThresholds::mostN2)
      T1_[spin1].print(prepend_spincase(spin1,"T1 amplitudes").c_str());

    return T1_[spin1];
  }

  const RefSCMatrix&PsiCC::T2(SpinCase2 spin2) {
    if (T2_[spin2].nonnull())
      return T2_[spin2];
    PsiSCF::RefType reftype = reference_->reftype();

    // If requesting AA or BB T2s, create their forms with unrestricted indices and read those
    if (spin2 != AlphaBeta) {
      dpd_start();

      if (spin2 == AlphaAlpha) {
        dpdbuf4 D;
        dpd_buf4_init(&D, CC_TAMPS, 0, 0, 5, 2, 7, 0, "tIJAB");
        dpd_buf4_copy(&D, CC_TAMPS, "tIJAB (IJ,AB)");
        dpd_buf4_close(&D);
      }
      if (spin2 == BetaBeta) {
        dpdbuf4 D;
        dpd_buf4_init(&D, CC_TAMPS, 0, 10, 15, 12, 17, 0, "tijab");
        dpd_buf4_copy(&D, CC_TAMPS, "tijab (ij,ab)");
        dpd_buf4_close(&D);
      }
      dpd_stop();
    }

    // Grab T matrices
    const char* kwd = (spin2 != AlphaBeta && reftype != PsiSCF::rhf) ? (spin2 == AlphaAlpha ? "tIJAB (IJ,AB)" : "tijab (ij,ab)") : "tIjAb";
    T2_[spin2] = T2(spin2, kwd);
    if (debug() >= DefaultPrintThresholds::mostN2)
      T2_[spin2].print(prepend_spincase(spin2,"T2 amplitudes").c_str());

    return T2_[spin2];
  }

  const RefSCMatrix&PsiCC::Tau2(SpinCase2 spin2) {
    if (Tau2_[spin2].nonnull())
      return Tau2_[spin2];
    PsiSCF::RefType reftype = reference_->reftype();

    // If requesting AA or BB Tau2s, create their forms with unrestricted indices and read those
    if (spin2 != AlphaBeta) {
      dpd_start();

      if (spin2 == AlphaAlpha) {
        dpdbuf4 D;
        dpd_buf4_init(&D, CC_TAMPS, 0, 0, 5, 2, 7, 0, "tauIJAB");
        dpd_buf4_copy(&D, CC_TAMPS, "tauIJAB (IJ,AB)");
        dpd_buf4_close(&D);
      }
      if (spin2 == BetaBeta) {
        dpdbuf4 D;
        dpd_buf4_init(&D, CC_TAMPS, 0, 10, 15, 12, 17, 0, "tauijab");
        dpd_buf4_copy(&D, CC_TAMPS, "tauijab (ij,ab)");
        dpd_buf4_close(&D);
      }
      dpd_stop();
    }

    const char* kwd = (spin2 != AlphaBeta && reftype != PsiSCF::rhf) ? (spin2 == AlphaAlpha ? "tauIJAB (IJ,AB)" : "tauijab (ij,ab)") : "tauIjAb";
    Tau2_[spin2] = T2(spin2, kwd);
    if (debug() >= DefaultPrintThresholds::mostO2N2)
      Tau2_[spin2].print(prepend_spincase(spin2,"Tau2 amplitudes").c_str());
    return Tau2_[spin2];
  }

  RefSCMatrix PsiCC::T1(SpinCase1 spin, const std::string& dpdlabel) {
    psi::PSIO& psio = exenv()->psio();
    // grab orbital info
    const std::vector<unsigned int>& occpi = reference()->occpi(spin);
    const std::vector<unsigned int>& uoccpi = reference()->uoccpi(spin);
    std::vector<unsigned int> actoccpi(nirrep_);
    std::vector<unsigned int> actuoccpi(nirrep_);
    std::vector<unsigned int> actoccioff(nirrep_);
    std::vector<unsigned int> actuoccioff(nirrep_);
    const std::vector<unsigned int>& frozen_docc = this->frozen_docc();
    const std::vector<unsigned int>& frozen_uocc = this->frozen_uocc();
    unsigned int nocc_act = 0;
    unsigned int nuocc_act = 0;
    for (unsigned int irrep=0; irrep<nirrep_; ++irrep) {
      actoccpi[irrep] = occpi[irrep] - frozen_docc_[irrep];
      actuoccpi[irrep] = uoccpi[irrep] - frozen_uocc_[irrep];
      nocc_act += actoccpi[irrep];
      nuocc_act += actuoccpi[irrep];
    }
    actoccioff[0] = 0;
    actuoccioff[0] = 0;
    for (unsigned int irrep=1; irrep<nirrep_; ++irrep) {
      actoccioff[irrep] = actoccioff[irrep-1] + actoccpi[irrep-1];
      actuoccioff[irrep] = actuoccioff[irrep-1] + actuoccpi[irrep-1];
    }

    RefSCDimension rowdim = new SCDimension(nocc_act);
    //rowdim->blocks()->set_subdim(0,new SCDimension(rowdim.n()));
    RefSCDimension coldim = new SCDimension(nuocc_act);
    //coldim->blocks()->set_subdim(0,new SCDimension(coldim.n()));
    RefSCMatrix T = matrixkit()->matrix(rowdim, coldim);
    T.assign(0.0);
    // if testing T2 transform, T1 amplitudes are not produced
    // also test that T1 is not empty
    if (!test_t2_phases_ && rowdim.n() && coldim.n()) {
      // read in the i by a matrix in DPD format
      unsigned int nia_dpd = 0;
      for (unsigned int h=0; h<nirrep_; ++h)
        nia_dpd += actoccpi[h] * actuoccpi[h];
      double* T1 = new double[nia_dpd];
      psio.open(CC_OEI, PSIO_OPEN_OLD);
      psio.read_entry(CC_OEI, const_cast<char*>(dpdlabel.c_str()),
                      reinterpret_cast<char*>(T1), nia_dpd*sizeof(double));
      psio.close(CC_OEI, 1);

      // form the full matrix
      unsigned int ia = 0;
      for (unsigned int h=0; h<nirrep_; ++h) {
        const unsigned int i_offset = actoccioff[h];
        const unsigned int a_offset = actuoccioff[h];
        for (int i=0; i<actoccpi[h]; ++i)
          for (int a=0; a<actuoccpi[h]; ++a, ++ia)
            T.set_element(i+i_offset, a+a_offset, T1[ia]);
      }
      delete[] T1;
    }

    return T;
  }

  RefSCMatrix PsiCC::T2(SpinCase2 spin12, const std::string& dpdlabel) {
    psi::PSIO& psio = exenv()->psio();
    const SpinCase1 spin1 = case1(spin12);
    const SpinCase1 spin2 = case2(spin12);

    typedef std::vector<unsigned int> uvec;
    // grab orbital info
    const uvec& occpi1 = reference()->occpi(spin1);
    const uvec& occpi2 = reference()->occpi(spin2);
    const uvec& uoccpi1 = reference()->uoccpi(spin1);
    const uvec& uoccpi2 = reference()->uoccpi(spin2);
    uvec actoccpi1(nirrep_);
    uvec actuoccpi1(nirrep_);
    uvec actoccioff1(nirrep_);
    uvec actuoccioff1(nirrep_);
    uvec actoccpi2(nirrep_);
    uvec actuoccpi2(nirrep_);
    uvec actoccioff2(nirrep_);
    uvec actuoccioff2(nirrep_);
    unsigned int nocc1_act = 0;
    unsigned int nuocc1_act = 0;
    unsigned int nocc2_act = 0;
    unsigned int nuocc2_act = 0;
    for (unsigned int irrep=0; irrep<nirrep_; ++irrep) {
      actoccpi1[irrep] = occpi1[irrep] - frozen_docc_[irrep];
      actuoccpi1[irrep] = uoccpi1[irrep] - frozen_uocc_[irrep];
      nocc1_act += actoccpi1[irrep];
      nuocc1_act += actuoccpi1[irrep];
      actoccpi2[irrep] = occpi2[irrep] - frozen_docc_[irrep];
      actuoccpi2[irrep] = uoccpi2[irrep] - frozen_uocc_[irrep];
      nocc2_act += actoccpi2[irrep];
      nuocc2_act += actuoccpi2[irrep];
    }
    actoccioff1[0] = 0;
    actuoccioff1[0] = 0;
    actoccioff2[0] = 0;
    actuoccioff2[0] = 0;
    for (unsigned int irrep=1; irrep<nirrep_; ++irrep) {
      actoccioff1[irrep] = actoccioff1[irrep-1] + actoccpi1[irrep-1];
      actuoccioff1[irrep] = actuoccioff1[irrep-1] + actuoccpi1[irrep-1];
      actoccioff2[irrep] = actoccioff2[irrep-1] + actoccpi2[irrep-1];
      actuoccioff2[irrep] = actuoccioff2[irrep-1] + actuoccpi2[irrep-1];
    }

    // DPD of orbital product spaces
    std::vector<int> ijpi(nirrep_);
    std::vector<int> abpi(nirrep_);
    unsigned int nijab_dpd = 0;
    for (unsigned int h=0; h<nirrep_; ++h) {
      unsigned int nij = 0;
      unsigned int nab = 0;
      for (unsigned int g=0; g<nirrep_; ++g) {
        nij += actoccpi1[g] * actoccpi2[h^g];
        nab += actuoccpi1[g] * actuoccpi2[h^g];
      }
      ijpi[h] = nij;
      abpi[h] = nab;
      nijab_dpd += nij*nab;
    }

    const unsigned int nij = nocc1_act*nocc2_act;
    const unsigned int nab = nuocc1_act*nuocc2_act;
    RefSCDimension rowdim = new SCDimension(nij);
    //rowdim->blocks()->set_subdim(0,new SCDimension(rowdim.n()));
    RefSCDimension coldim = new SCDimension(nab);
    //coldim->blocks()->set_subdim(0,new SCDimension(coldim.n()));
    RefSCMatrix T = matrixkit()->matrix(rowdim, coldim);
    T.assign(0.0);

    // If not empty...
    if (nijab_dpd) {
    // read in T2 in DPD form
    double* T2 = new double[nijab_dpd];
    psio.open(CC_TAMPS, PSIO_OPEN_OLD);
    psio.read_entry(CC_TAMPS, const_cast<char*>(dpdlabel.c_str()),
                    reinterpret_cast<char*>(T2), nijab_dpd*sizeof(double));
    psio.close(CC_TAMPS, 1);

    // convert to the full form
    unsigned int ijab = 0;
    unsigned int ij_offset = 0;
    unsigned int ab_offset = 0;
    for (unsigned int h=0; h<nirrep_; ij_offset+=ijpi[h], ab_offset+=abpi[h],
                                      ++h) {
      for (unsigned int g=0; g<nirrep_; ++g) {
        unsigned int gh = g^h;
        for (int i=0; i<actoccpi1[g]; ++i) {
          const unsigned int ii = i + actoccioff1[g];

          for (int j=0; j<actoccpi2[gh]; ++j) {
            const unsigned int jj = j + actoccioff2[gh];

            const unsigned int ij = ii * nocc2_act + jj;

            for (unsigned int f=0; f<nirrep_; ++f) {
              unsigned int fh = f^h;
              for (int a=0; a<actuoccpi1[f]; ++a) {
                const unsigned int aa = a + actuoccioff1[f];

                for (int b=0; b<actuoccpi2[fh]; ++b, ++ijab) {
                  const unsigned int bb = b + actuoccioff2[fh];
                  const unsigned int ab = aa*nuocc2_act + bb;

                  T.set_element(ij, ab, T2[ijab]);
                }
              }
            }
          }
        }
      }
    }
    delete[] T2;
    }

    return T;
  }

  const RefSCMatrix&PsiCC::Lambda1(SpinCase1 spin) {
    if (Lambda1_[spin].nonnull())
      return Lambda1_[spin];

    throw FeatureNotImplemented("PsiCC::Lambda1() -- cannot read Lambda1 amplitudes yet",__FILE__,__LINE__);
    return Lambda1_[spin];
  }

  const RefSCMatrix&PsiCC::Lambda2(SpinCase2 spin) {
    if (Lambda2_[spin].nonnull())
      return Lambda2_[spin];

    throw FeatureNotImplemented("PsiCC::Lambda2() -- cannot read Lambda2 amplitudes yet",__FILE__,__LINE__);
    return Lambda2_[spin];
  }

  RefSCMatrix PsiCC::transform_T1(const SparseMOIndexMap& occ_act_map,
                                  const SparseMOIndexMap& vir_act_map,
                                  const RefSCMatrix& T1,
                                  const Ref<SCMatrixKit>& kit) const {
    RefSCMatrix T1_new;
    T1_new = kit->matrix(T1.rowdim(), T1.coldim());
    T1_new.assign(0.0);

    const unsigned int no1 = occ_act_map.size();
    const unsigned int nv1 = vir_act_map.size();

    // convert T1 to new orbitals
    for (unsigned int i=0; i<no1; ++i) {
      const unsigned int ii = occ_act_map[i].first;
      const double ii_coef = occ_act_map[i].second;

      for (unsigned int a=0; a<nv1; ++a) {
        const unsigned int aa = vir_act_map[a].first;
        const double aa_coef = vir_act_map[a].second;

        const double elem = T1.get_element(ii, aa);
        T1_new.set_element(i, a, elem*ii_coef*aa_coef);
      }
    }

    return T1_new;
  }

  RefSCMatrix PsiCC::transform_T2(const SparseMOIndexMap& occ1_act_map,
                                  const SparseMOIndexMap& occ2_act_map,
                                  const SparseMOIndexMap& vir1_act_map,
                                  const SparseMOIndexMap& vir2_act_map,
                                  const RefSCMatrix& T2,
                                  const Ref<SCMatrixKit>& kit) const {
    RefSCMatrix T2_new;
    T2_new = kit->matrix(T2.rowdim(), T2.coldim());
    T2_new.assign(0.0);

    const unsigned int no1 = occ1_act_map.size();
    const unsigned int nv1 = vir1_act_map.size();
    const unsigned int no2 = occ2_act_map.size();
    const unsigned int nv2 = vir2_act_map.size();

    for (unsigned int i=0; i<no1; ++i) {
      const unsigned int ii = occ1_act_map[i].first;
      const double ii_coef = occ1_act_map[i].second;

      for (unsigned int j=0; j<no2; ++j) {
        const unsigned int jj = occ2_act_map[j].first;
        const double jj_coef = occ2_act_map[j].second;

        const unsigned int ij = i*no2+j;
        const unsigned int iijj = ii*no2+jj;

        const double ij_coef = ii_coef * jj_coef;

        for (unsigned int a=0; a<nv1; ++a) {
          const unsigned int aa = vir1_act_map[a].first;
          const double aa_coef = vir1_act_map[a].second;

          const double ija_coef = ij_coef * aa_coef;

          for (unsigned int b=0; b<nv2; ++b) {
            const unsigned int bb = vir2_act_map[b].first;
            const double bb_coef = vir2_act_map[b].second;

            const unsigned int ab = a*nv2+b;
            const unsigned int aabb = aa*nv2+bb;

            const double t2 = T2.get_element(iijj, aabb);
            const double t2_mpqc = t2 * ija_coef * bb_coef;
            T2_new.set_element(ij, ab, t2_mpqc);
          }
        }
      }
    }
    return T2_new;
  }

  RefSCMatrix PsiCC::transform_T1(const RefSCMatrix& occ_act_tform,
                                  const RefSCMatrix& vir_act_tform,
                                  const RefSCMatrix& T1,
                                  const Ref<SCMatrixKit>& kit) const {
    // convert to raw storage
    double* t1 = new double[T1.rowdim().n() * T1.coldim().n()];
    T1.convert(t1);

    RefSCMatrix T1_ia = kit->matrix(occ_act_tform.coldim(),
                                    vir_act_tform.coldim());
    T1_ia.assign(t1);

    RefSCMatrix T1_Ia = occ_act_tform * T1_ia;
    RefSCMatrix T1_IA = T1_Ia * vir_act_tform.t();

    return T1_IA;
  }

  RefSCMatrix PsiCC::transform_T2(const RefSCMatrix& occ1_act_tform,
                                  const RefSCMatrix& occ2_act_tform,
                                  const RefSCMatrix& vir1_act_tform,
                                  const RefSCMatrix& vir2_act_tform,
                                  const RefSCMatrix& T2,
                                  const Ref<SCMatrixKit>& kit) const {
    assert(occ1_act_tform.rowdim().n() == occ1_act_tform.coldim().n());
    assert(occ2_act_tform.rowdim().n() == occ2_act_tform.coldim().n());
    assert(vir1_act_tform.rowdim().n() == vir1_act_tform.coldim().n());
    assert(vir2_act_tform.rowdim().n() == vir2_act_tform.coldim().n());

    // convert to raw storage
    double* t2 = new double[T2.rowdim().n() * T2.coldim().n()];
    T2.convert(t2);

    const unsigned int nij = T2.rowdim().n();
    const unsigned int nab = T2.coldim().n();
    const unsigned int njab = occ2_act_tform.coldim().n() * nab;
    const unsigned int nija = vir1_act_tform.coldim().n() * nij;

    // store as i by jab
    RefSCMatrix T2_i_jab = kit->matrix(occ1_act_tform.coldim(),
                                       new SCDimension(njab));
    T2_i_jab.assign(t2);
    // transform i -> I
    RefSCMatrix T2_I_jab = occ1_act_tform * T2_i_jab;
    T2_i_jab = 0;
    T2_I_jab.convert(t2);

    // for each I store as j by ab
    // transform j -> J
    RefSCMatrix t2_j_ab = kit->matrix(occ2_act_tform.coldim(), T2.coldim());
    RefSCMatrix t2_J_ab = kit->matrix(occ2_act_tform.rowdim(), T2.coldim());
    const unsigned int nI = T2_I_jab.rowdim().n();
    for (unsigned int I=0; I<nI; ++I) {
      t2_j_ab.assign(t2 + I*njab);
      t2_J_ab.assign(0.0);
      t2_J_ab.accumulate_product(occ2_act_tform, t2_j_ab);
      t2_J_ab.convert(t2 + I*njab);
    }

    // for each IJ store as a by b
    // transform a -> A
    RefSCMatrix t2_a_b = kit->matrix(vir1_act_tform.coldim(),
                                     vir2_act_tform.coldim());
    RefSCMatrix t2_A_b = kit->matrix(vir1_act_tform.rowdim(),
                                     vir2_act_tform.coldim());
    const unsigned int nIJ = T2.rowdim().n();
    for (unsigned int IJ=0; IJ<nIJ; ++IJ) {
      t2_a_b.assign(t2 + IJ*nab);
      t2_A_b.assign(0.0);
      t2_A_b.accumulate_product(vir1_act_tform, t2_a_b);
      t2_A_b.convert(t2 + IJ*nab);
    }

    // store as IJA by b
    RefSCMatrix T2_IJA_b = kit->matrix(new SCDimension(nija), vir2_act_tform.coldim());
    T2_IJA_b.assign(t2);
    // transform B -> B
    RefSCMatrix T2_IJA_B = T2_IJA_b * vir2_act_tform.t();
    T2_IJA_b = 0;
    T2_IJA_B.convert(t2);

    // store as IJ by AB
    RefSCMatrix T2_IJ_AB = kit->matrix(T2.rowdim(), T2.coldim());
    T2_IJ_AB.assign(t2);
    return T2_IJ_AB;
  }

  namespace {
    bool gtzero(double a) {
      return a > 0.0;
    }
  }

  void PsiCC::compare_T2(const RefSCMatrix& T2, const RefSCMatrix& T2_ref,
                         SpinCase2 spin12, unsigned int no1, unsigned int no2, unsigned int nv1,
                         unsigned int nv2, double zero) const {
    using namespace sc::fastpairiter;
    if (spin12 != AlphaBeta)
      compare_tbint_tensors<AntiSymm,AntiSymm>(T2,T2_ref,no1,no2,nv1,nv2,zero);
    else
      compare_tbint_tensors<ASymm,ASymm>(T2,T2_ref,no1,no2,nv1,nv2,zero);
  }

  //////////////////////////////////////////////////////////////////////////

  static ClassDesc PsiCCSD_cd(typeid(PsiCCSD), "PsiCCSD", 1, "public PsiCC", 0,
                              create<PsiCCSD>, create<PsiCCSD>);

  PsiCCSD::PsiCCSD(const Ref<KeyVal>&keyval) :
    PsiCC(keyval) {
  }

  PsiCCSD::~PsiCCSD() {
  }

  PsiCCSD::PsiCCSD(StateIn&s) :
    PsiCC(s) {
  }

  int PsiCCSD::gradient_implemented() const {
    int impl = 0;
    PsiSCF::RefType reftype = reference_->reftype();
    if (reftype == PsiSCF::rhf || reftype == PsiSCF::hsoshf)
      impl = 1;
    return impl;
  }

  void PsiCCSD::save_data_state(StateOut&s) {
    PsiCC::save_data_state(s);
  }

  void PsiCCSD::write_input(int convergence) {
    Ref<PsiInput> input = get_psi_input();
    input->open();
    PsiCorrWavefunction::write_input(convergence);
    input->write_keyword("psi:wfn", "ccsd");
    input->close();
  }

  //////////////////////////////////////////////////////////////////////////

  static ClassDesc PsiCCSD_T_cd(typeid(PsiCCSD_T), "PsiCCSD_T", 1,
                                "public PsiCC", 0, create<PsiCCSD_T>, create<
                                    PsiCCSD_T>);

  PsiCCSD_T::PsiCCSD_T(const Ref<KeyVal>&keyval) :
    PsiCC(keyval) {
  }

  PsiCCSD_T::~PsiCCSD_T() {
  }

  PsiCCSD_T::PsiCCSD_T(StateIn&s) :
    PsiCC(s) {
  }

  int PsiCCSD_T::gradient_implemented() const {
    int impl = 0;
    PsiSCF::RefType reftype = reference_->reftype();
    return impl;
  }

  void PsiCCSD_T::save_data_state(StateOut&s) {
    PsiCC::save_data_state(s);
    SavableState::save_state(reference_.pointer(), s);
  }

  void PsiCCSD_T::write_input(int convergence) {
    Ref<PsiInput> input = get_psi_input();
    input->open();
    PsiCorrWavefunction::write_input(convergence);
    input->write_keyword("psi:wfn", "ccsd_t");
    input->close();
  }

} // namespace

//////////////////

namespace {
  using namespace sc::fastpairiter;
  template <PairSymm BraSymm, PairSymm KetSymm>
  void
  compare_tbint_tensors(const RefSCMatrix& T2, const RefSCMatrix& T2_ref,
                        unsigned int nb1, unsigned int nb2, unsigned int nk1,
                        unsigned int nk2, double zero)
  {
    T2_ref.print("compare_tbint_tensors() -- T2(ref)");
    T2.print("compare_tbint_tensors() -- T2");
    sc::fastpairiter::MOPairIter<BraSymm> biter(nb1,nb2);
    sc::fastpairiter::MOPairIter<KetSymm> kiter(nk1,nk2);
    for(biter.start(); biter; biter.next()) {
      const int b12 = biter.ij();
      for(kiter.start(); kiter; kiter.next()) {
        const int k12 = kiter.ij();

        const double t2 = T2.get_element(b12, k12);
        const double t2_ref = T2_ref.get_element(b12, k12);

        if (fabs(t2_ref - t2) > zero) {
          T2_ref.print("compare_tbint_tensors() -- T2(ref)");
          T2.print("compare_tbint_tensors() -- T2");
          throw ProgrammingError("2-body tensors do not match",__FILE__,__LINE__);
        }
      }
    }
  }

} // anonymous namespace

// Local Variables:
// mode: c++
// c-file-style: "CLJ"
// End:
