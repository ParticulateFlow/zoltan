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
#include <string.h>

#include "dr_const.h"
#include "dr_input_const.h"
#include "dr_elem_const.h"
#include "dr_util_const.h"
#include "dr_par_util_const.h"
#include "dr_err_const.h"
#include "dr_output_const.h"
#include "dr_elem_util_const.h"

#ifdef ZOLTAN_NEMESIS
#include "exodusII.h"
#include "ne_nemesisI.h"
#endif /* ZOLTAN_NEMESIS */

#ifdef __cplusplus
/* if C++, define the rest of this header file as extern C */
extern "C" {
#endif


#define LIST_ALLOC 10

#ifdef ZOLTAN_NEMESIS
static int read_elem_info(int, int, PROB_INFO_PTR, MESH_INFO_PTR);
static int find_surnd_elem(MESH_INFO_PTR, int **, int *, int *);
static int find_adjacency(int, MESH_INFO_PTR, int **, int *, int);
static int read_comm_map_info(int, int, PROB_INFO_PTR, MESH_INFO_PTR);
#endif /* ZOLTAN_NEMESIS */

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

int read_exoII_mesh(int Proc,
                    int Num_Proc,
                    PROB_INFO_PTR prob,
                    PARIO_INFO_PTR pio_info,
                    MESH_INFO_PTR mesh)
{
#ifndef ZOLTAN_NEMESIS
  Gen_Error(0, "Fatal:  Nemesis requested but not linked with driver.");
  return 0;

#else /* ZOLTAN_NEMESIS */
  /* Local declarations. */
  char  *yo = "read_exoII_mesh";
  char   par_nem_fname[FILENAME_MAX+1], title[MAX_LINE_LENGTH+1];
  char   cmesg[256];

  float  ver;

  int    i, pexoid, cpu_ws = 0, io_ws = 0;
  int   *nnodes = NULL, *etypes = NULL;
#ifdef DEBUG_EXO
  int    j, k, elem;
#endif
  FILE  *fdtmp;

/***************************** BEGIN EXECUTION ******************************/

  DEBUG_TRACE_START(Proc, yo);

  /* since this is a test driver, set error reporting in exodus */
  ex_opts(EX_VERBOSE | EX_DEBUG);

  /* generate the parallel filename for this processor */
  gen_par_filename(pio_info->pexo_fname, par_nem_fname, pio_info, Proc,
                   Num_Proc);

  /* 
   * check whether parallel file exists.  do the check with fopen 
   * as ex_open coredumps on the paragon when files do not exist.
   */

  if ((fdtmp = fopen(par_nem_fname, "r")) == NULL) {
    sprintf(cmesg,"fatal: parallel Exodus II file %s does not exist",
            par_nem_fname);
    Gen_Error(0, cmesg);
    return 0;
  }
  else
    fclose(fdtmp);

  /*
   * now open the existing parallel file using Exodus calls.
   */

  if ((pexoid = ex_open(par_nem_fname, EX_READ, &cpu_ws, &io_ws,
                        &ver)) < 0) {
    sprintf(cmesg,"fatal: could not open parallel Exodus II file %s",
            par_nem_fname);
    Gen_Error(0, cmesg);
    return 0;
  }

  /* and get initial information */
  if (ex_get_init(pexoid, title, &(mesh->num_dims),
                  &(mesh->num_nodes), &(mesh->num_elems),
                  &(mesh->num_el_blks), &(mesh->num_node_sets),
                  &(mesh->num_side_sets)) < 0) {
    Gen_Error(0, "fatal: Error returned from ex_get_init");
    return 0;
  }


  /* alocate some memory for the element blocks */
  mesh->eb_etypes = (int *) malloc (5 * mesh->num_el_blks * sizeof(int));
  if (!mesh->eb_etypes) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }
  mesh->eb_ids = mesh->eb_etypes + mesh->num_el_blks;
  mesh->eb_cnts = mesh->eb_ids + mesh->num_el_blks;
  mesh->eb_nnodes = mesh->eb_cnts + mesh->num_el_blks;
  mesh->eb_nattrs = mesh->eb_nnodes + mesh->num_el_blks;

  mesh->eb_names = (char **) malloc (mesh->num_el_blks * sizeof(char *));
  if (!mesh->eb_names) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }

  if (ex_get_elem_blk_ids(pexoid, mesh->eb_ids) < 0) {
    Gen_Error(0, "fatal: Error returned from ex_get_elem_blk_ids");
    return 0;
  }

  /* allocate temporary storage for items needing global reduction.   */
  /* nemesis does not store most element block info about blocks for  */
  /* which the processor owns no elements.                            */
  /* we, however, use this information in migration, so we need to    */
  /* accumulate it for all element blocks.    kdd 2/2001              */

  if (mesh->num_el_blks > 0) {
    nnodes = (int *) malloc(2 * mesh->num_el_blks * sizeof(int));
    if (!nnodes) {
      Gen_Error(0, "fatal: insufficient memory");
      return 0;
    }
    etypes = nnodes + mesh->num_el_blks;
  }

  /* get the element block information */
  for (i = 0; i < mesh->num_el_blks; i++) {

    /* allocate space for name */
    mesh->eb_names[i] = (char *) malloc((MAX_STR_LENGTH+1) * sizeof(char));
    if (!mesh->eb_names[i]) {
      Gen_Error(0, "fatal: insufficient memory");
      return 0;
    }

    if (ex_get_elem_block(pexoid, mesh->eb_ids[i], mesh->eb_names[i],
                          &(mesh->eb_cnts[i]), &(nnodes[i]),
                          &(mesh->eb_nattrs[i])) < 0) {
      Gen_Error(0, "fatal: Error returned from ex_get_elem_block");
      return 0;
    }

    if (mesh->eb_cnts[i] > 0) {
      if ((etypes[i] =  (int) get_elem_type(mesh->eb_names[i],
                                            nnodes[i],
                                            mesh->num_dims)) == E_TYPE_ERROR) {
        Gen_Error(0, "fatal: could not get element type");
        return 0;
      }
    }
    else etypes[i] = (int) NULL_EL;
  }

  /* Perform reduction on necessary fields of element blocks.  kdd 2/2001 */
  MPI_Allreduce(nnodes, mesh->eb_nnodes, mesh->num_el_blks, MPI_INT, MPI_MAX, 
                MPI_COMM_WORLD);
  MPI_Allreduce(etypes, mesh->eb_etypes, mesh->num_el_blks, MPI_INT, MPI_MIN, 
                MPI_COMM_WORLD);
  for (i = 0; i < mesh->num_el_blks; i++) {
    strcpy(mesh->eb_names[i], get_elem_name(mesh->eb_etypes[i]));
  }
  free(nnodes);

  /*
   * allocate memory for the elements
   * allocate a little extra for element migration latter
   */
  mesh->elem_array_len = mesh->num_elems + 5;
  mesh->elements = (ELEM_INFO_PTR) malloc (mesh->elem_array_len 
                                         * sizeof(ELEM_INFO));
  if (!(mesh->elements)) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }

  /*
   * intialize all of the element structs as unused by
   * setting the globalID to -1
   */
  for (i = 0; i < mesh->elem_array_len; i++) 
    initialize_element(&(mesh->elements[i]));

  /* read the information for the individual elements */
  if (!read_elem_info(pexoid, Proc, prob, mesh)) {
    Gen_Error(0, "fatal: Error returned from read_elem_info");
    return 0;
  }

  /* read the communication information */
  if (!read_comm_map_info(pexoid, Proc, prob, mesh)) {
    Gen_Error(0, "fatal: Error returned from read_comm_map_info");
    return 0;
  }

  /* Close the parallel file */
  if(ex_close (pexoid) < 0) {
    Gen_Error(0, "fatal: Error returned from ex_close");
    return 0;
  }

  /* print out the distributed mesh */
  if (Debug_Driver > 3)
    print_distributed_mesh(Proc, Num_Proc, mesh);

  DEBUG_TRACE_END(Proc, yo);
  return 1;

#endif /* ZOLTAN_NEMESIS */
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

#ifdef ZOLTAN_NEMESIS

static int read_elem_info(int pexoid, int Proc, PROB_INFO_PTR prob,
                          MESH_INFO_PTR mesh)
{
  /* Local declarations. */
  char  *yo = "read_elem_info";
  int    iblk, ielem, inode, lnode, cnode, iplace, len;
  int    max_nsur = 0;
  int   *nmap, *emap, *connect;
  int  **sur_elem, *nsurnd;
  ELEM_INFO_PTR elements = mesh->elements;

  float *xptr = NULL, *yptr = NULL, *zptr = NULL;
/***************************** BEGIN EXECUTION ******************************/

  DEBUG_TRACE_START(Proc, yo);

  /* allocate memory for the global number maps */
  nmap = (int *) malloc ((mesh->num_nodes + mesh->num_elems) * sizeof(int));
  if (!nmap) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }
  emap = nmap + mesh->num_nodes;


  /*
   * get the global maps
   */
  if (ex_get_elem_num_map(pexoid, emap) < 0) {
    Gen_Error(0, "fatal: Error returned from ex_get_elem_num_map");
    return 0;
  }

  if (ex_get_node_num_map(pexoid, nmap) < 0) {
    Gen_Error(0, "fatal: Error returned from ex_get_node_num_map");
    return 0;
  }

  /* allocate memory for the coordinates */
  xptr = (float *) malloc (mesh->num_dims * mesh->num_nodes * sizeof(float));
  if (!xptr) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }
  switch (mesh->num_dims) {
    case 3:
      zptr = xptr + 2 * mesh->num_nodes;
      /* FALLTHRU */
    case 2:
      yptr = xptr + mesh->num_nodes;
  }

  if (ex_get_coord(pexoid, xptr, yptr, zptr) < 0) {
    Gen_Error(0, "fatal: Error returned from ex_get_coord");
    return 0;
  }

  /*
   * figure out which element block needs
   * the most space for its connect table
   */
  len = 0;
  for (iblk = 0; iblk < mesh->num_el_blks; iblk++)
    if ((iplace = mesh->eb_cnts[iblk] * mesh->eb_nnodes[iblk]) > len)
      len = iplace;

  connect = (int *) malloc (len * sizeof(int));
  if (!connect) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }

  /***************************************************************************/
  /* Fill the Connect table, Coordinates, Global Ids for each element        */
  /***************************************************************************/
  iplace = 0;
  for (iblk = 0; iblk < mesh->num_el_blks; iblk++) {

    if (mesh->eb_cnts[iblk] > 0) {
      if (ex_get_elem_conn(pexoid, mesh->eb_ids[iblk], connect) < 0) {
        Gen_Error(0, "fatal: Error returned from ex_get_elem_conn");
        return 0;
      }

      cnode = 0;
      for (ielem = 0; ielem < mesh->eb_cnts[iblk]; ielem++) {
        /* set some fields in the element structure */
        elements[iplace].border = 0;
        elements[iplace].globalID = emap[iplace];
        elements[iplace].elem_blk = iblk;
        elements[iplace].nadj = 0;
        elements[iplace].adj_len = 0;
        /* weights are 1 for now */
        elements[iplace].cpu_wgt[0] = 1.0;
        elements[iplace].mem_wgt = 1.0;

        /* allocate space for the connect list and the coordinates */
        elements[iplace].connect = (int *) malloc(mesh->eb_nnodes[iblk] *
                                                  sizeof(int));
        if (!(elements[iplace].connect)) {
          Gen_Error(0, "fatal: insufficient memory");
          return 0;
        }

        elements[iplace].coord = (float **) malloc(mesh->eb_nnodes[iblk] *
                                                   sizeof(float *));
        if (!(elements[iplace].coord)) {
          Gen_Error(0, "fatal: insufficient memory");
          return 0;
        }

        /* save the connect table as local numbers for the moment */
        for (inode = 0; inode < mesh->eb_nnodes[iblk]; inode++) {
          lnode = connect[cnode] - 1;
          elements[iplace].connect[inode] = lnode;
          cnode++;

          elements[iplace].coord[inode] = (float *) malloc(mesh->num_dims *
                                                           sizeof(float));
          if (!(elements[iplace].coord[inode])) {
            Gen_Error(0, "fatal: insufficient memory");
            return 0;
          }

          switch (mesh->num_dims) {
            case 3:
              elements[iplace].coord[inode][2] = zptr[lnode];
              /* FALLTHRU */
            case 2:
              elements[iplace].coord[inode][1] = yptr[lnode];
              /* FALLTHRU */
            case 1:
              elements[iplace].coord[inode][0] = xptr[lnode];
          }
        } /* End: "for (inode = 0; inode < mesh->eb_nnodes[iblk]; inode++)" */

        iplace++;

      } /* End: "for (ielem = 0; ielem < mesh->eb_cnts[iblk]; ielem++)" */
    } /* End: "if (mesh->eb_cnts[iblk] > 0)" */
  } /* End: "for (iblk = 0; iblk < mesh->num_el_blks; iblk++)" */

  /* free some memory */
  free(connect);
  free(xptr);

  /*************************************************************************/
  /* Find the adjacency list for each element                              */
  /*	Part one: find the surrounding elements for each node                */
  /*************************************************************************/
  sur_elem = (int **) malloc(mesh->num_nodes * sizeof(int *));
  if (!sur_elem) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }
  nsurnd = (int *) malloc(mesh->num_nodes * sizeof(int));
  if (!nsurnd) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }

  if (!find_surnd_elem(mesh, sur_elem, nsurnd, &max_nsur)) {
    Gen_Error(0, "fatal: Error returned from find_surnd_elems");
    return 0;
  }

  /*************************************************************************/
  /*	Part two: Find the adjacencies on this processor                     */
  /*		and get the edge weights                                     */ 
  /*************************************************************************/
  if (!find_adjacency(Proc, mesh, sur_elem, nsurnd, max_nsur)) {
    Gen_Error(0, "fatal: Error returned from find_adjacency");
    return 0;
  }

  /*
   * convert the node numbers in the connect lists to Global IDs
   * since they will be much easier to work with
   */
  for (ielem = 0; ielem < mesh->num_elems; ielem++) {
    iblk = elements[ielem].elem_blk;
    for (inode = 0; inode < mesh->eb_nnodes[iblk]; inode++) {
      elements[ielem].connect[inode] = nmap[elements[ielem].connect[inode]];
    }
  }

  for (inode = 0; inode < mesh->num_nodes; inode++) free(sur_elem[inode]);
  free(sur_elem);
  free(nsurnd);

  free(nmap);

  DEBUG_TRACE_END(Proc, yo);
  return 1;
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

static int find_surnd_elem(MESH_INFO_PTR mesh, int **sur_elem, int *nsurnd,
                           int *max_nsur)
{
  /* Local declarations. */
  int     ielem, inode, lnode;
  int    *alloc_cnt, *tmp_ptr;
  ELEM_INFO_PTR elements = mesh->elements;
  
/***************************** BEGIN EXECUTION ******************************/

  alloc_cnt = (int *) malloc(mesh->num_nodes * sizeof(int));
  if (!alloc_cnt) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }

  /* Allocate rows of the structure */
  for(inode=0; inode < mesh->num_nodes; inode++)
  {
    sur_elem[inode] = (int *) malloc(LIST_ALLOC*sizeof(int));
    if(!(sur_elem[inode])) {
      Gen_Error(0, "fatal: insufficient memory");
      return 0;
    }
    alloc_cnt[inode] = LIST_ALLOC;
    nsurnd[inode] = 0;
  }

  /* Find the surrounding elements for each node in the mesh */
  for(ielem=0; ielem < mesh->num_elems; ielem++) {
    for(inode=0; inode < mesh->eb_nnodes[elements[ielem].elem_blk]; inode++) {
      lnode = elements[ielem].connect[inode];

      /*
       * in the case of degenerate elements, where a node can be
       * entered into the connect table twice, need to check to
       * make sure that this element is not already listed as
       * surrounding this node
       */
      if (nsurnd[lnode] > 0 &&
            ielem == sur_elem[lnode][(nsurnd[lnode]) - 1])
        continue;

      nsurnd[lnode]++;

      /* keep track of the largest number of surrounding elements */
      if (nsurnd[lnode] > *max_nsur) *max_nsur = nsurnd[lnode];

      /* Check to see if this row's size should be increased */
      if(nsurnd[lnode] > alloc_cnt[lnode]) {
        alloc_cnt[lnode] += LIST_ALLOC;
        tmp_ptr = (int *) realloc(sur_elem[lnode],
                                  (alloc_cnt[lnode]) * sizeof(int));
        if(!tmp_ptr) {
          Gen_Error(0, "fatal: insufficient memory");
          return 0;
        }
        sur_elem[lnode] = tmp_ptr;
      }

      /* Add the element to the list */
      sur_elem[lnode][nsurnd[lnode]-1] = ielem;
    }

  } /* End "for(ielem=0; ielem < mesh->num_elems; ielem++)" */

  free (alloc_cnt);

  return 1;
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

static int find_adjacency(int Proc, MESH_INFO_PTR mesh,
                          int **sur_elem, int *nsurnd, int max_nsur)
{
  /* Local declarations. */
  int     i, iblk, nsides, ielem, nscnt, inode, entry;
  int     side_cnt, nnodes, sid;
  int     side_nodes[MAX_SIDE_NODES], mirror_nodes[MAX_SIDE_NODES];
  int    *hold_elem, *pt_list, nhold, nelem;
  ELEM_INFO_PTR elements = mesh->elements;

  int *eb_etype;
/***************************** BEGIN EXECUTION ******************************/
  /*
   * Use face definition of adjacencies. So, one elements that are
   * connected by an entire face will be considered adjacent. This
   * is temporary, and will be expanded. This will make determining
   * off processor adjacencies much easier.
   *
   * Adjacency info for an element's side i will be stored in the (i-1) entry 
   * of adj array for the element.  Sides without adjacencies will have -1
   * as the adj array entries for those sides.  This system allows easy
   * identification of side ids for recomputing element comm maps after
   * migration.
   */

  eb_etype = mesh->eb_etypes;

  /* allocate space to hold info about surounding elements */
  pt_list = (int *) malloc(2 * max_nsur * sizeof(int));
  if(!pt_list) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }
  hold_elem = pt_list + max_nsur;

  for (ielem = 0; ielem < mesh->num_elems; ielem++) {

    iblk = elements[ielem].elem_blk;

    /* exclude circle and sphere elements from graph */
    if (mesh->eb_nnodes[iblk] > 1) {

      if ((nsides = get_elem_info(NSIDES, (E_Type) (eb_etype[iblk]), 0)) < 0) {
        Gen_Error(0, "fatal: could not get element information");
        return 0;
      }

      elements[ielem].adj = (int *) malloc(nsides*sizeof(int));
      elements[ielem].adj_proc = (int *) malloc(nsides*sizeof(int));
      elements[ielem].edge_wgt = (float *) malloc(nsides*sizeof(float));
      if(!(elements[ielem].adj) || !(elements[ielem].edge_wgt) ||
         !(elements[ielem].adj_proc)) {
        Gen_Error(0, "fatal: insufficient memory");
        return 0;
      }
      /* NOTE: nadj set in read_elem_info in case graph not generated */
      elements[ielem].adj_len = nsides;

      /* Initialize adjacency entries to -1 for each side. */
      for (nscnt = 0; nscnt < nsides; nscnt++) {
        elements[ielem].adj[nscnt] = -1;
        elements[ielem].adj_proc[nscnt] = -1;
        elements[ielem].edge_wgt[nscnt] = 0;
      }

      /* check each side of this element */
      for (nscnt = 0; nscnt < nsides; nscnt++) {

        /* get the list of nodes on this side set */
        side_cnt = ss_to_node_list((E_Type) (eb_etype[iblk]), 
                                   elements[ielem].connect,
                                   (nscnt+1), side_nodes);

        /*
         * now I need to determine how many side set nodes I
         * need to use to determine if there is an element
         * connected to this side.
         *
         * 2-D - need two nodes, so find one intersection
         * 3-D - need three nodes, so find two intersections
         * NOTE: must check to make sure that this number is not
         *       larger than the number of nodes on the sides (ie - SHELL).
         */
        nnodes = mesh->num_dims;
        if (side_cnt < nnodes)   nnodes = side_cnt;
        nnodes--;      /* decrement to find the number of intersections  */

        nelem = 0;     /* reset this in case no intersections are needed */

        /* copy the first array into temp storage */
        nhold = nsurnd[side_nodes[0]];
        for (i = 0; i < nhold; i++)
          hold_elem[i] = sur_elem[side_nodes[0]][i];

        for (inode = 0; inode < nnodes; inode++) {
          nelem = find_inter(hold_elem, sur_elem[side_nodes[(inode+1)]],
                             nhold, nsurnd[side_nodes[(inode+1)]], 2, pt_list);

          if (nelem < 2) break;
          else {
            nhold = nelem;
            for (i = 0; i < nelem; i++)
              hold_elem[i] = hold_elem[pt_list[i]];
          }
        }

        /*
         * if there is an element on this side of ielem, then there
         * will be at least two elements in the intersection (one
         * will be ielem)
         */
        if (nelem > 1) {

          /*
           * now go through and check each element in the list
           * to see if it is different than ielem.
           */
          for(i=0; i < nelem; i++) {

            entry = hold_elem[i];

            if(entry != ielem) {
              /*
               * get the side id of entry. Make sure that ielem is
               * trying to communicate to a valid side of elem
               */
              side_cnt = get_ss_mirror((E_Type) (eb_etype[iblk]), 
                                       side_nodes, (nscnt+1),
                                       mirror_nodes);

              /*
               * in order to get the correct side order for elem,
               * get the mirror of the side of ielem
               */
              sid = get_side_id((E_Type) (eb_etype[elements[entry].elem_blk]),
                                elements[entry].connect,
                                side_cnt, mirror_nodes);
              if (sid > 0) {
                (elements[ielem].nadj)++;

                /*
                 * store the adjacency info in the entry for this side.
                 */
                elements[ielem].adj[nscnt] = entry;
                elements[ielem].adj_proc[nscnt] = Proc;

                /*
                 * the edge weight is the number of nodes in the
                 * connecting face
                 */
                elements[ielem].edge_wgt[nscnt] = 
                  (float) get_elem_info(NSNODES, (E_Type) (eb_etype[iblk]),
                                       (nscnt+1));

              } /* End: "if (sid > 0)" */
              else if (sid < 0) {
                Gen_Error(0, "fatal: could not find side id");
                return 0;
              }
            } /* End: "if(ielem != entry)" */
          } /* End: "for(i=0; i < nelem; i++)" */
        } /* End: "if (nelem > 1)" */
      } /* End: "for (nscnt = 0; ...)" */
    } /* End: "if (nnode > 1)" */
  } /* End: "for (ielem=0; ...)" */

  free(pt_list);

  return 1;
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

static int read_comm_map_info(int pexoid, int Proc, PROB_INFO_PTR prob,
                              MESH_INFO_PTR mesh)
{
  /* Local declarations. */
  char *yo = "read_comm_map_info";
  int  ielem, imap, loc_elem, iblk, max_len, offset, index;
  int  nnodei, nnodeb, nnodee, nelemi, nelemb, nncmap;
  int *int_elem, *bor_elem;
  int *proc_ids;
  int  sid;
  ELEM_INFO_PTR elements = mesh->elements;

  E_Type etype;

  MPI_Status status;
/***************************** BEGIN EXECUTION ******************************/

  DEBUG_TRACE_START(Proc, yo);

  if (ne_get_loadbal_param(pexoid, &nnodei, &nnodeb, &nnodee,
                     &nelemi, &nelemb, &nncmap, &(mesh->necmap), Proc) < 0) {
    Gen_Error(0, "fatal: Error returned from ne_get_loadbal_param");
    return 0;
  }

  /*
   * get the list of the border elements in order to set
   * the border flag in the element structures
   */
  int_elem = (int *) malloc ((nelemi + nelemb) * sizeof(int));
  if (!int_elem) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }
  bor_elem = int_elem + nelemi;

  if (ne_get_elem_map(pexoid, int_elem, bor_elem, Proc) < 0) {
    Gen_Error(0, "fatal: Error returned from ne_get_elem_map");
    return 0;
  }

  for (ielem = 0; ielem < nelemb; ielem++) {
    elements[bor_elem[ielem]-1].border = 1;
  }

  free(int_elem);

  /*
   * For now, only get the elemental communication maps,
   * since, in the driver, elements are only considered
   * adjacent if they share a face (same definition used
   * in element communication maps). Eventually, the ability
   * to consider elements that are connected by any nodes
   * adjacent will have to be added. When that happens,
   * the nodal communication maps will be needed.
   */
  mesh->ecmap_cnt = (int *) malloc (mesh->necmap * sizeof(int));
  mesh->ecmap_id = (int *) malloc(mesh->necmap * sizeof(int));
  if (!mesh->ecmap_cnt || !mesh->ecmap_id) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }

  if (ne_get_cmap_params(pexoid, NULL, NULL, mesh->ecmap_id, 
                         mesh->ecmap_cnt, Proc) < 0) {
    Gen_Error(0, "fatal: Error returned from ne_get_cmap_params");
    return 0;
  }

  max_len = 0;
  for (imap = 0; imap < mesh->necmap; imap++)
    max_len += mesh->ecmap_cnt[imap];

  proc_ids = (int *) malloc(max_len * sizeof(int));
  mesh->ecmap_elemids = (int *) malloc(max_len * sizeof(int));
  mesh->ecmap_sideids = (int *) malloc(max_len * sizeof(int));
  mesh->ecmap_neighids = (int *) malloc(max_len * sizeof(int));
  if (!mesh->ecmap_elemids || !mesh->ecmap_sideids || !mesh->ecmap_neighids) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }

  offset = 0;
  for (imap = 0; imap < mesh->necmap; imap++) {

    if(ne_get_elem_cmap(pexoid, mesh->ecmap_id[imap],
                        &(mesh->ecmap_elemids[offset]),
                        &(mesh->ecmap_sideids[offset]), 
                        &(proc_ids[offset]), Proc) < 0) {
      Gen_Error(0, "fatal: Error returned from ne_get_elem_cmap");
      return 0;
    }
    offset += mesh->ecmap_cnt[imap];
  } /* End: "for (imap = 0; imap < mesh->necmap; imap++)" */

  /*
   * Decrement the ecmap_elemids by one for zero-based local numbering.
   * Convert the element ids to global ids to send to
   * the neighboring processor. Store them in the proc_ids
   * array, since the proc_id information is not used for
   * anything here.
   */
  for (ielem = 0; ielem < max_len; ielem++) {
    mesh->ecmap_elemids[ielem]--;
    proc_ids[ielem] = elements[mesh->ecmap_elemids[ielem]].globalID;
  }

  /*
   * Now communicate with other processor to get global IDs
   * for the adjacent elements in this communication map.
   *
   * parallel nemesis trick...
   * each communication map is only for a single neigboring
   * processor, and the communication map id is the processor
   * number that it is for
   */
  offset = 0;
  for (imap = 0; imap < mesh->necmap; imap++) {

    /*
     * handshake with processor and wait until it is ready
     * to talk
     */
    MPI_Send(NULL, 0, MPI_INT, mesh->ecmap_id[imap], 0, MPI_COMM_WORLD);
    MPI_Recv(NULL, 0, MPI_INT, mesh->ecmap_id[imap], 0, MPI_COMM_WORLD, &status);

    /* now send list of global element ids to the processor for this map */
    MPI_Send(&(proc_ids[offset]), mesh->ecmap_cnt[imap], MPI_INT, 
             mesh->ecmap_id[imap], 0, MPI_COMM_WORLD);
    MPI_Recv(&(mesh->ecmap_neighids[offset]), mesh->ecmap_cnt[imap], MPI_INT, 
             mesh->ecmap_id[imap], 0, MPI_COMM_WORLD, &status);
    offset += mesh->ecmap_cnt[imap];
  }

  /* now process all of the element ids that have been received */
  offset = 0;
  for (imap = 0; imap < mesh->necmap; imap++) {
    for (ielem = 0; ielem < mesh->ecmap_cnt[imap]; ielem++) {
      index = ielem + offset;
      /* translate from element id in the communication map to local elem id */
      loc_elem = mesh->ecmap_elemids[index];
      iblk = elements[loc_elem].elem_blk;
      etype = (E_Type) (mesh->eb_etypes[iblk]);

      (elements[loc_elem].nadj)++;
      if(elements[loc_elem].nadj > elements[loc_elem].adj_len) {
        /* Shouldn't happen as long as only side adjacencies are used. */
        /* adj_len == number of sides.                                 */
        /* Space should already be allocated for the adjacencies read  */
        /* from the communication maps.                                */
        Gen_Error(0, "fatal: Number of adj greater than adj_len");
        return 0;
      }

      /* Store adjacency info in the adj entry corresponding to this side. */
      sid = mesh->ecmap_sideids[index] - 1;
      elements[loc_elem].adj[sid] = mesh->ecmap_neighids[index];
      elements[loc_elem].adj_proc[sid] = mesh->ecmap_id[imap];
      elements[loc_elem].edge_wgt[sid] =
             (float) get_elem_info(NSNODES, etype, mesh->ecmap_sideids[index]);

    } /* End: "for (ielem = 0; ielem < mesh->ecmap_cnt[imap]; ielem++)" */
    offset += mesh->ecmap_cnt[imap];
  } /* End: "for for (imap = 0; imap < mesh->necmap; imap++)" */
  
  free (proc_ids);

  DEBUG_TRACE_END(Proc, yo);
  return 1;
}
#endif /* ZOLTAN_NEMESIS */

#ifdef __cplusplus
} /* closing bracket for extern "C" */
#endif
