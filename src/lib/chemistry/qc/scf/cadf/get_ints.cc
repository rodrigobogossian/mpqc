//
// get_ints.cc
//
// Copyright (C) 2014 David Hollman
//
// Author: David Hollman
// Maintainer: DSH
// Created: Feb 13, 2014
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

//////////////////////////////////////////////////////////////////////////////////


// MPQC includes
#include <util/container/conc_cache.h>
#include "cadfclhf.h"

using namespace sc;
using std::make_shared;

CADFCLHF::TwoCenterIntContainerPtr
CADFCLHF::ints_to_eigen(
    int ish, int jsh,
    Ref<TwoBodyTwoCenterInt>& ints, TwoBodyOper::type int_type
){

#if USE_INTEGRAL_CACHE
  return ints2_->get_or_permute(ish, jsh, int_type,
  // Compute function
  [&ints, this](
      const boost::tuple<int, int, TwoBodyOper::type>& keys_canonical
  ){
    int ish, jsh;
    TwoBodyOper::type int_type;
    boost::tie(ish, jsh, int_type) = keys_canonical;
    // This won't work if the basis sets for ish and jsh are different,
    //   since keys_canonical may have a different ordering from the
    //   order of indices in ints.
#endif

    const int nbfi = dfbs_->shell(ish).nfunction();
    const int nbfj = dfbs_->shell(jsh).nfunction();
    auto rv = make_shared<TwoCenterIntContainer>(nbfi, nbfj);
    //----------------------------------------//
    ints->compute_shell(ish, jsh);
    const double* buffer = ints->buffer(int_type);
    //::memcpy(rv->data(), buffer, nbfi*nbfj * sizeof(double));
    std::copy(buffer, buffer + nbfi*nbfj, rv->data());
    //std::move(buffer, buffer + nbfi*nbfj, rv->data());
    //----------------------------------------//
    ints_computed_locally_ += nbfi * nbfj;
    return rv;

#if USE_INTEGRAL_CACHE
  },
  // Permute function
  [](
      const TwoCenterIntContainerPtr& perm_val,
      const boost::tuple<int, int, TwoBodyOper::type>& keys_canonical
  ){
    auto rv = make_shared<TwoCenterIntContainer>(perm_val->transpose());
    return rv;
  });
#endif

}

//////////////////////////////////////////////////////////////////////////////////

CADFCLHF::ThreeCenterIntContainerPtr
CADFCLHF::ints_to_eigen(
    int ish, int jsh, int ksh,
    Ref<TwoBodyThreeCenterInt>& ints,
    TwoBodyOper::type int_type
){

#if USE_INTEGRAL_CACHE
  return ints3_->get_or_permute(ish, jsh, ksh, int_type,
  // Compute function
  [&ints, this](
      const boost::tuple<int, int, int, TwoBodyOper::type>& key_canonical
  ){
    int ish, jsh, ksh;
    TwoBodyOper::type int_type;
    boost::tie(ish, jsh, ksh, int_type) = key_canonical;
    // Note: This won't work if these basis sets for ish and jsh are different,
    //   since keys_canonical may have a different ordering from the
    //   order of indices in ints.
#endif

    const int nbfi = gbs_->shell(ish).nfunction();
    const int nbfj = gbs_->shell(jsh).nfunction();
    const int nbfk = dfbs_->shell(ksh).nfunction();
    auto rv = make_shared<ThreeCenterIntContainer>(nbfi * nbfj, nbfk);
    //----------------------------------------//
    ints->compute_shell(ish, jsh, ksh);
    const double* buffer = ints->buffer(int_type);
    std::copy(buffer, buffer + nbfi*nbfj*nbfk, rv->data());
    //----------------------------------------//
    ints_computed_locally_ += nbfi * nbfj * nbfk;
    return rv;

#if USE_INTEGRAL_CACHE
  },
  // Permute function
  [ish, jsh, ksh, &ints, this](
      const ThreeCenterIntContainerPtr& perm_val,
      const boost::tuple<int, int, int, TwoBodyOper::type>& key_canonical
  ){
    const int nbfi = gbs_->shell(ish).nfunction();
    const int nbfj = gbs_->shell(jsh).nfunction();
    const int nbfk = dfbs_->shell(ksh).nfunction();

    auto rv = make_shared<ThreeCenterIntContainer>(nbfi * nbfj, nbfk);
    for(int i = 0; i < nbfi; ++i) {
      for(int j = 0; j < nbfj; ++j) {
        rv->row(i*nbfj + j) = perm_val->row(j*nbfi + i);
      }
    }

    return rv;
  });
#endif

}


CADFCLHF::FourCenterIntContainerPtr
CADFCLHF::ints_to_eigen(
    const ShellData& ish, const ShellData& jsh,
    const ShellData& ksh, const ShellData& lsh,
    Ref<TwoBodyInt>& ints, TwoBodyOper::type int_type
){
    auto rv = make_shared<FourCenterIntContainer>(ish.nbf*jsh.nbf, ksh.nbf*lsh.nbf);
    //----------------------------------------//
    ints->compute_shell(ish, jsh, ksh, lsh);
    const double* buffer = ints->buffer(int_type);
    std::copy(buffer, buffer + ish.nbf*jsh.nbf*ksh.nbf*lsh.nbf, rv->data());
    //----------------------------------------//
    ints_computed_locally_ += ish.nbf * jsh.nbf * ksh.nbf * lsh.nbf;
    return rv;
}


