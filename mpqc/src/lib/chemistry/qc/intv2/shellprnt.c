/* $Log$
 * Revision 1.3  1994/08/26 22:45:45  etseidl
 * fix a bunch of warnings, get rid of rcs id's, get rid of bread/bwrite and
 * fread/fwrite modules
 *
 * Revision 1.2  1993/12/30  13:33:04  etseidl
 * mostly rcs id stuff
 *
 * Revision 1.3  1992/06/17  22:05:06  jannsen
 * cleaned up for saber-c
 *
 * Revision 1.2  1992/03/31  01:23:39  jannsen
 * Merged in Sandia non CVS codes.
 *
 * Revision 1.1  1992/01/17  22:12:11  cljanss
 * Initial revision
 *
 * Revision 1.1  1992/01/17  12:47:20  seidl
 * Initial revision
 * */

#include <stdio.h>
#include <math.h>
#include <tmpl.h>
#include <util/sgen/sgen.h>

#include "int_macros.h"

#include "atoms.h"

#include "atomsprnt.h"

extern int sgen_print_nindent;
#define SPI sgen_print_indent(fp)

void
print_shell(fp,_shell)
FILE *fp;
shell_t *_shell;
{
  int nfunc;
  int i,j;
  int orig_indent = sgen_print_nindent;

  fprintf(fp,"nprim = ");
  sgen_print_nindent += 8;
  print_int(fp,&(_shell->nprim));
  fprintf(fp,"\n");
  sgen_print_nindent = orig_indent;
  SPI;

  fprintf(fp,"ncon = ");
  sgen_print_nindent += 7;
  print_int(fp,&(_shell->ncon));
  fprintf(fp,"\n");
  sgen_print_nindent = orig_indent;
  SPI;

  fprintf(fp,"nfunc = ");
  sgen_print_nindent += 8;
  print_int(fp,&(_shell->nfunc));
  fprintf(fp,"\n");
  sgen_print_nindent = orig_indent;
  SPI;

  fprintf(fp,"exp = ");
  sgen_print_nindent += 6;
  fprintf(fp,"[");
  sgen_print_nindent += 1;
  for (i=0; i<_shell->nprim; i++)  {
    print_double(fp,&(_shell->exp[i]));
    if(!((i+1)%8) && i) {
      fprintf(fp,"\n");
      SPI;
      }
    }
  fprintf(fp,"]\n");
  sgen_print_nindent += -1;
  sgen_print_nindent = orig_indent;
  SPI;

  fprintf(fp,"type:");
  sgen_print_nindent += 5;
  fprintf(fp,"[");
  sgen_print_nindent += 1;
  for (i=0; i<_shell->ncon; i++)  {
    if (i!=0) SPI;
    fprintf(fp,"(");
    sgen_print_nindent += 1;
    print_shell_type(fp,&(_shell->type[i]));
    SPI;
    fprintf(fp,")\n");
    sgen_print_nindent += -1;
    if(!((i+1)%8) && i) {
      fprintf(fp,"\n");
      SPI;
      }
    }
  SPI;
  fprintf(fp,"]\n");
  sgen_print_nindent += -1;
  sgen_print_nindent = orig_indent;
  SPI;

  fprintf(fp,"coef = ");
  sgen_print_nindent += 7;
  fprintf(fp,"[");
  sgen_print_nindent += 1;
  for (i=0; i<_shell->ncon; i++)  {
    if (i!=0) SPI;
    fprintf(fp,"[");
    sgen_print_nindent += 1;
    for (j=0; j<_shell->nprim; j++)  {
      print_double(fp,&(_shell->coef[i][j]));
      if(!((j+1)%8) && j) {
        fprintf(fp,"\n");
        SPI;
        }
      }
    fprintf(fp,"]\n");
    sgen_print_nindent += -1;
    }
  SPI;
  fprintf(fp,"]\n");
  sgen_print_nindent += -1;
  sgen_print_nindent = orig_indent;
  SPI;

/* hand coded part for norm */
  if (_shell->norm) {
    fprintf(fp,"norm = ");
    sgen_print_nindent += 7;
    fprintf(fp,"[");
    sgen_print_nindent += 1;
    for (i=0; i<_shell->ncon; i++)  {
      if (i!=0) SPI;
      fprintf(fp,"[");
      sgen_print_nindent += 1;
      nfunc=INT_NCART(_shell->type[i].am);
      for (j=0; j<nfunc; j++)  {
        print_double(fp,&(_shell->norm[i][j]));
        if(!((j+1)%8) && j) {
          fprintf(fp,"\n");
          SPI;
          }
        }
      fprintf(fp,"]\n");
      sgen_print_nindent += -1;
      }
    SPI;
    fprintf(fp,"]\n");
    sgen_print_nindent += -1;
    sgen_print_nindent = orig_indent;
    SPI;
    }
  }
