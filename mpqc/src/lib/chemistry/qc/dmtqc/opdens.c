
/* $Log$
 * Revision 1.2  1994/08/26 22:48:00  etseidl
 * get rid of rcsids
 *
 * Revision 1.1.1.1  1993/12/29  12:53:00  etseidl
 * SC source tree 0.1
 *
 * Revision 1.2  1992/06/17  21:37:21  jannsen
 * cleaned up for saber-c and converted to ngl loops
 *
 * Revision 1.1.1.1  1992/03/17  14:56:52  seidl
 * DOE-NIH Quantum Chemistry Library 0.0
 *
 * Revision 1.1  1992/03/17  14:56:51  seidl
 * Initial revision
 *
 * Revision 1.1  1992/02/13  00:46:55  seidl
 * Initial revision
 * */

#include <stdio.h>
#include <tmpl.h>
#include <math/dmt/libdmt.h>

#include "opdens.gbl"
#include "opdens.lcl"

/* This computes the open-shell density */

GLOBAL_FUNCTION VOID
dmt_open_density (C, ndoc, nsoc, PO)
dmt_matrix C;
int ndoc;
int nsoc;
dmt_matrix PO;
{
  double *col;
  int i;
  loop_t *loop;
  int iind,isize,jsize;

  dmt_fill (PO, 0.0);
  loop = dmt_ngl_create("%mr",C);
  while(dmt_ngl_next(loop)) {
    dmt_ngl_create_inner(loop,0);
    while(dmt_ngl_next_inner_m(loop,&iind,&isize,&i,&jsize,&col)) {
      if (i >= ndoc && i < ndoc+nsoc) {
        /*Sum contribution from |col| into local |PO| blocks.*/
        int localp = dmt_nlocal (PO);
        int il;
        int mub, nub, mu, nu, mustart, nustart, musize, nusize;
        double *block;
        for (il = 0; il<localp; il++) {
          dmt_get_block (PO, il, &mub, &nub, &block);
          dmt_describe_block (PO, mub, &mustart, &musize);
          dmt_describe_block (PO, nub, &nustart, &nusize);
          for (mu = 0; mu<musize; mu++) {
            for (nu = 0; nu<nusize; nu++) {
              block[mu*nusize+nu] += col[nustart+nu]*col[mustart+mu];
              }
            }
          }
        }
      }
    }
  dmt_ngl_kill(loop);

  }
