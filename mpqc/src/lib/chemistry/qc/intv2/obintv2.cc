
#include <stdio.h>

extern "C" {
#include <chemistry/qc/intv2/int_macros.h>
#include <chemistry/qc/intv2/atoms.h>
#include <chemistry/qc/intv2/atomsfree.h>
#include <chemistry/qc/intv2/comp_1e.gbl>
#include <chemistry/qc/intv2/offsets.gbl>
}

#include <chemistry/qc/intv2/int_cplus.h>
#include <chemistry/qc/intv2/obintv2.h>

////////////////////////////////////////////////////////////////////////////
// OneBodyIntv2

OneBodyIntv2::OneBodyIntv2(const RefGaussianBasisSet&bs) :
  OneBodyInt(bs)
{
  c1 = c2 = int_centers_from_gbs(bs);
  int_initialize_1e(0,0,c1,c1);
  int_initialize_offsets1(c1,c1);
  same_center=1;
}

OneBodyIntv2::OneBodyIntv2(const RefGaussianBasisSet&bs1,
                           const RefGaussianBasisSet&bs2) :
  OneBodyInt(bs1,bs2)
{
  c1 = int_centers_from_gbs(bs1);
  c2 = int_centers_from_gbs(bs2);
  int_initialize_1e(0,0,c1,c2);
  int_initialize_offsets1(c1,c2);
  same_center=0;
}

OneBodyIntv2::~OneBodyIntv2()
{
  int_done_offsets1(c1,c2);
  int_done_1e();
  free_centers(c1);
  free(c1);
  if (!same_center) {
    free_centers(c2);
    free(c2);
  }
}

////////////////////////////////////////////////////////////////////////////
// GaussianOverlapIntv2

GaussianOverlapIntv2::GaussianOverlapIntv2(const RefGaussianBasisSet&bs_) :
  OneBodyIntv2(bs_)
{
}

GaussianOverlapIntv2::GaussianOverlapIntv2(const RefGaussianBasisSet&bs1,
                                           const RefGaussianBasisSet&bs2) :
  OneBodyIntv2(bs1,bs2)
{
}

GaussianOverlapIntv2::~GaussianOverlapIntv2()
{
}

void
GaussianOverlapIntv2::compute_shell(int i, int j)
{
  int_shell_overlap(c1,c2,buffer_,i,j);
}

////////////////////////////////////////////////////////////////////////////
// GaussianKineticIntv2

GaussianKineticIntv2::GaussianKineticIntv2(const RefGaussianBasisSet&bs_) :
  OneBodyIntv2(bs_)
{
}

GaussianKineticIntv2::GaussianKineticIntv2(const RefGaussianBasisSet&bs1,
                                           const RefGaussianBasisSet&bs2) :
  OneBodyIntv2(bs1,bs2)
{
}

GaussianKineticIntv2::~GaussianKineticIntv2()
{
}

void
GaussianKineticIntv2::compute_shell(int i, int j)
{
  int_shell_kinetic(c1,c2,buffer_,i,j);
}

////////////////////////////////////////////////////////////////////////////
// GaussianPointChargeIntv2

void
GaussianPointChargeIntv2::init(PointBag_double*charges)
{
  ncharge = charges->length();
  
  if (ncharge) {
    position = new double*[ncharge];
    charge = new double[ncharge];
  }
  
  int i = 0;
  for (Pix pix= charges->first(); pix!=0; charges->next(pix)) {
    position[i] = new double[3];
    charge[i] = charges->get(pix);
    for (int j=0; j<3; j++) {
      position[i][j] = charges->point(pix)[j];
    }
    i++;
  }
}

GaussianPointChargeIntv2::GaussianPointChargeIntv2(PointBag_double*charges,
                                           const RefGaussianBasisSet&bs_) :
  OneBodyIntv2(bs_)
{
  init(charges);
  delete charges;
}

GaussianPointChargeIntv2::GaussianPointChargeIntv2(PointBag_double*charges,
					   const RefGaussianBasisSet&bs1,
                                           const RefGaussianBasisSet&bs2):
  OneBodyIntv2(bs1,bs2)
{
  init(charges);
  delete charges;
}

GaussianPointChargeIntv2::~GaussianPointChargeIntv2()
{
  for (int i=0; i<ncharge; i++) {
      delete position[i];
    }
  if (ncharge) {
      delete[] charge;
      delete[] position;
    }
}

void
GaussianPointChargeIntv2::compute_shell(int i,int j)
{
  int_shell_point_charge(c1,c2,buffer_,i,j,ncharge,charge,position);
}

////////////////////////////////////////////////////////////////////////////
// GaussianNuclearIntv2

GaussianNuclearIntv2::GaussianNuclearIntv2(const RefGaussianBasisSet&bs_) :
  GaussianPointChargeIntv2(bs_->molecule()->charges(),bs_)
{
}

GaussianNuclearIntv2::GaussianNuclearIntv2(PointBag_double *charges,
                                           const RefGaussianBasisSet&bs1,
                                           const RefGaussianBasisSet&bs2) :
  GaussianPointChargeIntv2(charges,bs1,bs2)
{
}

GaussianNuclearIntv2::~GaussianNuclearIntv2()
{
}

////////////////////////////////////////////////////////////////////////////
// GaussianEfieldDotVectorIntv2

GaussianEfieldDotVectorIntv2::GaussianEfieldDotVectorIntv2(
    const RefGaussianBasisSet&bs_,
    double *p,
    double *v) :
  OneBodyIntv2(bs_)
{
  int biggest_shell = bs_->max_nfunction_in_shell();
  if (biggest_shell) {
      buffer3_ = new double[biggest_shell * biggest_shell * 3];
    }
  else {
      buffer3_ = 0;
    }
  if (p) position(p);
  if (v) vector(v);
}

GaussianEfieldDotVectorIntv2::GaussianEfieldDotVectorIntv2(
    const RefGaussianBasisSet&bs1,
    const RefGaussianBasisSet&bs2,
    double *p,
    double *v) :
  OneBodyIntv2(bs1,bs2)
{
  int biggest_shell = bs1->max_nfunction_in_shell() *
                      bs2->max_nfunction_in_shell();
  if (biggest_shell) {
      buffer3_ = new double[biggest_shell * 3];
    }
  else {
      buffer3_ = 0;
    }
  if (p) position(p);
  if (v) vector(v);
}

GaussianEfieldDotVectorIntv2::~GaussianEfieldDotVectorIntv2()
{
  if (buffer3_) delete[] buffer3_;
}

void
GaussianEfieldDotVectorIntv2::position(const double *a)
{
  for (int i=0; i<3; i++) position_[i] = a[i];
}

void
GaussianEfieldDotVectorIntv2::vector(const double *a)
{
  for (int i=0; i<3; i++) vector_[i] = a[i];
}

void
GaussianEfieldDotVectorIntv2::compute_shell(int i,int j)
{
  int nbfi = basis1()->operator()(i).nfunction();
  int nbfj = basis2()->operator()(j).nfunction();
  int nint = nbfi*nbfj;
  int nint3 = nint*3;
  double *tmp;
  int ii,jj;

  tmp = buffer3_;
  for (ii=0;ii<nint3;ii++) *tmp++ = 0.0;

  int_accum_shell_efield(c1,c2,buffer3_,i,j,position_);

  tmp = buffer3_;
  for (ii=0; ii<nint; ii++) {
      buffer_[ii] = 0.0;
      for (jj=0; jj<3; jj++) {
          buffer_[ii] += *tmp++ * vector_[jj];
        }
    }
}

////////////////////////////////////////////////////////////////////////////
// GaussianDipoleIntv2

GaussianDipoleIntv2::GaussianDipoleIntv2(const RefGaussianBasisSet&bs_,
                                         const double* o) :
  OneBodyIntv2(bs_)
{
  if (o) origin(o);
}

GaussianDipoleIntv2::GaussianDipoleIntv2(const RefGaussianBasisSet&bs1,
                                         const RefGaussianBasisSet&bs2,
                                         const double* o) :
  OneBodyIntv2(bs1,bs2)
{
  if (o) origin(o);
}

GaussianDipoleIntv2::~GaussianDipoleIntv2()
{
}

void
GaussianDipoleIntv2::origin(const double *a)
{
  for (int i=0; i<3; i++) origin_[i] = a[i];
}

void
GaussianDipoleIntv2::compute_shell(int i,int j)
{
  int nbfi = basis1()->operator()(i).nfunction();
  int nbfj = basis2()->operator()(j).nfunction();
  int nint3 = nbfi*nbfj*3;
  double *tmp;
  int ii;

  tmp = buffer_;
  for (ii=0;ii<nint3;ii++) *tmp++ = 0.0;

  int_accum_shell_dipole(c1,c2,buffer_,i,j,origin_);
}

////////////////////////////////////////////////////////////////////////////
// OneBodyDerivIntv2

OneBodyDerivIntv2::OneBodyDerivIntv2(const RefGaussianBasisSet&bs) :
  OneBodyDerivInt(bs)
{
  c1 = c2 = int_centers_from_gbs(bs);
  int_initialize_offsets1(c1,c1);
  intbuf = int_initialize_1e(0,1,c1,c1);
  same_center=1;
}

OneBodyDerivIntv2::OneBodyDerivIntv2(const RefGaussianBasisSet&bs1,
                                     const RefGaussianBasisSet&bs2) :
  OneBodyDerivInt(bs1,bs2)
{
  c1 = int_centers_from_gbs(bs1);
  c2 = int_centers_from_gbs(bs2);
  int_initialize_offsets1(c1,c2);
  intbuf = int_initialize_1e(0,1,c1,c2);
  same_center=0;
}

OneBodyDerivIntv2::~OneBodyDerivIntv2()
{
  int_done_offsets1(c1,c2);
  int_done_1e();
  free_centers(c1);
  free(c1);
  if (!same_center) {
    free_centers(c2);
    free(c2);
  }
}

void
OneBodyDerivIntv2::compute_hcore_shell(int center, int ish, int jsh)
{
  int_shell_hcore_1der(c1,c2,intbuf,ish,jsh,c1,center);

  int ni = INT_SH_NFUNC((c1),ish);
  int nj = INT_SH_NFUNC((c2),jsh);

  memcpy(buffer_, intbuf, sizeof(double)*ni*nj*3);
}

void
OneBodyDerivIntv2::compute_overlap_shell(int center, int ish, int jsh)
{
  int_shell_overlap_1der(c1,c2,intbuf,ish,jsh,c1,center);

  int ni = INT_SH_NFUNC((c1),ish);
  int nj = INT_SH_NFUNC((c2),jsh);

  memcpy(buffer_, intbuf, sizeof(double)*ni*nj*3);
}
