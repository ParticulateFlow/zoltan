/*****************************************************************************
 * Zoltan Library for Parallel Applications                                  *
 * Copyright (c) 2000,2001,2002, Sandia National Laboratories.               *
 * For more info, see the README file in the top-level Zoltan directory.     *  
 *****************************************************************************/
/*****************************************************************************
 * CVS File Information :
 *    $RCSfile$
 *    $Author$
 *    $Date$
 *    Revision$
 ****************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "dr_const.h"
#include "dr_util_const.h"
#include "dr_par_util_const.h"
#include "dr_err_const.h"
#include "dr_output_const.h"

#ifdef __cplusplus
/* if C++, define the rest of this header file as extern C */
extern "C" {
#endif


/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/

/*--------------------------------------------------------------------------*/
/* Purpose: Output the element assignments in gnuplot format.               */
/*--------------------------------------------------------------------------*/
int output_gnu(char *cmd_file,
               char *tag,
               int Proc,
               int Num_Proc,
               PROB_INFO_PTR prob,
               PARIO_INFO_PTR pio_info,
               MESH_INFO_PTR mesh)
/*
 * For 2D problems, output files that can be read by gnuplot for looking at
 * results.
 * We'll do 3D problems later.
 *
 * One gnuplot file is written for each processor.  

 * For Chaco input files, the file written contains coordinates of owned
 * nodes and all nodes on that processor connected to the owned nodes. When
 * drawn "with linespoints", the subdomains are drawn, but lines connecting the
 * subdomains are not drawn.
 *
 * For Nemesis input files, the file written contains the coordinates of
 * each node of owned elements.  When drawn "with lines", the element outlines
 * for each owned element are drawn.
 *
 * In addition, processor 0 writes a gnuplot command file telling gnuplot how
 * to process the individual coordinate files written.  This file can be used
 * with the gnuplot "load" command to simplify generation of the gnuplot.
 */
{
  /* Local declarations. */
  char  *yo = "output_gnu";
  char   par_out_fname[FILENAME_MAX+1], ctemp[FILENAME_MAX+1];
  ELEM_INFO *current_elem, *nbor_elem;
  int    nbor, num_nodes;
  char  *datastyle;
  int    i, j;

  FILE  *fp;
/***************************** BEGIN EXECUTION ******************************/

  DEBUG_TRACE_START(Proc, yo);

  if (mesh->num_dims > 2) {
    Gen_Error(0, "warning: cannot generate gnuplot data for 3D problems.");
    DEBUG_TRACE_END(Proc, yo);
    return 0;
  }

  if (mesh->eb_nnodes[0] == 0) {
    /* No coordinate information is available.  */
    Gen_Error(0, "warning: cannot generate gnuplot data when no coordinate"
                 " input is given.");
    DEBUG_TRACE_END(Proc, yo);
    return 0;
  }

  /* generate the parallel filename for this processor */
  strcpy(ctemp, pio_info->pexo_fname);
  strcat(ctemp, ".");
  strcat(ctemp, tag);
  strcat(ctemp, ".gnu");
  gen_par_filename(ctemp, par_out_fname, pio_info, Proc, Num_Proc);

  fp = fopen(par_out_fname, "w");

  if (pio_info->file_type == CHACO_FILE) {
    /* 
     * For each node of Chaco graph, print the coordinates of the node.
     * Then, for each neighboring node on the processor, print the neighbor's
     * coordinates.
     */
    datastyle = "linespoints";
    for (i = 0; i < mesh->elem_array_len; i++) {
      current_elem = &(mesh->elements[i]);
      if (current_elem->globalID >= 0) {
        /* Include the point itself, so that even if there are no edges,
         * the point will appear.  */
        fprintf(fp, "\n%e %e\n", 
                current_elem->coord[0][0], current_elem->coord[0][1]);
        for (j = 0; j < current_elem->nadj; j++) {
          if (current_elem->adj_proc[j] == Proc) {
            /* Plot the edge.  Need to include current point and nbor point
             * for each edge. */
            fprintf(fp, "\n%e %e\n", 
                current_elem->coord[0][0], current_elem->coord[0][1]);
            nbor = current_elem->adj[j];
            nbor_elem = &(mesh->elements[nbor]);
            fprintf(fp, "%e %e\n",
                    nbor_elem->coord[0][0], nbor_elem->coord[0][1]);
          }
        }
      }
    }
  }
  else { /* Nemesis input file */
    /* 
     *  For each element of Nemesis input file, print the coordinates of its
     *  nodes.  No need to follow neighbors, as decomposition is by elements.
     */
    datastyle = "lines";
    for (i = 0; i < mesh->elem_array_len; i++) {
      current_elem = &(mesh->elements[i]);
      if (current_elem->globalID >= 0) {
        num_nodes = mesh->eb_nnodes[current_elem->elem_blk];
        for (j = 0; j < num_nodes; j++) {
          fprintf(fp, "%e %e\n", 
                  current_elem->coord[j][0], current_elem->coord[j][1]);
        }
        fprintf(fp, "\n");
      }
    }
  }
    
  fclose(fp);

  if (Proc == 0) {
    /* Write gnu master file with gnu commands for plotting */
    strcpy(ctemp, pio_info->pexo_fname);
    strcat(ctemp, ".");
    strcat(ctemp, tag);
    strcat(ctemp, ".gnuload");
    fp = fopen(ctemp, "w");
    fprintf(fp, "set nokey\n");
    fprintf(fp, "set nolabel\n");
    fprintf(fp, "set noxzeroaxis\n");
    fprintf(fp, "set noyzeroaxis\n");
    fprintf(fp, "set noxtics\n");
    fprintf(fp, "set noytics\n");
    fprintf(fp, "set data style %s\n", datastyle);

    fprintf(fp, "plot ");
    strcpy(ctemp, pio_info->pexo_fname);
    strcat(ctemp, ".");
    strcat(ctemp, tag);
    strcat(ctemp, ".gnu");
    for (i = 0; i < Num_Proc; i++) {
      gen_par_filename(ctemp, par_out_fname, pio_info, i, Num_Proc);
      fprintf(fp, "\"%s\"", par_out_fname);
      if (i != Num_Proc-1) {
        fprintf(fp, ",\\\n");
      }
    }
    fprintf(fp, "\n");
    fclose(fp);
  }

  DEBUG_TRACE_END(Proc, yo);
  return 1;
}

#ifdef __cplusplus
} /* closing bracket for extern "C" */
#endif
