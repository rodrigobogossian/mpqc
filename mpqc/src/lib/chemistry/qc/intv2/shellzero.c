/* $Log$
 * Revision 1.3  1994/08/26 22:45:53  etseidl
 * fix a bunch of warnings, get rid of rcs id's, get rid of bread/bwrite and
 * fread/fwrite modules
 *
 * Revision 1.2  1993/12/30  13:33:08  etseidl
 * mostly rcs id stuff
 *
 * Revision 1.3  1992/06/17  22:05:19  jannsen
 * cleaned up for saber-c
 *
 * Revision 1.2  1992/03/31  01:24:36  jannsen
 * Merged in Sandia non CVS codes.
 *
 * Revision 1.1  1992/01/17  22:12:11  cljanss
 * Initial revision
 *
 * Revision 1.1  1992/01/17  12:47:20  seidl
 * Initial revision
 * */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <tmpl.h>
#include <util/sgen/sgen.h>

#include "int_macros.h"

#include "atoms.h"

#include "atomszero.h"

void
zero_shell(_shell)
shell_t *_shell;
{
  int nfunc;
  int i;

  if(_shell->nprim!=0) {
    if(_shell->exp!=NULL) {
      bzero(_shell->exp,sizeof(double)*_shell->nprim);
      }
    }

  if(_shell->ncon!=0) {
    if(_shell->type!=NULL) {
      for (i=0; i<_shell->ncon; i++)  {
        zero_shell_type(&(_shell->type[i]));
        }
      }
    }

  if(_shell->ncon!=0) {
    if(_shell->coef!=NULL) {
      for (i=0; i<_shell->ncon; i++)  {
        if(_shell->nprim!=0) {
          if(_shell->coef[i]!=NULL) {
            bzero(_shell->coef[i],sizeof(double)*_shell->nprim);
            }
          }
        }
      }
    }

/* hand coded part for norm */
  if(_shell->ncon!=0) {
    if(_shell->norm!=NULL) {
      for (i=0; i<_shell->ncon; i++)  {
        if((nfunc=INT_NCART(_shell->type[i].am))!=0) {
          if(_shell->norm[i]!=NULL) {
            bzero(_shell->norm[i],sizeof(double)*nfunc);
            }
          }
        }
      }
    }
  }

