
#include <iostream.h>

#include <math.h>

#include <util/misc/libmisc.h>
#include <util/group/message.h>

#include <math/scmat/disthql.h>
#include <math/linpackd/linpackd.h>

#include <math/scmat/f77sym.h>

extern "C" {
  void F77_PDSTEQR(int *n, double *d, double *e,
                double *z, int *ldz, int *nz, double *work,
                int *info);
}

static void dist_diagonalize_(int n, int m, double *a, double *d, double *e,
                              double *sigma, double *z, double *v, double *w,
                              int *ind, const RefMessageGrp&);
static void
pimtql2_ (double *d,double *e,int *sn,double *z,int *sm,int *info);

static double absol(double x);

static void pflip(int id,int n,int m,int p,double *ar,double *ac,double *w,
                  const RefMessageGrp&);

static double epslon (double x);

static void update(double *z,int m,double c,double s);

static void
ptred2_(double *a, int *lda, int *n, int *m, int *p, int *id,
        double *d, double *e, double *z, double *work,
        const RefMessageGrp& grp);

static void
ptred_single(double *a,int *lda,int *n,int *m,int *p,int *id,
             double *d,double *e,double *z,double *work);
static void
ptred_parallel(double *a, int *lda, int *n, int *m, int *p, int *id,
               double *d, double *e, double *z, double *work,
               const RefMessageGrp&);

/* ******************************************************** */
/* Function of this subroutine :                            */ 
/*    Diagonalize a real, symmetric matrix                  */ 
/*                                                          */
/* Parameters :                                             */
/*    n   - size of the matrix                              */
/*    m   - number of locally held columns                  */
/*    a[n][m] - locally held submatrix                      */
/*    d[n]    - returned with eigenvalues                   */
/*    v[n][m] - returned with eigenvectors                  */
/* -------------------------------------------------------- */
void
dist_diagonalize(int n, int m, double *a, double *d, double *v,
                 const RefMessageGrp &grp)
{
  double *e = new double[n];
  double *sigma = new double[n];
  double *z = new double[n*m];
  double *w = new double[3*n];
  int *ind = new int[n];
  dist_diagonalize_(n, m, a, d, e, sigma, z, v, w, ind, grp);
  delete[] e;
  delete[] sigma;
  delete[] z;
  delete[] w;
  delete[] ind;
}

/* ******************************************************** */
/* Function of this subroutine :                            */ 
/*    Diagonalize a real, symmetric matrix                  */ 
/*                                                          */
/* Parameters :                                             */
/*    n   - size of the matrix                              */
/*    m   - number of locally held columns                  */
/*    a[n][m] - locally held submatrix                      */
/*    d[n]    - returned with eigenvalues                   */
/*    e[n]    - scratch space                               */
/*    sigma[n]- scratch space                               */
/*    z[m][n] - scratch space                               */
/*    v[n][m] - returned with eigenvectors                  */
/*    w[3*n]  - scratch space                               */
/*    ind[n]  - scratch space (integer)                     */
/* -------------------------------------------------------- */
static void
dist_diagonalize_(int n, int m, double *a, double *d, double *e,
                  double *sigma, double *z, double *v, double *w, int *ind,
                  const RefMessageGrp& grp)
{
  int i,info,one=1;
  int nproc = grp->n();
  int id = grp->me();

 /* reduce A to tridiagonal matrix using Householder transformation */

  ptred2_(&a[0],&n,&n,&m,&nproc,&id,&d[0],&e[0],&z[0],&w[0],grp);

 /* diagonalize tridiagonal matrix using implicit QL method */

#if 0
  pimtql2_(d,e,&n,z,&m,&info);
  if (info != 0) {
      cout << "dist_diagonalize: node "
           << grp->me() << ": nonzero ierr from pimtql2" << endl;
      abort();
    }
#else
   for (i=1; i<n; i++) e[i-1] = e[i];
   F77_PDSTEQR(&n, d, e, z, &m, &m, w, &info);
#endif

 /* rearrange the eigenvectors by transposition */

  i = m * n;
  dcopy_(&i,&z[0],&one,&a[0],&one);
  pflip(id,n,m,nproc,&a[0],&v[0],&w[0],grp);
}

/* ******************************************************** */
/* Function of this subroutine :                            */ 
/*    diagonalize a real, symmetric tridiagonal matrix      */
/*    using the QL method                                   */ 
/* Parameters :                                             */
/*  on entry :                                              */
/*    d[n]    - the diagonal of the tridiagonal result      */
/*    e[n]    - the offdiagonal of the result(e[1]-e[n-1])  */
/*    sn      - size of the tridiagonal matrix              */
/*    z[m][n] - m rows of transformation matrix from before */
/*    m   - number of locally held columns                  */
/*  on return :                                             */
/*    d[n]    - the eigenvalues                             */
/*    e[n]    - non-useful information                      */
/*    z[m][n] - m rows of eigenvectors                      */
/*    info    - if 0, results are accurate                  */
/*              if nonzero, results may be inaccurate       */
/* -------------------------------------------------------- */

static void
pimtql2_ (double *d,double *e,int *sn,double *z,int *sm,int *info)
{
   double  c,s,t,q,u,p,h,macheps;
   int     n,m,i,j,k,im,its,maxit=30,one=1;

   /* extract parameters */

   *info = 0;
   n = *sn;
   m = *sm;
   t = 1.0;
   macheps = epslon(t);

   for (i=1; i<n; i++) e[i-1] = e[i];
   e[n-1] = 0.0;
   k = n - 2;
   for (j=0; j<n; j++) {
      its = 0;
      while (its < maxit) {
         for (im=j; im<=k; im++) {
            // this is the original threshold
            double threshold = macheps*(absol(d[im])+absol(d[im+1]));
            // new threshold will hopefully ensure convergence
            //if (threshold < macheps) threshold = macheps;
            if (absol(e[im]) <= threshold) break; 
//from NR:
//             double dsum = absol(d[im])+absol(d[im+1]);
//             if (dsum + absol(e[im]) == dsum) break;
         }
         u = d[j];
         if (im == j) break;
         else {
            its++;

            /* form implicit Wilkinson shift */

            q = (d[j+1] - u) / (2.0 * e[j]);
            t = sqrt(1.0 + q * q);      
            q = d[im] - u + e[j]/((q < 0.0) ? q - t : q + t);
            u = 0.0;
            s = c = 1.0;
            for (i=im-1; i>=j; i--) {
               p = s * e[i];
               h = c * e[i];
               if (absol(p) >= absol(q)) {
                  c = q / p;
                  t = sqrt(1.0 + c * c);
                  e[i+1] = p * t;
                  s = 1.0 / t;
                  c *= s;
               } else {
                  s = p / q;
                  t = sqrt(1.0 + s * s);
                  e[i+1] = q * t;
                  c = 1.0 / t;
                  s *= c;
               }
               q = d[i+1] - u;
               t = (d[i] - q) * s + 2.0 * c * h;
               u = s * t;
               d[i+1] = q + u;
               q = c * t - h;

               /* form eigenvectors */

#if 0
               for (int ia=0; ia<m; ia++) {
                  p = z[(i+1)*m+ia];
                  z[(i+1)*m+ia] = s * z[i*m+ia] + c * p; 
                  z[i*m+ia] = c * z[i*m+ia] - s * p;
               }
#else
               update(&z[i*m],m,c,s);
#endif
            }
            d[j] -= u;
            e[j] = q;
            e[im] = 0.0;
         }
      }
      if (its == maxit) {
         *info = its;
         break;
      }
   }

   /* order eigenvalues and eigenvectors */

   for (j=0; j<n-1; j++) {
      k = j;
      for (i=j+1; i<n; i++) if (d[i] < d[k]) k = i;
      if (k != j) {
         dswap_(&one,&d[j],&one,&d[k],&one);
         dswap_(&m,&z[j*m],&one,&z[k*m],&one); 
      }
   }
}

/* ******************************************************** */

static double 
absol(double x)
{
 if (x > 0.0)
   return(x);
 else
   return(-x);
}

/* ******************************************************** */
/* Function : transpose matrix                              */
/* -------------------------------------------------------- */

static void
pflip(int id,int n,int m,int p,double *ar,double *ac,double *w,
      const RefMessageGrp& grp)
{
  int i,k,r,dpsize=sizeof(double),one=1;

  i = 0;
  for (k=0; k<n; k++) {
    r = k % p;
    if (id == r) {
      dcopy_(&n,&ar[i],&m,&w[0],&one);
      i++;
    }
    grp->raw_bcast(&w[0], n*dpsize, r);
    dcopy_(&m,&w[id],&p,&ac[k],&n);
  }
}

/* ******************************************************** */
/* Function : calculate machine epslon                      */
/* -------------------------------------------------------- */

static double
epslon (double x) 
{
  double a,b,c,eps; 

  a = 4.0/3.0;
  eps = 0.0;
  while (eps == 0.0) {
    b = a - 1.0; 
    c = 3.0 * b; 
    eps = c-1.0; 
    if (eps < 0.0) eps = - eps;
  }
  if (x < 0.0) a = - x;
  return(eps*a); 
}

static void
update(double *z,int m,double c,double s)
{
  register int i;
  register double p;

  for (i=0; i < m; i++) {
    p = z[i+m];
    z[m+i] = s * z[i] + c * p;
    z[i]   = c * z[i] - s * p;
  }
}

/*******************************************************************/

static void
ptred2_(double *a, int *lda, int *n, int *m, int *p, int *id,
        double *d, double *e, double *z, double *work,
        const RefMessageGrp& grp)
{
  if (*p==1)
    ptred_single(a, lda, n, m, p, id, d, e, z, work);
  else
    ptred_parallel(a, lda, n, m, p, id, d, e, z, work, grp);
}

/* ******************************************************** */
/* Function of this subroutine :                            */ 
/*    tridiagonalize a real, symmetric matrix using         */ 
/*    Householder transformation                            */
/* Parameters :                                             */
/*    a[lda][m] - locally held submatrix                    */
/*    lda - leading dimension of array a                    */
/*    n   - size of the matrix a                            */
/*    m   - number of locally held columns                  */
/*    p   - number of nodes used                            */
/*    id  - my node id                                      */
/*  on return :                                             */
/*    d[n]    - the diagonal of the tridiagonal result      */
/*    e[n]    - the offdiagonal of the result(e[1]-e[n-1])  */
/*    z[m][n] - m rows of transformation matrix             */
/*    matrix a will be destroyed                            */
/* -------------------------------------------------------- */

static void
ptred_single(double *a,int *lda,int *n,int *m,int *p,int *id,
             double *d,double *e,double *z,double *work)
{
   double  alpha=0.0, beta, gamma, alpha2; 
   double  oobeta;
   int     i,j,k,l,ld,r;
   int     slda, sn, sm, sp, sid, q, inc=1;

   /* extract parameters and get  cube information */

   slda = *lda;
   sn = *n;
   sm = *m;
   sp = *p;
   sid = *id;

   /* initialize eigenvector matrix to be identity */

   i = sn * sm;
   alpha2 = 0.0;
   j = 0;
   dcopy_(&i,&alpha2,&j,&z[0],&inc);
   ld = sid;
   for (i=0; i<sm; i++) {
      z[ld*sm+i] = 1.0;
      ld += sp; 
   }

   /* start reduction - one column at a time */

   l = 0;
   ld = sid;
   d[0] = 0.0;
   e[0] = 0.0;
   if (sid == 0) d[0] = a[0];
   for (k=1; k<=sn-1; k++) {
      r = (k-1) % sp;

      /* Use a Householder reflection to zero a(i,k), i = k+2,..., n .*/
      /* Let  a = (0, ..., 0, a(k+1,k) ... a(n,k))',                  */
      /*      u =  a/norm(a) + (k+1-st unit vector),                  */
      /*      beta = -u(k+1) = -norm(u)**2/2 ,                        */
      /*      H = I + u*u'/beta .                                     */
      /* Replace  A  by  H*A*H .                                      */
      /* Store u in D(K+1) through D(N) .                             */
      /* The root node, r, is the owner of column k.                  */

      if (sid == r) {
         q = sn - k;      
         alpha = dnrm2_(&q,&a[l*slda+k],&inc);
         if (a[l*slda+k] < 0.0) alpha = -alpha;
         if (alpha != 0.0) {
            alpha2 = 1.0 / alpha;
            dscal_(&q,&alpha2,&a[l*slda+k],&inc);
            a[l*slda+k] += 1.0;
         }
         dcopy_(&q,&a[l*slda+k],&inc,&d[k],&inc);
         l++;
         ld += sp;
      }

      beta = -d[k];
      if (beta != 0.0) {

         /* Symmetric matrix times vector,  v = A*u.*/
         /* Store  v  in  E(K+1) through E(N) .     */

         alpha2 = 0.0;
         j = 0;
         q = sn - k;
         dcopy_(&q,&alpha2,&j,&e[k],&inc);
         i = ld;
         for (j=l; j<sm; j++) {
            q = sn - i;
            e[i] = e[i] + ddot_(&q,&a[j*slda+i],&inc,&d[i],&inc);
            q = sn - i - 1;
            daxpy_(&q,&d[i],&a[slda*j+i+1],&inc,&e[i+1],&inc);
            i += sp;
         }

         /* v = v/beta            */
         /* gamma = v'*u/(2*beta) */
         /* v = v + gamma*u       */

         if (sid == r) {
            q = sn - k;
            alpha2 = 1.0 / beta;
            dscal_(&q,&alpha2,&e[k],&inc);
            gamma = 0.5*ddot_(&q,&d[k],&inc,&e[k],&inc)/beta;
            daxpy_(&q,&gamma,&d[k],&inc,&e[k],&inc);
         }

         /* Rank two update of A, compute only lower half. */
         /* A  =  A + u'*v + v'*u  =  H*A*H                */

         i = ld;
         for (j=l; j<sm; j++) {
            q = sn - i;
            daxpy_(&q,&d[i],&e[i],&inc,&a[j*slda+i],&inc);
            daxpy_(&q,&e[i],&d[i],&inc,&a[j*slda+i],&inc);
            i += sp;
         }
         q = sn - k;
         oobeta=1.0/beta;
         for (i=0; i<sm; i++) {
            gamma = ddot_(&q,&d[k],&inc,&z[k*sm+i],&sm)*oobeta;
            daxpy_(&q,&gamma,&d[k],&inc,&z[k*sm+i],&sm);
         }
      }

      d[k] = 0.0;
      e[k] = 0.0;
      if (sid == (k % sp)) d[k] = a[l*slda+ld];
      if (sid == r) e[k] = -alpha;
   }
   r = 0;

}

/*
 * Function of this subroutine :
 * tridiagonalize a real, symmetric matrix using
 * Householder transformation
 *
 * Parameters :
 *   a[lda][m] - locally held submatrix
 *   lda - leading dimension of array a
 *   n   - size of the matrix a
 *   m   - number of locally held columns
 *   p   - number of nodes used
 *   id  - my node id
 *
 * on return :
 *   d[n]    - the diagonal of the tridiagonal result
 *   e[n]    - the offdiagonal of the result(e[1]-e[n-1])
 *   z[m][n] - m rows of transformation matrix
 *   matrix a will be destroyed 
 *
 * merge C code from libdmt with FORTRAN code modified by R. Chamberlain
 * FORTRAN COMMENTS:
 *    This version dated 5/4/92
 *    Richard Chamberlain, Intel Supercomputer Systems Division
 *    Improvements:
 *      1. gdcomb of Robert van de Geijn used.
 *      2. look-ahead distribution of Householder vector. Here the node
 *        containing the next Householder vector defers updating the
 *        eigenvector matrix until the next Householder vector is sent.
 */

static void
ptred_parallel(double *a, int *lda, int *n, int *m, int *p, int *id,
               double *d, double *e, double *z, double *work,
               const RefMessageGrp& grp)
{
  int i, j, k, l, ld, r, dpsize = sizeof(double);
  int kp1l;
  int slda, sn, sm, sp, sid, q, inc = 1;
  double alpha=0.0, beta=0.0, gamma, alpha2;
  double oobeta, atemp;

  /* extract parameters and get  cube information */

  slda = *lda;
  sn = *n;
  sm = *m;
  sp = *p;
  sid = *id;

  /* initialize eigenvector matrix to be identity */

  i = sn * sm;
  alpha2 = 0.0;
  j = 0;
  dcopy_(&i, &alpha2, &j, &z[0], &inc);
  ld = sid;
  for (i = 0; i < sm; i++) {
    z[ld * sm + i] = 1.0;
    ld += sp;
    }

  /* start reduction - one column at a time */

  l = 0;
  ld = sid;
  d[0] = 0.0;
  e[0] = 0.0;
  if (sid == 0) d[0] = a[0];

  for (k = 1; k <= sn - 1; k++) {

    /* Use a Householder reflection to zero a(i,k), i = k+2,..., n .
     * Let  a = (0, ..., 0, a(k+1,k) ... a(n,k))',
     * u =  a/norm(a) + (k+1-st unit vector),
     * beta = -u(k+1) = -norm(u)**2/2,
     * H = I + u*u'/beta.
     * Replace  A  by  H*A*H.
     * Store u in D(K+1) through D(N).
     * The root node, r, is the owner of column k.                  
     */

    r = (k - 1) % sp;
    if (sid == r) {
      kp1l=l*slda+k;
      q = sn - k;
      atemp = a[l * slda + ld];
      alpha = dnrm2_(&q, &a[kp1l], &inc);
      if (a[kp1l] < 0.0) alpha = -alpha;
      if (alpha != 0.0) {
	alpha2 = 1.0 / alpha;
	dscal_(&q, &alpha2, &a[kp1l], &inc);
	a[kp1l] += 1.0;
        }

      grp->raw_bcast(&a[kp1l], (sn - k) * dpsize, r);

   /* this is the deferred update of the eigenvector matrix. It was
    * deferred from the last step to accelerate the sending of the Householder
    * vector. Don't do this on the first step.
    */
      if (k != 1) {
	int ik = k - 1; /* ik is a temporary index to the previous step */
	int nmik = sn - ik;

	if (beta != 0.0) {
	  for (i = 0; i < sm; i++) {
	    gamma = ddot_(&nmik, &d[ik], &inc, &z[ik * sm + i], &sm) / beta;
	    daxpy_(&nmik, &gamma, &d[ik], &inc, &z[ik * sm + i], &sm);
	    }
	  }
	e[ik] = 0.0;
	d[ik] = atemp;
        }

   /* now resume normal service */
      dcopy_(&q, &a[kp1l], &inc, &d[k], &inc);
      l++;
      ld += sp;
      }
    else {
      grp->raw_bcast(&d[k], (sn - k) * dpsize, r);
      }

    beta = -d[k];
    if (beta != 0.0) {

      /* Symmetric matrix times vector,  v = A*u. */
      /* Store  v  in  E(K+1) through E(N) .     */

      alpha2 = 0.0;
      j = 0;
      q = sn - k;
      dcopy_(&q, &alpha2, &j, &e[k], &inc);
      i = ld;
      for (j = l; j < sm; j++) {
        int ij=j*slda+i;
	q = sn - i;
	e[i] += ddot_(&q, &a[ij], &inc, &d[i], &inc);
	q--;
	daxpy_(&q, &d[i], &a[ij+1], &inc, &e[i + 1], &inc);
	i += sp;
        }
      grp->sum(&e[k], sn-k, work);

      /* v = v/beta
       * gamma = v'*u/(2*beta)
       * v = v + gamma*u
       */

      q = sn - k;
      alpha2 = 1.0 / beta;
      dscal_(&q, &alpha2, &e[k], &inc);
      gamma = 0.5 * ddot_(&q, &d[k], &inc, &e[k], &inc) / beta;
      daxpy_(&q, &gamma, &d[k], &inc, &e[k], &inc);

      /* Rank two update of A, compute only lower half. */
      /* A  =  A + u'*v + v'*u  =  H*A*H                */

      i = ld;
      for (j = l; j < sm; j++) {
        double *atmp= &a[j*slda+i];
	q = sn - i;
	daxpy_(&q, &d[i], &e[i], &inc, atmp, &inc);
	daxpy_(&q, &e[i], &d[i], &inc, atmp, &inc);
	i += sp;
        }

      /*  Accumulate m rows of transformation matrix.
       *  Z = Z*H
       *
       * if I have next column, defer updating
       */

      if (sid != k%sp || k == sn - 1) {
	q = sn - k;
	oobeta = 1.0 / beta;
	for (i = 0; i < sm; i++) {
	  gamma = ddot_(&q, &d[k], &inc, &z[k * sm + i], &sm) * oobeta;
	  daxpy_(&q, &gamma, &d[k], &inc, &z[k * sm + i], &sm);
	  }
        }
      }

   /* another bit of calcs to be deferred */
    if (sid != k%sp || k == sn - 1) {
      d[k] = 0.0;
      e[k] = 0.0;
      if (sid == k%sp) d[k] = a[l * slda + ld];
      if (sid == r) e[k] = -alpha;
      }
    }

  /* collect the whole tridiagonal matrix at every node */

  grp->sum(d, sn, work);
  grp->sum(e, sn, work);
  }
