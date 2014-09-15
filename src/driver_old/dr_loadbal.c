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

#include <mpi.h>

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>

#include "dr_const.h"
#include "dr_err_const.h"
#include "dr_loadbal_const.h"
#include "dr_eval_const.h"

#ifdef __cplusplus
/* if C++, define the rest of this header file as extern C */
extern "C" {
#endif

static int Num_GID = 1, Num_LID = 1;

/*--------------------------------------------------------------------------*/
/* Purpose: Call Zoltan to determine a new load balance.                    */
/*          Contains all of the callback functions that Zoltan needs        */
/*          for the load balancing.                                         */
/*--------------------------------------------------------------------------*/

/*
 *  PROTOTYPES for load-balancer interface functions.
 */

LB_NUM_OBJ_FN get_num_elements;
/* not used right now --->
LB_OBJ_LIST_FN get_elements;
*/
LB_FIRST_OBJ_FN get_first_element;
LB_NEXT_OBJ_FN get_next_element;

LB_NUM_GEOM_FN get_num_geom;
LB_GEOM_FN get_geom;

LB_NUM_EDGES_FN get_num_edges;
LB_EDGE_LIST_FN get_edge_list;

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/

int setup_zoltan(struct LB_Struct *lb, int Proc, PROB_INFO_PTR prob,
                 MESH_INFO_PTR mesh)
{
/* Local declarations. */
  char *yo = "run_zoltan";

  int i;                         /* Loop index                               */
  int ierr;                      /* Error code                               */
  char errmsg[128];              /* Error message */

  DEBUG_TRACE_START(Proc, yo);

  /* Set the user-specified parameters */
  for (i = 0; i < prob->num_params; i++) {
    ierr = LB_Set_Param(lb, prob->params[i][0], prob->params[i][1]);
    if (ierr == LB_FATAL) {
      sprintf(errmsg,"fatal: error in LB_Set_Param when setting parameter %s\n",
              prob->params[i][0]);
      Gen_Error(0, errmsg);
      return 0;
    }
    if (strcasecmp(prob->params[i][0], "NUM_GID_ENTRIES") == 0) 
      Num_GID = atoi(prob->params[i][1]);
    else if (strcasecmp(prob->params[i][0], "NUM_LID_ENTRIES") == 0) 
      Num_LID = atoi(prob->params[i][1]);
  }

  /* Set the method */
  if (LB_Set_Method(lb, prob->method) == LB_FATAL) {
    Gen_Error(0, "fatal:  error returned from LB_Set_Method()\n");
    return 0;
  }

  /*
   * Set the callback functions
   */

  if (LB_Set_Fn(lb, LB_NUM_OBJ_FN_TYPE, (void (*)()) get_num_elements,
                (void *) mesh) == LB_FATAL) {
    Gen_Error(0, "fatal:  error returned from LB_Set_Fn()\n");
    return 0;
  }

  if (LB_Set_Fn(lb, LB_FIRST_OBJ_FN_TYPE, (void (*)()) get_first_element,
                (void *) mesh) == LB_FATAL) {
    Gen_Error(0, "fatal:  error returned from LB_Set_Fn()\n");
    return 0;
  }

  if (LB_Set_Fn(lb, LB_NEXT_OBJ_FN_TYPE, (void (*)()) get_next_element,
                (void *) mesh) == LB_FATAL) {
    Gen_Error(0, "fatal:  error returned from LB_Set_Fn()\n");
    return 0;
  }

  /* Functions for geometry based algorithms */
  if (LB_Set_Fn(lb, LB_NUM_GEOM_FN_TYPE, (void (*)()) get_num_geom,
                (void *) mesh) == LB_FATAL) {
    Gen_Error(0, "fatal:  error returned from LB_Set_Fn()\n");
    return 0;
  }

  if (LB_Set_Fn(lb, LB_GEOM_FN_TYPE, (void (*)()) get_geom,
                (void *) mesh) == LB_FATAL) {
    Gen_Error(0, "fatal:  error returned from LB_Set_Fn()\n");
    return 0;
  }

  /* Functions for geometry based algorithms */
  if (LB_Set_Fn(lb, LB_NUM_EDGES_FN_TYPE, (void (*)()) get_num_edges,
                (void *) mesh) == LB_FATAL) {
    Gen_Error(0, "fatal:  error returned from LB_Set_Fn()\n");
    return 0;
  }

  if (LB_Set_Fn(lb, LB_EDGE_LIST_FN_TYPE, (void (*)()) get_edge_list,
                (void *) mesh)== LB_FATAL) {
    Gen_Error(0, "fatal:  error returned from LB_Set_Fn()\n");
    return 0;
  }

  DEBUG_TRACE_END(Proc, yo);
  return 1;
}

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/

int run_zoltan(struct LB_Struct *lb, int Proc, PROB_INFO_PTR prob,
               MESH_INFO_PTR mesh)
{
/* Local declarations. */
  char *yo = "run_zoltan";

  /* Variables returned by Zoltan */
  LB_ID_PTR import_gids = NULL;  /* Global node nums of nodes to be imported */
  LB_ID_PTR import_lids = NULL;  /* Pointers to nodes to be imported         */
  int   *import_procs = NULL;    /* Proc IDs of procs owning nodes to be
                                    imported.                                */
  LB_ID_PTR export_gids = NULL;  /* Global node nums of nodes to be exported */
  LB_ID_PTR export_lids = NULL;  /* Pointers to nodes to be exported         */
  int   *export_procs = NULL;    /* Proc IDs of destination procs for nodes
                                    to be exported.                          */
  int num_imported;              /* Number of nodes to be imported.          */
  int num_exported;              /* Number of nodes to be exported.          */
  int new_decomp;                /* Flag indicating whether the decomposition
                                    has changed                              */
  int num_gid_entries;           /* Number of array entries in a global ID.  */
  int num_lid_entries;           /* Number of array entries in a local ID.   */

  int i;                         /* Loop index                               */
  double stime = 0.0, mytime = 0.0, maxtime = 0.0;

/***************************** BEGIN EXECUTION ******************************/

  DEBUG_TRACE_START(Proc, yo);

  /* Evaluate the old balance */
  if (Debug_Driver > 0) {
    if (Proc == 0) printf("\nBEFORE load balancing\n");
    driver_eval(mesh);
    i = LB_Eval(lb, 1, NULL, NULL, NULL, NULL, NULL, NULL);
    if (i) printf("Warning: LB_Eval returned error code %d\n", i);
  }

  /*
   * Call Zoltan
   */
  stime = MPI_Wtime();
  if (LB_Balance(lb, &new_decomp, &num_gid_entries, &num_lid_entries,
                 &num_imported, &import_gids,
                 &import_lids, &import_procs, &num_exported, &export_gids,
                 &export_lids, &export_procs) == LB_FATAL) {
    Gen_Error(0, "fatal:  error returned from LB_Balance()\n");
    return 0;
  }
  mytime = MPI_Wtime() - stime;
  MPI_Allreduce(&mytime, &maxtime, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  if (Proc == 0)
    printf("DRIVER:  Zoltan_Balance time = %g\n", maxtime);

  if (Proc == 0) {
    double x[] = {0.0L, 0.0L, 0.0L} ;
    int proc ;
    int status ;
    status = LB_Point_Assign (lb, x, &proc) ;
    if (status != LB_OK) printf ("Point_Assign returned an error\n") ;
  }

  if (Proc == 0) {
    double xlo, ylo, zlo ;
    double xhi, yhi, zhi ;
    int procs[1000] ;
    int count ;
    int status ;

    xlo = ylo = zlo = 0.0L ;
    xhi = yhi = zhi = 1.0L ;
    status = LB_Box_Assign (lb, xlo, ylo, zlo, xhi, yhi, zhi, procs, &count) ;
    if (status != LB_OK) printf ("Box_Assign returned an error\n") ;
  }

  /*
   * Call another routine to perform the migration
   */
  if (new_decomp) {
    if (!migrate_elements(Proc, mesh, lb, num_gid_entries, num_lid_entries,
                        num_imported, import_gids, import_lids, import_procs,
                        num_exported, export_gids, export_lids, export_procs)) {
      Gen_Error(0, "fatal:  error returned from migrate_elements()\n");
      return 0;
    }
  }

  /* Evaluate the new balance */
  if (Debug_Driver > 0) {
    if (Proc == 0) printf("\nAFTER load balancing\n");
    driver_eval(mesh);
    i = LB_Eval(lb, 1, NULL, NULL, NULL, NULL, NULL, NULL);
    if (i) printf("Warning: LB_Eval returned error code %d\n", i);
  }


  /* Clean up */
  (void) LB_Free_Data(&import_gids, &import_lids, &import_procs,
                      &export_gids, &export_lids, &export_procs);

  DEBUG_TRACE_END(Proc, yo);
  return 1;
}

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
int get_num_elements(void *data, int *ierr)
{
MESH_INFO_PTR mesh;

  if (data == NULL) {
    *ierr = LB_FATAL;
    return 0;
  }
  mesh = (MESH_INFO_PTR) data;

  *ierr = LB_OK; /* set error code */

  return(mesh->num_elems);
}

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
int get_first_element(void *data, int num_gid_entries, int num_lid_entries,
                      LB_ID_PTR global_id, LB_ID_PTR local_id,
                      int wdim, float *wgt, int *ierr)
{
  MESH_INFO_PTR mesh;
  ELEM_INFO *elem;
  ELEM_INFO *current_elem;
  int i;
  int gid = num_gid_entries-1;
  int lid = num_lid_entries-1;

 *ierr = LB_OK; 

  if (data == NULL) {
    *ierr = LB_FATAL;
    return 0;
  }
  
  mesh = (MESH_INFO_PTR) data;
  if (mesh->num_elems == 0) {
    /* No elements on this processor */
    return 0;
  }

  elem = mesh->elements;
  current_elem = &elem[0];
  if (num_lid_entries) local_id[lid] = 0;
  global_id[gid] = (LB_ID_TYPE) current_elem->globalID;

  if (wdim>0){
    for (i=0; i<wdim; i++){
      *wgt++ = current_elem->cpu_wgt[i];
      /* printf("Debug: In query function, object = %d, weight no. %1d = %f\n",
             global_id[gid], i, current_elem->cpu_wgt[i]); */
    }
  }

  return 1;
}

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
int get_next_element(void *data, int num_gid_entries, int num_lid_entries,
                     LB_ID_PTR global_id, LB_ID_PTR local_id,
                     LB_ID_PTR next_global_id, LB_ID_PTR next_local_id, 
                     int wdim, float *next_wgt, int *ierr)
{
  int found = 0;
  ELEM_INFO *elem;
  ELEM_INFO *next_elem;
  MESH_INFO_PTR mesh;
  int i, idx;
  int gid = num_gid_entries-1;
  int lid = num_lid_entries-1;

  if (data == NULL) {
    *ierr = LB_FATAL;
    return 0;
  }
  
  mesh = (MESH_INFO_PTR) data;
  elem = mesh->elements;

  if (num_lid_entries) {
    idx = local_id[lid];
  }
  else {
    /* testing zero-length local IDs; search by global ID for current elem */
    (void) search_by_global_id(mesh, global_id[gid], &idx);
  }

  if (idx+1 < mesh->num_elems) { 
    found = 1;
    if (num_lid_entries) next_local_id[lid] = idx + 1;
    next_elem = &elem[idx+1];
    next_global_id[gid] = next_elem->globalID;

    if (wdim>0){
      for (i=0; i<wdim; i++){
        *next_wgt++ = next_elem->cpu_wgt[i];
      }
    }

    *ierr = LB_OK; 
  }

  return(found);
}

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
int get_num_geom(void *data, int *ierr)
{
  MESH_INFO_PTR mesh;

  if (data == NULL) {
    *ierr = LB_FATAL;
    return 0;
  }
  mesh = (MESH_INFO_PTR) data;

  *ierr = LB_OK; /* set error flag */

  return(mesh->num_dims);
}

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
void get_geom(void *data, int num_gid_entries, int num_lid_entries,
              LB_ID_PTR global_id, LB_ID_PTR local_id,
              double *coor, int *ierr)
{
  ELEM_INFO *elem;
  ELEM_INFO *current_elem;
  int i, j, idx;
  double tmp;
  MESH_INFO_PTR mesh;
  int gid = num_gid_entries-1;
  int lid = num_lid_entries-1;

  if (data == NULL) {
    *ierr = LB_FATAL;
    return;
  }
  mesh = (MESH_INFO_PTR) data;
  elem = mesh->elements;
  current_elem = (num_lid_entries 
                    ? &elem[local_id[lid]] 
                    : search_by_global_id(mesh, global_id[gid], &idx));

  if (mesh->eb_nnodes[current_elem->elem_blk] == 0) {
    /* No geometry info was read. */
    *ierr = LB_FATAL;
    return;
  }
  
  /*
   * calculate the geometry of the element by averaging
   * the coordinates of the nodes in its connect table
   */
  for (i = 0; i < mesh->num_dims; i++) {
    tmp = 0.0;
    for (j = 0; j < mesh->eb_nnodes[current_elem->elem_blk]; j++)
      tmp += current_elem->coord[j][i];

    coor[i] = tmp / mesh->eb_nnodes[current_elem->elem_blk];
  }

  *ierr = LB_OK;
}

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
int get_num_edges(void *data, int num_gid_entries, int num_lid_entries,
                  LB_ID_PTR global_id, LB_ID_PTR local_id, int *ierr)
{
  MESH_INFO_PTR mesh;
  ELEM_INFO *elem, *current_elem;
  int gid = num_gid_entries-1;
  int lid = num_lid_entries-1;
  int idx;

  if (data == NULL) {
    *ierr = LB_FATAL;
    return 0;
  }
  mesh = (MESH_INFO_PTR) data;
  elem = mesh->elements;

  *ierr = LB_OK;

  current_elem = (num_lid_entries 
                    ? &elem[local_id[lid]] 
                    : search_by_global_id(mesh, global_id[gid], &idx));

  return(current_elem->nadj);
}

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
void get_edge_list (void *data, int num_gid_entries, int num_lid_entries, 
                   LB_ID_PTR global_id, LB_ID_PTR local_id,
                   LB_ID_PTR nbor_global_id, int *nbor_procs,
                   int get_ewgts, float *nbor_ewgts, int *ierr)
{
  MESH_INFO_PTR mesh;
  ELEM_INFO *elem;
  ELEM_INFO *current_elem;
  int i, j, proc, local_elem, idx;
  int gid = num_gid_entries-1;
  int lid = num_lid_entries-1;

  if (data == NULL) {
    *ierr = LB_FATAL;
    return;
  }

  mesh = (MESH_INFO_PTR) data;
  elem = mesh->elements;
  current_elem = (num_lid_entries
                   ? &elem[local_id[lid]] 
                   : search_by_global_id(mesh, global_id[gid], &idx));

  /* get the processor number */
  MPI_Comm_rank(MPI_COMM_WORLD, &proc);

  j = 0;
  for (i = 0; i < current_elem->adj_len; i++) {

    /* Skip NULL adjacencies (sides that are not adjacent to another elem). */
    if (current_elem->adj[i] == -1) continue;

    if (current_elem->adj_proc[i] == proc) {
      local_elem = current_elem->adj[i];
      nbor_global_id[gid+j*num_gid_entries] = elem[local_elem].globalID;
    }
    else { /* adjacent element on another processor */
      nbor_global_id[gid+j*num_gid_entries] = current_elem->adj[i];
    }
    nbor_procs[j] = current_elem->adj_proc[i];

    if (get_ewgts) {
      if (current_elem->edge_wgt == NULL)
        nbor_ewgts[j] = 1.0; /* uniform weights is default */
      else
        nbor_ewgts[j] = current_elem->edge_wgt[i];
    }
    j++;
  }

  *ierr = LB_OK;
}
/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
ELEM_INFO *search_by_global_id(MESH_INFO *mesh, int global_id, int *idx)
{
/*
 * Function that searchs for an element based upon its global ID.
 * This function does not provide the most efficient implementation of
 * the query functions; more efficient implementation uses local IDs
 * to directly access element info.  However, this function is useful
 * for testing Zoltan when the number of entries in a local ID 
 * (NUM_LID_ENTRIES) is zero.
 */

int i;
ELEM_INFO *elem, *found_elem = NULL;


  elem = mesh->elements;

  for (i = 0; i < mesh->elem_array_len; i++)
    if (elem[i].globalID == global_id) {
      found_elem = &elem[i];
      *idx = i;
      break;
    }
  
  return(found_elem);
}

#ifdef __cplusplus
} /* closing bracket for extern "C" */
#endif
