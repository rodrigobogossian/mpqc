
#if defined(__cplusplus) && defined(__GNUC__)
#pragma interface
#endif

#ifndef _chemistry_qc_intv2_tranform_h
#define _chemistry_qc_intv2_tranform_h

#include <chemistry/qc/intv2/atoms.h>

#ifdef __cplusplus
extern "C" {
#endif

/* integrals and target may overlap */
void int_transform_1e(double *integrals, double *target,
                      shell_t *sh1, shell_t *sh2);

/* integrals and target may not overlap */
void int_accum_transform_1e(double *integrals, double *target,
                            shell_t *sh1, shell_t *sh2);

/* integrals and target may overlap */
void int_transform_1e_xyz(double *integrals, double *target,
                          shell_t *sh1, shell_t *sh2);

/* integrals and target may not overlap */
void int_accum_transform_1e_xyz(double *integrals, double *target,
                                shell_t *sh1, shell_t *sh2);

/* integrals and target may overlap */
void int_transform_2e(double *integrals, double *target,
                      shell_t *sh1, shell_t *sh2,
                      shell_t *sh3, shell_t *sh4);

#ifdef __cplusplus
}

#include <chemistry/qc/basis/transform.h>
#include <chemistry/qc/intv2/int_macros.h>

class SphericalTransformComponentV2 : public SphericalTransformComponent {
  public:
    void init(int a, int b, int c, double coef, int pureindex) {
      a_ = a;
      b_ = b;
      c_ = c;
      coef_ = coef;
      pureindex_ = pureindex;
      cartindex_ = INT_CARTINDEX(a+b+c,a,b);
    }
};

class SphericalTransformV2 : public SphericalTransform {
  public:
    SphericalTransformV2(int l) {
      n_=0;
      l_=l;
      components_=0;
      init();
    }

    SphericalTransformComponent * new_components() {
      return new SphericalTransformComponentV2[n_+1];
    }
};

class ISphericalTransformV2 : public ISphericalTransform {
  public:
    ISphericalTransformV2(int l) {
      n_ = 0;
      l_ = l;
      components_ = 0;
      init();
    }

    SphericalTransformComponent * new_components() {
      return new SphericalTransformComponentV2[n_+1];
    }
};

class SphericalTransformIterV2 : public SphericalTransformIter {
  public:
    SphericalTransformIterV2(int l, int inverse=0);
};

#endif /* __c_plus_plus */

#endif
