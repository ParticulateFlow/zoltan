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


#ifndef _DR_LOADBAL_CONST_H_
#define _DR_LOADBAL_CONST_H_

#ifdef __cplusplus
/* if C++, define the rest of this header file as extern C */
extern "C" {
#endif


extern int setup_zoltan(struct LB_Struct *, int, PROB_INFO_PTR, MESH_INFO_PTR); 
extern int run_zoltan(struct LB_Struct *, int, PROB_INFO_PTR, MESH_INFO_PTR); 
extern int migrate_elements(int, MESH_INFO_PTR, struct LB_Struct *, 
                            int, int, int, 
                            LB_ID_PTR, LB_ID_PTR, int *, int, LB_ID_PTR,
                            LB_ID_PTR, int *);

extern ELEM_INFO *search_by_global_id(MESH_INFO *, int, int *);

#ifdef __cplusplus
} /* closing bracket for extern "C" */
#endif

#endif /* _DR_LOADBAL_CONST_H_ */
