!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
! Zoltan Library for Parallel Applications                                   !
! For more info, see the README file in the top-level Zoltan directory.      ! 
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!  CVS File Information :
!     $RCSfile$
!     $Author$
!     $Date$
!     Revision$
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

module dr_migrate

!/*--------------------------------------------------------------------------*/
!/* Purpose: Call Zoltan to migrate elements.                                */
!/*          Contains all of the callback functions that Zoltan needs        */
!/*          for the migration.                                              */
!/*                                                                          */
!/* General migration strategy:                                              */
!/*  1. In migrate_pre_process, reset all adjacency info for local elems,    */
!/*     using the local ID for adj. elems that are or will be (after         */
!/*     migration) on this processor, and the global ID for adj elems that   */
!/*     are not or will not be (after migration)  on this processor.         */
!/*  2. When exporting elements, convert all the export elems' adjacencies'  */
!/*     local IDs to global IDs.                                             */ 
!/*  3. When importing elements, convert import elems' adjacencies that are  */
!/*     local elements to local ids.                                         */
!/*                                                                          */
!/*--------------------------------------------------------------------------*/
!/* Author(s):  Matthew M. St.John (9226)                                    */
!/*             Karen D. Devine (9226)                                       */
!   Translated to Fortran by William F. Mitchell
!/*--------------------------------------------------------------------------*/
!/*--------------------------------------------------------------------------*/
!/* Revision History:                                                        */
!/*    10 May 1999:       Date of creation.                                  */
!      14 Sept 1999:      Fortran translation
!/*--------------------------------------------------------------------------*/

use mpi_h
use zoltan
use lb_user_const
use dr_chaco_io
use dr_const

implicit none
private

public :: migrate_elements, search_by_global_id

!/*****************************************************************************/
!/*
! *  Static global variables to help with migration.
! */
integer(LB_INT), allocatable, save :: New_Elem_Index(:)
!                                      /* Array containing globalIDs of 
!                                         elements in the new decomposition,
!                                         ordered in the same order as the
!                                         elements array.
!                                         Built in migrate_pre_process; used
!                                         in migrate_pre_process to adjust
!                                         element adjacencies; used in 
!                                         migrate_unpack_elem to store 
!                                         imported elements.                  */
integer(LB_INT), save :: New_Elem_Index_Size = 0 !/* Number of integers
!                                         allocated in New_Elem_Index.
logical, save :: Use_Edge_Wgts = .false.  !/* Flag indicating whether elements
!                                         store edge weights.                 */

contains

!/*****************************************************************************/
!/*****************************************************************************/
!/*****************************************************************************/
logical function migrate_elements(Proc, lb_obj, &
                                  num_gid_entries, num_lid_entries, &
                                  num_imp, imp_gids, &
                                  imp_lids, imp_procs, num_exp, exp_gids, &
                                  exp_lids, exp_procs)
  integer(LB_INT) :: Proc
  type(LB_Struct) :: lb_obj
  integer(LB_INT) :: num_gid_entries, num_lid_entries
  integer(LB_INT) :: num_imp
  integer(LB_INT),pointer :: imp_gids(:)
  integer(LB_INT),pointer :: imp_lids(:)
  integer(LB_INT), pointer :: imp_procs(:)
  integer(LB_INT) :: num_exp
  integer(LB_INT),pointer :: exp_gids(:)
  integer(LB_INT),pointer :: exp_lids(:)
  integer(LB_INT), pointer :: exp_procs(:)

!/* Local declarations. */
type(LB_User_Data_2) :: mesh_wrapper ! wrapper to pass mesh to query

!/***************************** BEGIN EXECUTION ******************************/

! make Mesh passable to the callback functions
  mesh_wrapper%ptr => Mesh

!  /*
!   * register migration functions
!   */
! if (LB_Set_Fn(lb_obj, LB_PRE_MIGRATE_FN_TYPE, migrate_pre_process, &
!               mesh_wrapper) == LB_FATAL) then
  if (LB_Set_Pre_Migrate_Fn(lb_obj, migrate_pre_process, &
                mesh_wrapper) == LB_FATAL) then
    print *, "fatal:  error returned from LB_Set_Fn()"
    migrate_elements = .false.; return
  endif

! if (LB_Set_Fn(lb_obj, LB_POST_MIGRATE_FN_TYPE, migrate_post_process, &
!               mesh_wrapper) == LB_FATAL) then
  if (LB_Set_Post_Migrate_Fn(lb_obj, migrate_post_process, &
                mesh_wrapper) == LB_FATAL) then
    print *, "fatal:  error returned from LB_Set_Fn()"
    migrate_elements = .false.; return
  endif

! if (LB_Set_Fn(lb_obj, LB_OBJ_SIZE_FN_TYPE, migrate_elem_size, &
!              mesh_wrapper) == LB_FATAL) then
  if (LB_Set_Obj_Size_Fn(lb_obj, migrate_elem_size, &
               mesh_wrapper) == LB_FATAL) then
    print *, "fatal:  error returned from LB_Set_Fn()"
    migrate_elements = .false.; return
  endif

! if (LB_Set_Fn(lb_obj, LB_PACK_OBJ_FN_TYPE, migrate_pack_elem, &
!               mesh_wrapper) == LB_FATAL) then
  if (LB_Set_Pack_Obj_Fn(lb_obj, migrate_pack_elem, & 
                mesh_wrapper) == LB_FATAL) then
    print *, "fatal:  error returned from LB_Set_Fn()"
    migrate_elements = .false.; return
  endif

! if (LB_Set_Fn(lb_obj, LB_UNPACK_OBJ_FN_TYPE, migrate_unpack_elem, &
!               mesh_wrapper) == LB_FATAL) then
  if (LB_Set_Unpack_Obj_Fn(lb_obj, migrate_unpack_elem, &
                mesh_wrapper) == LB_FATAL) then
    print *, "fatal:  error returned from LB_Set_Fn()"
    migrate_elements = .false.; return
  endif

  if (LB_Help_Migrate(lb_obj, &
                      num_imp, imp_gids, imp_lids, imp_procs, &
                      num_exp, exp_gids, exp_lids, exp_procs) == LB_FATAL) then
    print *, "fatal:  error returned from LB_Help_Migrate()"
    migrate_elements = .false.; return
  endif

  migrate_elements = .true.
end function migrate_elements

!/*****************************************************************************/
!/*****************************************************************************/
!/*****************************************************************************/
subroutine migrate_pre_process(data, num_gid_entries, num_lid_entries, &
                               num_import, import_global_ids, &
                               import_local_ids, import_procs, num_export, &
                               export_global_ids, export_local_ids, &
                               export_procs, ierr)
type(LB_User_Data_2), intent(in) :: data
integer(LB_INT), intent(in) :: num_gid_entries, num_lid_entries
integer(LB_INT), intent(in) :: num_import, num_export
integer(LB_INT), intent(in) :: import_global_ids(*), import_local_ids(*), &
                    export_global_ids(*), export_local_ids(*)
integer(LB_INT), intent(in) :: import_procs(*), export_procs(*)
integer(LB_INT), intent(out) :: ierr

integer(LB_INT) :: i, j, k, idx, maxlen, proc, offset, mpierr, allocstat
integer(LB_INT), allocatable :: proc_ids(:) !/* Temp array of processor assignments for elements.*/
logical, allocatable :: change(:) !/* Temp array indicating whether local element's adj 
                           ! list must be updated due to a nbor's migration.  */
integer(LB_INT) :: new_proc  !/* New processor assignment for nbor element.
integer(LB_INT) :: exp_elem  !/* index of an element being exported */
integer(LB_INT) :: bor_elem  !/* index of an element along the processor border
integer(LB_INT), allocatable :: send_vec(:), recv_vec(:) !/* Communication vecs.
type(ELEM_INFO), pointer :: elements(:), tmp(:), exp_elem_ptr
type(MESH_INFO), pointer :: mesh_data
logical :: flag
integer(LB_INT) :: gid  ! Temporary variables to change positioning of IDs.
integer(LB_INT) :: lid

  gid = num_gid_entries;
  lid = num_lid_entries;

  mesh_data => data%ptr
  elements => mesh_data%elements

  ierr = LB_OK

!  /*
!   *  Set some flags.  Assume if true for one element, true for all elements.
!   */

  if (associated(elements(0)%edge_wgt)) then
    flag = .true.
  else
    flag = .false.
  endif
! Make sure all procs have the same value.
  call MPI_Allreduce(flag, Use_Edge_Wgts, 1, MPI_INTEGER, MPI_LOR, &
                     MPI_COMM_WORLD, mpierr)


!  /*
!   *  For all elements, update adjacent elements' processor information.
!   *  That way, when perform migration, will be migrating updated adjacency
!   *  information.  
!   */
  
  if (Mesh%num_elems == 0) return !/* No elements to update */

  call MPI_Comm_rank(MPI_COMM_WORLD, proc, mpierr)

!  /*
!   *  Build New_Elem_Index array and list of processor assignments.
!   */
  New_Elem_Index_Size = Mesh%num_elems + num_import - num_export
  if (Mesh%elem_array_len > New_Elem_Index_Size) then
    New_Elem_Index_Size = Mesh%elem_array_len
  endif
  allocate(New_Elem_Index(0:New_Elem_Index_Size-1), &
           proc_ids(0:Mesh%num_elems-1), &
           change(0:Mesh%num_elems-1), stat=allocstat)
  if (allocstat /= 0) then
    print *, "fatal: insufficient memory"
    ierr = LB_MEMERR
    return
  endif

  do i = 0, Mesh%num_elems-1
    New_Elem_Index(i) = elements(i)%globalID
    proc_ids(i) = proc
    change(i) = .false.
  end do

  do i = Mesh%num_elems, New_Elem_Index_Size-1
    New_Elem_Index(i) = -1
  end do

  do i = 1, num_export
    if (num_lid_entries.gt.0) then
      exp_elem = export_local_ids(lid + (i-1)*num_lid_entries)
    else
      exp_elem_ptr => search_by_global_id(mesh_data, &
                       export_global_ids(gid+(i-1)*num_gid_entries), exp_elem)
    endif
    New_Elem_Index(exp_elem) = -1
    proc_ids(exp_elem) = export_procs(i)
  end do

  do i = 1, num_import
!    /* search for first free location */
    do j = 0, New_Elem_Index_Size-1
      if (New_Elem_Index(j) == -1) exit
    end do

    New_Elem_Index(j) = import_global_ids(gid+(i-1)*num_gid_entries)
  end do

!  /* 
!   * Update local information 
!   */

!  /* Set change flag for elements whose adjacent elements are being exported */

  do i = 1, num_export
    if (num_lid_entries.gt.0) then
      exp_elem = export_local_ids(lid + (i-1)*num_lid_entries)
    else
      exp_elem_ptr => search_by_global_id(mesh_data, &
                       export_global_ids(gid+(i-1)*num_gid_entries), exp_elem)
    endif
    do j = 0, elements(exp_elem)%adj_len-1

!     /* Skip NULL adjacencies (sides that are not adjacent to another elem). */
      if (elements(exp_elem)%adj(j) == -1) cycle

!      /* Set change flag for adjacent local elements. */
      if (elements(exp_elem)%adj_proc(j) == proc) then
        change(elements(exp_elem)%adj(j)) = .true.
      endif
    end do
  end do

!  /* Change adjacency information in marked elements */
  do i = 0, Mesh%num_elems-1
    if (.not.change(i)) cycle

!    /* loop over marked element's adjacencies; look for ones that are moving */
    do j = 0, elements(i)%adj_len-1

!     /* Skip NULL adjacencies (sides that are not adjacent to another elem). */
      if (elements(i)%adj(j) == -1) cycle

      if (elements(i)%adj_proc(j) == proc) then
!        /* adjacent element is local; check whether it is moving. */
        new_proc = proc_ids(elements(i)%adj(j))
        if (new_proc /= proc) then
!          /* Adjacent element is being exported; update this adjacency entry */
          elements(i)%adj(j) = elements(elements(i)%adj(j))%globalID
          elements(i)%adj_proc(j) = new_proc
        endif
      endif
    end do
  end do
  deallocate(change)

!  /*
!   * Update off-processor information 
!   */

  maxlen = 0
  do i = 0, Mesh%necmap-1
    maxlen = maxlen + Mesh%ecmap_cnt(i)
  end do

!  /*  No communication is being done; don't have to update any more info. */
  if (maxlen == 0) return

  allocate(send_vec(0:maxlen-1), stat=allocstat)
  if (allocstat /= 0) then
    print *, "fatal: insufficient memory"
    ierr = LB_MEMERR
    return
  endif

!  /* Load send vector */

  do i = 0, maxlen-1
    send_vec(i) = proc_ids(Mesh%ecmap_elemids(i))
  end do

  deallocate(proc_ids)
  allocate(recv_vec(0:maxlen-1), stat=allocstat)
  if (allocstat /= 0) then
    print *, "fatal: insufficient memory"
    ierr = LB_MEMERR
    return
  endif

!  /*  Perform boundary exchange */

  call boundary_exchange(1_LB_INT, send_vec, recv_vec)
  
!  /* Unload receive vector */

  offset = 0
  do i = 0, Mesh%necmap-1
    do j = 0, Mesh%ecmap_cnt(i)-1
      if (recv_vec(offset) == Mesh%ecmap_id(i)) then
!        /* off-processor element is not changing processors.  */
!        /* no changes are needed in the local data structure. */
        offset = offset + 1
        cycle
      endif
!      /* Change processor assignment in local element's adjacency list */
      bor_elem = Mesh%ecmap_elemids(offset)
      do k = 0, elements(bor_elem)%adj_len-1

!        /* Skip NULL adjacencies (sides that are not adj to another elem). */
        if (elements(bor_elem)%adj(k) == -1) cycle

        if (elements(bor_elem)%adj(k) == Mesh%ecmap_neighids(offset) .and. &
            elements(bor_elem)%adj_proc(k) == Mesh%ecmap_id(i)) then
          elements(bor_elem)%adj_proc(k) = recv_vec(offset)
          if (recv_vec(offset) == proc) then
!            /* element is moving to this processor; */
!            /* convert adj from global to local ID. */
            idx = in_list(Mesh%ecmap_neighids(offset), New_Elem_Index_Size, &
                          New_Elem_Index)
            if (idx == -1) then
              print *, "fatal: unable to locate element in New_Elem_Index"
              ierr = LB_FATAL
              return
            endif
            elements(bor_elem)%adj(k) = idx
          endif
          exit  !/* from k loop */
        endif
      end do
    offset = offset + 1
    end do
  end do

if (allocated(recv_vec)) deallocate(recv_vec)
if (allocated(send_vec)) deallocate(send_vec)

  if (Mesh%elem_array_len < New_Elem_Index_Size) then
    allocate(tmp(0:New_Elem_Index_Size-1),stat=allocstat)
    if (allocstat /= 0) then
      print *, "fatal: insufficient memory"
      ierr = LB_MEMERR
      return
    endif
    tmp(0:Mesh%num_elems-1) = Mesh%elements(0:Mesh%num_elems-1)
    deallocate(Mesh%elements)
    Mesh%elements => tmp
    Mesh%elem_array_len = New_Elem_Index_Size

!    /* initialize the new spots */
    do i = Mesh%num_elems, Mesh%elem_array_len-1
      call initialize_element(Mesh%elements(i))
    end do
  endif

end subroutine migrate_pre_process

!/*****************************************************************************/
!/*****************************************************************************/
!/*****************************************************************************/
subroutine migrate_post_process(data, num_gid_entries, num_lid_entries, &
                                num_import, import_global_ids, &
                                import_local_ids, import_procs, num_export, &
                                export_global_ids, export_local_ids, &
                                export_procs, ierr)
type(LB_User_Data_2), intent(in) :: data
integer(LB_INT), intent(in) :: num_gid_entries, num_lid_entries
integer(LB_INT), intent(in) :: num_import, num_export
integer(LB_INT), intent(in) :: import_global_ids(*), import_local_ids(*), &
                               import_procs(*), export_global_ids(*), &
                               export_local_ids(*), export_procs(*)
integer(LB_INT), intent(out) :: ierr

type(ELEM_INFO), pointer :: element(:)
integer(LB_INT) :: proc, num_proc
integer(LB_INT) :: i, j, k, last, mpierr
integer(LB_INT) :: adj_elem
type(MESH_INFO), pointer :: mesh_data

  mesh_data => data%ptr
  element => mesh_data%elements

  call MPI_Comm_rank(MPI_COMM_WORLD, proc, mpierr)
  call MPI_Comm_size(MPI_COMM_WORLD, num_proc, mpierr)

! /* compact elements array, as the application expects the array to be dense */
  do i = 0, New_Elem_Index_Size-1
    if (New_Elem_Index(i) /= -1) cycle

!    /* Don't want to shift all elements down one position to fill the  */
!    /* blank spot -- too much work to adjust adjacencies!  So find the */
!    /* last element in the array and move it to the blank spot.        */

    do last = New_Elem_Index_Size-1, 0, -1
      if (New_Elem_Index(last) /= -1) exit
    end do

!    /* If (last < i), array is already dense; i is just in some blank spots  */
!    /* at the end of the array.  Quit the compacting.                     */
    if (last < i) exit

!    /* Copy element[last] to element[i]. */
    element(i) = element(last)

!    /* Adjust adjacencies for local elements.  Off-processor adjacencies */
!    /* don't matter here.                                                */

    do j = 0, element(i)%adj_len-1

!     /* Skip NULL adjacencies (sides that are not adjacent to another elem). */
      if (element(i)%adj(j) == -1) cycle

      adj_elem = element(i)%adj(j)

!      /* See whether adjacent element is local; if so, adjust its entry */
!      /* for local element i.                                           */
      if (element(i)%adj_proc(j) == proc) then
        do k = 0, element(adj_elem)%adj_len-1
          if (element(adj_elem)%adj(k) == last .and. &
              element(adj_elem)%adj_proc(k) == proc) then
!            /* found adjacency entry for element last; change it to i */
            element(adj_elem)%adj(k) = i
            exit
          endif
        end do
      endif
    end do

    !/* Update New_Elem_Index */
    New_Elem_Index(i) = New_Elem_Index(last)
    New_Elem_Index(last) = -1

    element(last)%globalID = -1
    element(last)%border = 0
    element(last)%nadj = 0
    element(last)%adj_len = 0
    element(last)%elem_blk = -1
    element(last)%cpu_wgt = 0
    element(last)%mem_wgt = 0
    nullify(element(last)%coord)
    nullify(element(last)%connect)
    nullify(element(last)%adj)
    nullify(element(last)%adj_proc)
    nullify(element(last)%edge_wgt)
  end do

  if (allocated(New_Elem_Index)) deallocate(New_Elem_Index)
  New_Elem_Index_Size = 0

  if (.not.build_elem_comm_maps(proc, element)) then
    print *, "Fatal: error rebuilding elem comm maps"
  endif

  ierr = LB_OK

end subroutine migrate_post_process

!/*****************************************************************************/
!/*****************************************************************************/
!/*****************************************************************************/
integer(LB_INT) function migrate_elem_size(data, num_gid_entries, num_lid_entries, elem_gid, elem_lid, ierr)
type(LB_User_Data_2), intent(in) :: data
integer(LB_INT), intent(in) :: num_gid_entries, num_lid_entries
integer(LB_INT), intent(in) :: elem_gid(*), elem_lid(*)
integer(LB_INT), intent(out) :: ierr
!/*
! * Function to return size of element information for a single element.
! */

integer(LB_INT) :: size
type(ELEM_INFO), pointer :: elements(:), current_elem
integer, parameter :: SIZE_OF_INT = 4, SIZE_OF_FLOAT = 4
type(MESH_INFO), pointer :: mesh_data
integer(LB_INT) :: idx, num_nodes
integer(LB_INT) :: gid  ! Temporary variables to change positioning of IDs.
integer(LB_INT) :: lid

  gid = num_gid_entries;
  lid = num_lid_entries;

  mesh_data => data%ptr
  elements => mesh_data%elements
  ierr = LB_OK
  if (num_lid_entries.gt.0) then
    current_elem => elements(elem_lid(lid))
  else
    current_elem => search_by_global_id(mesh_data, elem_gid(gid), idx)
  endif

!  /*
!   * Compute size of one element's data.
!   */

  size = 5 * SIZE_OF_INT + 2 * SIZE_OF_FLOAT
  num_nodes =  mesh_data%eb_nnodes(current_elem%elem_blk)
 
!  /* Add space for connect table. */
  if (Mesh%num_dims > 0) then
    size = size + num_nodes * SIZE_OF_INT
  endif

!  /* Add space for adjacency info (elements[].adj and elements[].adj_proc). */
  size = size + current_elem%adj_len * 2 * SIZE_OF_INT

!  /* Assume if one element has edge wgts, all elements have edge wgts. */
  if (Use_Edge_Wgts) then
    size = size + current_elem%adj_len * SIZE_OF_FLOAT
  endif

!  /* Add space for coordinate info */
  size = size + num_nodes * Mesh%num_dims * SIZE_OF_FLOAT
  
  migrate_elem_size = size
end function migrate_elem_size

!/*****************************************************************************/
!/*****************************************************************************/
!/*****************************************************************************/
subroutine migrate_pack_elem(data, num_gid_entries, num_lid_entries, &
                             elem_gid, elem_lid,  mig_proc, &
                             elem_data_size, buf, ierr)
type(LB_User_Data_2), intent(in) :: data
integer(LB_INT), intent(in) :: num_gid_entries, num_lid_entries
integer(LB_INT), intent(in) :: elem_gid(*), elem_lid(*)
integer(LB_INT), intent(in) :: mig_proc, elem_data_size
integer(LB_INT), intent(out) :: buf(*)
integer(LB_INT), intent(out) :: ierr

! NOTE: this assumes that a float is no bigger than an int
!       (see the use of the transfer function)

  type(ELEM_INFO), pointer :: elem(:)
  type(ELEM_INFO), pointer :: current_elem
  integer(LB_INT) :: size
  integer(LB_INT) :: i, j
  integer(LB_INT) :: idx
  integer(LB_INT) :: proc
  integer(LB_INT) :: num_nodes
  integer(LB_INT) :: mpierr
  type(MESH_INFO), pointer :: mesh_data
  integer(LB_INT) :: gid  ! Temporary variables to change positioning of IDs.
  integer(LB_INT) :: lid

  gid = num_gid_entries;
  lid = num_lid_entries;

  call MPI_Comm_rank(MPI_COMM_WORLD, proc, mpierr)

  mesh_data => data%ptr
  elem => mesh_data%elements !/* this is the head of the element struct array */

  if (num_lid_entries.gt.0) then
    current_elem => elem(elem_lid(lid))
  else
    current_elem => search_by_global_id(mesh_data, elem_gid(gid), idx)
  endif
  num_nodes = Mesh%eb_nnodes(current_elem%elem_blk)

!  /*
!   * copy the ELEM_INFO structure
!   */

  buf(1) = current_elem%border
  buf(2) = current_elem%globalID
  buf(3) = current_elem%elem_blk
  buf(4) = transfer(current_elem%cpu_wgt,1_LB_INT)
  buf(5) = transfer(current_elem%mem_wgt,1_LB_INT)
  buf(6) = current_elem%nadj
  buf(7) = current_elem%adj_len

!  /*
!   * copy the allocated integer fields for this element.
!   */

  size = 7

!  /* copy the connect table */
  if (Mesh%num_dims > 0) then
    do i = 0, num_nodes-1
      buf(size+i+1) = current_elem%connect(i)
    end do
    size = size + num_nodes
  endif

!  /* copy the adjacency info */
!  /* send globalID for all adjacencies */
  do i =  0, current_elem%adj_len-1
    if (current_elem%adj(i) /= -1 .and. current_elem%adj_proc(i) == proc) then
      buf(size + 2*i + 1) = New_Elem_Index(current_elem%adj(i))
    else
      buf(size + 2*i + 1) = current_elem%adj(i)
    endif
    buf(size + 2*i + 2) = current_elem%adj_proc(i)
  end do
  size = size + current_elem%adj_len * 2

!  /*
!   * copy the allocated float fields for this element.
!   */

!  /* copy the edge_wgt data */
  if (Use_Edge_Wgts) then
    do i = 0, current_elem%adj_len-1
      buf(size+i+1) = transfer(current_elem%edge_wgt(i),1_LB_INT)
    end do
    size = size + current_elem%adj_len
  endif

!  /* copy coordinate data */
  do i = 0, Mesh%num_dims-1
    do j = 0, num_nodes-1
      buf(size+i*num_nodes+j+1) = transfer(current_elem%coord(i,j),1_LB_INT)
    end do
  end do
  size = size + num_nodes * Mesh%num_dims

!  /*
!   * need to update the Mesh struct to reflect this element
!   * being gone
!   */
  Mesh%num_elems = Mesh%num_elems - 1
  Mesh%eb_cnts(current_elem%elem_blk) = Mesh%eb_cnts(current_elem%elem_blk) - 1

!  /*
!   * need to remove this entry from this procs list of elements
!   * do so by setting the globalID to -1
!   */
  current_elem%globalID = -1
  call free_element_arrays(current_elem)

!  /*
!   * NOTE: it is not worth the effort to determine the change in the
!   * number of nodes on this processor until all of the migration is
!   * completed.
!   */
  if (size > elem_data_size) then
    ierr = LB_WARN
  else
    ierr = LB_OK
  endif
end subroutine migrate_pack_elem

!/*****************************************************************************/
!/*****************************************************************************/
!/*****************************************************************************/
subroutine migrate_unpack_elem(data, num_gid_entries, &
                               elem_gid, elem_data_size, buf, ierr)
type(LB_User_Data_2), intent(in) :: data
integer(LB_INT), intent(in) :: num_gid_entries
integer(LB_INT), intent(in) :: elem_gid(*)
integer(LB_INT), intent(in) :: elem_data_size
integer(LB_INT), intent(in) :: buf(*)
integer(LB_INT), intent(out) :: ierr

  type(ELEM_INFO), pointer :: elem(:)
  type(ELEM_INFO), pointer :: current_elem
  integer(LB_INT) :: size, num_nodes
  integer(LB_INT) :: i, j, idx, mpierr, allocstat
  integer(LB_INT) :: proc
  type(MESH_INFO), pointer :: mesh_data
  integer(LB_INT) :: gid  ! Temporary variables to change positioning of IDs.

  gid = num_gid_entries;

  call MPI_Comm_rank(MPI_COMM_WORLD, proc, mpierr)

!  /*
!   * check if the element array has any space
!   * if not, allocate some new space
!   */

  mesh_data => data%ptr
  elem => mesh_data%elements

  idx = in_list(elem_gid(gid), New_Elem_Index_Size, New_Elem_Index)
  if (idx == -1) then
    print *, "fatal: Unable to locate position for element"
    ierr = LB_FATAL
    return
  endif

  current_elem => elem(idx)
!  /* now put the migrated information into the array */
  current_elem%border = buf(1)
  current_elem%globalID = buf(2)
  current_elem%elem_blk = buf(3)
  current_elem%cpu_wgt = transfer(buf(4),1.0_LB_FLOAT)
  current_elem%mem_wgt = transfer(buf(5),1.0_LB_FLOAT)
  current_elem%nadj = buf(6)
  current_elem%adj_len = buf(7)
  num_nodes = Mesh%eb_nnodes(current_elem%elem_blk)

  size = 7

!  /*
!   * copy the allocated integer fields for this element.
!   */

!  /* copy the connect table */
  if (Mesh%num_dims > 0) then
    allocate(current_elem%connect(0:num_nodes-1),stat=allocstat)
    if (allocstat /= 0) then
      print *, "fatal: insufficient memory"
      ierr = LB_MEMERR
      return
    endif
    do i = 0, num_nodes-1
      current_elem%connect(i) = buf(size + i + 1)
    end do
    size = size + num_nodes
  endif

!  /* copy the adjacency info */
!  /* globalIDs are received; convert to local IDs when adj elem is local */
  allocate(current_elem%adj(0:current_elem%adj_len-1), &
           current_elem%adj_proc(0:current_elem%adj_len-1), stat=allocstat)
  if (allocstat /= 0) then
    print *, "fatal: insufficient memory"
    ierr = LB_MEMERR
    return
  endif
  do i =  0, current_elem%adj_len-1
    current_elem%adj(i) = buf(size + 2*i + 1)
    current_elem%adj_proc(i) = buf(size + 2*i + 2)
    if (current_elem%adj(i) /= -1 .and. current_elem%adj_proc(i) == proc) then
      current_elem%adj(i) = in_list(current_elem%adj(i), &
                                     New_Elem_Index_Size, New_Elem_Index)
    endif
  end do
  size = size + current_elem%adj_len * 2

!  /*
!   * copy the allocated float fields for this element.
!   */

!  /* copy the edge_wgt data */
  if (Use_Edge_Wgts) then
    allocate(current_elem%edge_wgt(0:current_elem%adj_len-1),stat=allocstat)
    if (allocstat /= 0) then
      print *, "fatal: insufficient memory"
      ierr = LB_MEMERR
      return
    endif
    do i = 0, current_elem%adj_len-1
      current_elem%edge_wgt(i) = transfer(buf(size+i+1),1.0_LB_FLOAT)
    end do
    size = size + current_elem%adj_len
  endif

!  /* copy coordinate data */
  if (num_nodes > 0) then
    allocate(current_elem%coord(0:Mesh%num_dims-1,0:num_nodes-1),stat=allocstat)
    if (allocstat /= 0) then
      print *, "fatal: insufficient memory"
      ierr = LB_MEMERR
      return
    endif
    do i = 0, Mesh%num_dims-1
      do j = 0, num_nodes-1
        current_elem%coord(i,j) = transfer(buf(size+i*num_nodes+j+1),1.0_LB_FLOAT)
      end do
    end do
    size = size + num_nodes * Mesh%num_dims
  endif

!  /* and update the Mesh struct */
  Mesh%num_elems = Mesh%num_elems + 1
  Mesh%eb_cnts(current_elem%elem_blk) = Mesh%eb_cnts(current_elem%elem_blk) + 1

  if (size > elem_data_size) then
    ierr = LB_WARN
  else
    ierr = LB_OK
  endif
end subroutine migrate_unpack_elem

!/*****************************************************************************/
!/*****************************************************************************/
!/*****************************************************************************/

subroutine boundary_exchange(vec_len,send_vec,recv_vec)
  integer(LB_INT) :: vec_len           ! /* Length of vector for each element
  integer(LB_INT) :: send_vec(0:)       ! /* Vector of values to be sent.
  integer(LB_INT) :: recv_vec(0:)       ! /* Vector of values to be received.

integer(LB_INT) :: i, ierr, offset
integer(LB_INT) :: msg_type = 111

integer, allocatable :: status(:,:), req(:)

  allocate(req(0:Mesh%necmap-1),status(MPI_STATUS_SIZE,Mesh%necmap))

!  /* Post receives */
  offset = 0
  do i = 0, Mesh%necmap-1
! RISKY old style assumption the address of recv_vec(offset) is passed
    call MPI_Irecv(recv_vec(offset), Mesh%ecmap_cnt(i), MPI_INTEGER, &
                     Mesh%ecmap_id(i), msg_type, MPI_COMM_WORLD, req(i), ierr)
    offset = offset + Mesh%ecmap_cnt(i)
  end do

!  /* Send messages */
  offset = 0
  do i = 0, Mesh%necmap-1
! RISKY old style assumption the address of send_vec(offset) is passed
    call MPI_Send(send_vec(offset), Mesh%ecmap_cnt(i), MPI_INTEGER, &
                    Mesh%ecmap_id(i), msg_type, MPI_COMM_WORLD, ierr)
    offset = offset + Mesh%ecmap_cnt(i)
  end do

!  /* Receive messages */
  call MPI_Waitall(Mesh%necmap, req, status, ierr)

  deallocate(status,req)
end subroutine boundary_exchange

!/*****************************************************************************/
!/*****************************************************************************/
!/*****************************************************************************/
!/*
! * Function that searchs for an element based upon its global ID.
! * This function does not provide the most efficient implementation of
! * the query functions; more efficient implementation uses local IDs
! * to directly access element info.  However, this function is useful
! * for testing Zoltan when the number of entries in a local ID
! * (NUM_LID_ENTRIES) is zero.
! */
function search_by_global_id(mesh, global_id, idx)
type(ELEM_INFO), pointer :: search_by_global_id
type(MESH_INFO),pointer :: mesh
integer(LB_INT) :: global_id
integer(LB_INT) :: idx
integer(LB_INT) :: i
type(ELEM_INFO),pointer :: elem(:), found_elem


  elem => mesh%elements
  nullify(found_elem);

  do i = 0, mesh%elem_array_len-1
    if (elem(i)%globalID == global_id) then
      found_elem => elem(i);
      idx = i;
    endif
  enddo

  search_by_global_id => found_elem;

end function search_by_global_id

end module dr_migrate
