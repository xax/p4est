/*
  This file is part of p4est.
  p4est is a C library to manage a collection (a forest) of multiple
  connected adaptive quadtrees or octrees in parallel.

  Copyright (C) 2010 The University of Texas System
  Written by Carsten Burstedde, Lucas C. Wilcox, and Tobin Isaac

  p4est is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  p4est is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with p4est; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/
#include <unistd.h>
#ifndef P4_TO_P8
#include <p4est_extended.h>
#include <p4est_ghost.h>
#include <p4est_mesh.h>
#include <p4est_vtk.h>
#else /* !P4_TO_P8 */
#include <p8est_extended.h>
#include <p8est_ghost.h>
#include <p8est_mesh.h>
#include <p8est_vtk.h>
#endif /* !P4_TO_P8 */
/* Function for refining a mesh exactly once, in the very first quadrant
 *
 * \param [in] p4est        The forest.
 * \param [in] which_tree   The tree index of the current quadrant \a q
 * \param [in] q            The currently considered quadrant
 */
static int
refineExactlyOnce (p4est_t * p4est, p4est_topidx_t which_tree,
                   p4est_quadrant_t * q)
{
  int                 dec;
#ifndef P4_TO_P8
  dec = q->x == 0 && q->y == 0 && which_tree == 0;
#else /* !P4_TO_P8 */
  dec = q->x == 0 && q->y == 0 && q->z == 0 && which_tree == 0;
#endif /* !P4_TO_P8 */
  if (dec) {
    return 1;
  }
  return 0;
}

/* Function prototype to check the created mesh
 *
 * \param [in] p4est    The forest.
 * \param [in] ghost    The process-local ghost-layer.
 * \param [in] mesh     The process-local mesh.
 */
int
check_mesh (p4est_t * p4est, p4est_ghost_t * ghost, p4est_mesh_t * mesh)
{
  for (uint32_t c = 0; c < p4est->local_num_quadrants; ++c) {
    printf ("[p4est %i] Cell %i\n", p4est->mpirank,
            p4est->global_first_quadrant[p4est->mpirank] + c);
    for (uint32_t f = 0; f < P4EST_FACES; ++f) {
      char               *ghostInfo;
      int                 idx;
      int                 enc = mesh->quad_to_face[P4EST_FACES * c + f];
      if (mesh->quad_to_quad[P4EST_FACES * c + f] <
          p4est->local_num_quadrants) {
        ghostInfo = "";
        idx = mesh->quad_to_quad[P4EST_FACES * c + f];
      }
      else {
        ghostInfo = "(g)";
        p4est_quadrant_t   *q = sc_array_index_int (&ghost->ghosts,
                                                    mesh->quad_to_quad
                                                    [P4EST_FACES * c + f] -
                                                    p4est->local_num_quadrants);
        p4est_tree_t       *tree;
        int                 offset = 0;
        tree = (p4est_tree_t *) sc_array_index_int (p4est->trees,
                                                    q->p.piggy3.which_tree);
        offset =
          tree->quadrants_offset +
          p4est->global_first_quadrant[q->p.piggy1.owner_rank];
        idx = q->p.piggy3.local_num + offset;
      }

      printf ("[p4est %i] Face neighbor %i: index %i, encoding %i %s\n",
              p4est->mpirank, f, idx, enc, ghostInfo);
    }
  }

  return 0;
}

/* Function for testing p4est-mesh for a single tree scenario
 *
 * \param [in] p4est     The forest.
 * \param [in] conn      The connectivity structure
 * \param [in] periodic  Flag for checking if we have periodic boundaries
 * \returns 0 for success, -1 for failure
 */
int
test_mesh_one_tree (p4est_t * p4est,
                    p4est_connectivity_t * conn,
                    int8_t periodic, sc_MPI_Comm mpicomm)
{
  /* ensure that we have null pointers at beginning and end of function */
  P4EST_ASSERT (p4est == NULL);
  P4EST_ASSERT (conn == NULL);

  /* create connectivity */
#ifndef P4_TO_P8
  conn =
    periodic == 1 ? p4est_connectivity_new_periodic () :
    p4est_connectivity_new_unitsquare ();
#else /* !P4_TO_P8 */
  conn =
    periodic == 1 ? p8est_connectivity_new_periodic () :
    p8est_connectivity_new_unitcube ();
#endif /* !P4_TO_P8 */

  /* setup p4est */
  int                 minLevel = 1;
  p4est = p4est_new_ext (mpicomm, conn, 0, minLevel, 0, 0, 0, 0);
  p4est_refine (p4est, 0, refineExactlyOnce, 0);
  p4est_partition (p4est, 0, 0);
  p4est_balance (p4est, P4EST_CONNECT_FULL, 0);

  /* inspect setup of geometry */
  char                filename[35] = "test_mesh_setup_single_tree_";
  strcat (filename, P4EST_STRING);
  p4est_vtk_write_file (p4est, 0, filename);

  /* create mesh */
  p4est_ghost_t      *ghost = p4est_ghost_new (p4est, P4EST_CONNECT_FULL);
  p4est_mesh_t       *mesh =
    p4est_mesh_new_ext (p4est, ghost, 1, 1, P4EST_CONNECT_FULL);

  /* check mesh */
  sleep (p4est->mpirank);
  check_mesh (p4est, ghost, mesh);

  sc_MPI_Barrier (p4est->mpicomm);

  /* cleanup */
  p4est_ghost_destroy (ghost);
  p4est_mesh_destroy (mesh);
  p4est_destroy (p4est);
  p4est_connectivity_destroy (conn);
  conn = 0;
  p4est = 0;
  P4EST_ASSERT (p4est == NULL);
  P4EST_ASSERT (conn == NULL);

  return 0;
}

/* Function for testing p4est-mesh for multiple trees in a brick scenario
 *
 * \param [in] p4est     The forest.
 * \param [in] conn      The connectivity structure
 * \param [in] periodic  Flag for checking if we have periodic boundaries
 * \returns 0 for success, -1 for failure
 */
int
test_mesh_multiple_trees_brick (p4est_t * p4est,
                                p4est_connectivity_t * conn,
                                int8_t periodic, sc_MPI_Comm mpicomm)
{
  /* ensure that we have null pointers at beginning and end of function */
  P4EST_ASSERT (p4est == NULL);
  P4EST_ASSERT (conn == NULL);
  /* create connectivity */
#ifndef P4_TO_P8
  conn = p4est_connectivity_new_brick (2, 2, periodic, periodic);
#else /* !P4_TO_P8 */
  conn = p8est_connectivity_new_brick (2, 2, 2, periodic, periodic, periodic);
#endif /* !P4_TO_P8 */
  /* setup p4est */
  int                 minLevel = 0;
  p4est = p4est_new_ext (mpicomm, conn, 0, minLevel, 0, 0, 0, 0);
  p4est_refine (p4est, 0, refineExactlyOnce, 0);
  p4est_partition (p4est, 0, 0);
  p4est_balance (p4est, P4EST_CONNECT_FULL, 0);
  char                filename[29] = "test_mesh_setup_brick_";
  strcat (filename, P4EST_STRING);
  p4est_vtk_write_file (p4est, 0, filename);
  /* create mesh */
  p4est_ghost_t      *ghost = p4est_ghost_new (p4est, P4EST_CONNECT_FULL);
  p4est_mesh_t       *mesh =
    p4est_mesh_new_ext (p4est, ghost, 1, 1, P4EST_CONNECT_FULL);
  /* cleanup */
  p4est_ghost_destroy (ghost);
  p4est_mesh_destroy (mesh);
  p4est_destroy (p4est);
  p4est_connectivity_destroy (conn);
  conn = 0;
  p4est = 0;
  P4EST_ASSERT (p4est == NULL);
  P4EST_ASSERT (conn == NULL);
  return 0;
}

/* Function for testing p4est-mesh for multiple trees in a non-brick scenario
 *
 * \param [in] p4est     The forest.
 * \param [in] conn      The connectivity structure
 * \param [in] periodic  Flag for checking if we have periodic boundaries
 * \returns 0 for success, -1 for failure
 */
int
test_mesh_multiple_trees_nonbrick (p4est_t * p4est,
                                   p4est_connectivity_t * conn,
                                   int8_t periodic, sc_MPI_Comm mpicomm)
{
  return 0;
}

int
main (int argc, char **argv)
{
  sc_MPI_Comm         mpicomm;
  int                 mpiret;
  int                 mpisize, mpirank;
  p4est_t            *p4est;
  p4est_connectivity_t *conn;
  int8_t              periodic_boundaries;
  /* initialize MPI */
  mpiret = sc_MPI_Init (&argc, &argv);
  SC_CHECK_MPI (mpiret);
  mpicomm = sc_MPI_COMM_WORLD;
  mpiret = sc_MPI_Comm_size (mpicomm, &mpisize);
  SC_CHECK_MPI (mpiret);
  mpiret = sc_MPI_Comm_rank (mpicomm, &mpirank);
  SC_CHECK_MPI (mpiret);
  sc_init (mpicomm, 1, 1, NULL, SC_LP_DEFAULT);
  p4est_init (NULL, SC_LP_DEFAULT);
  p4est = 0;
  conn = 0;
  /* test both periodic and non-periodic boundaries */
  /* test one tree */
  periodic_boundaries = 0;

  test_mesh_one_tree (p4est, conn, periodic_boundaries, mpicomm);
  periodic_boundaries = 1;

  test_mesh_one_tree (p4est, conn, periodic_boundaries, mpicomm);

  /* test multiple trees; brick */
  /*
     periodic_boundaries = 0;
     test_mesh_multiple_trees_brick (p4est, conn, periodic_boundaries, mpicomm);
     periodic_boundaries = 1;
     test_mesh_multiple_trees_brick (p4est, conn, periodic_boundaries, mpicomm);
   */

  /* test multiple trees; non-brick */
  /*
     periodic_boundaries = 0;
     test_mesh_multiple_trees_nonbrick (p4est, conn,
     periodic_boundaries, mpicomm);
     periodic_boundaries = 1;
     test_mesh_multiple_trees_nonbrick (p4est, conn,
     periodic_boundaries, mpicomm);
   */
  /* exit */
  sc_finalize ();
  mpiret = sc_MPI_Finalize ();
  SC_CHECK_MPI (mpiret);
  return 0;
}
