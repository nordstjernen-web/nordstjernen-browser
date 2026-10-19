/* Pango
 * pango-matrix.c: Matrix manipulation routines
 *
 * Copyright (C) 2000, 2006 Red Hat Software
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"
#include <stdlib.h>
#include <math.h>

#include "pango-matrix.h"
#include "pango-impl-utils.h"

/**
 * ns_pango_matrix_copy:
 * @matrix: (nullable): a `NsPangoMatrix`
 *
 * Copies a `NsPangoMatrix`.
 *
 * Return value: (nullable): the newly allocated `NsPangoMatrix`
 *
 * Since: 1.6
 */
NsPangoMatrix *
ns_pango_matrix_copy (const NsPangoMatrix *matrix)
{
  NsPangoMatrix *new_matrix;

  if (matrix == NULL)
    return NULL;

  new_matrix = g_slice_new (NsPangoMatrix);

  *new_matrix = *matrix;

  return new_matrix;
}

/**
 * ns_pango_matrix_free:
 * @matrix: (nullable): a `NsPangoMatrix`, may be %NULL
 *
 * Free a `NsPangoMatrix`.
 *
 * Since: 1.6
 */
void
ns_pango_matrix_free (NsPangoMatrix *matrix)
{
  if (matrix == NULL)
    return;

  g_slice_free (NsPangoMatrix, matrix);
}

/**
 * ns_pango_matrix_concat:
 * @matrix: a `NsPangoMatrix`
 * @new_matrix: a `NsPangoMatrix`
 *
 * Changes the transformation represented by @matrix to be the
 * transformation given by first applying transformation
 * given by @new_matrix then applying the original transformation.
 *
 * Since: 1.6
 */
void
ns_pango_matrix_concat (NsPangoMatrix       *matrix,
		     const NsPangoMatrix *new_matrix)
{
  NsPangoMatrix tmp;

  g_return_if_fail (matrix != NULL);

  tmp = *matrix;

  matrix->xx = tmp.xx * new_matrix->xx + tmp.xy * new_matrix->yx;
  matrix->xy = tmp.xx * new_matrix->xy + tmp.xy * new_matrix->yy;
  matrix->yx = tmp.yx * new_matrix->xx + tmp.yy * new_matrix->yx;
  matrix->yy = tmp.yx * new_matrix->xy + tmp.yy * new_matrix->yy;
  matrix->x0  = tmp.xx * new_matrix->x0 + tmp.xy * new_matrix->y0 + tmp.x0;
  matrix->y0  = tmp.yx * new_matrix->x0 + tmp.yy * new_matrix->y0 + tmp.y0;
}

/**
 * ns_pango_matrix_get_font_scale_factor:
 * @matrix: (nullable): a `NsPangoMatrix`, may be %NULL
 *
 * Returns the scale factor of a matrix on the height of the font.
 *
 * That is, the scale factor in the direction perpendicular to the
 * vector that the X coordinate is mapped to.  If the scale in the X
 * coordinate is needed as well, use [method@Pango.Matrix.get_font_scale_factors].
 *
 * Return value: the scale factor of @matrix on the height of the font,
 *   or 1.0 if @matrix is %NULL.
 *
 * Since: 1.12
 */
double
ns_pango_matrix_get_font_scale_factor (const NsPangoMatrix *matrix)
{
  double yscale;
  ns_pango_matrix_get_font_scale_factors (matrix, NULL, &yscale);
  return yscale;
}

/**
 * ns_pango_matrix_get_font_scale_factors:
 * @matrix: (nullable): a `NsPangoMatrix`
 * @xscale: (out) (optional): output scale factor in the x direction
 * @yscale: (out) (optional): output scale factor perpendicular to the x direction
 *
 * Calculates the scale factor of a matrix on the width and height of the font.
 *
 * That is, @xscale is the scale factor in the direction of the X coordinate,
 * and @yscale is the scale factor in the direction perpendicular to the
 * vector that the X coordinate is mapped to.
 *
 * Note that output numbers will always be non-negative.
 *
 * Since: 1.38
 **/
void
ns_pango_matrix_get_font_scale_factors (const NsPangoMatrix *matrix,
				     double *xscale, double *yscale)
{
/*
 * Based on cairo-matrix.c:_cairo_matrix_compute_scale_factors()
 *
 * Copyright 2005, Keith Packard
 */
  double major = 1., minor = 1.;

  if (matrix)
    {
      double x = matrix->xx;
      double y = matrix->yx;
      major = sqrt (x*x + y*y);

      if (major)
	{
	  double det = matrix->xx * matrix->yy - matrix->yx * matrix->xy;

	  /*
	   * ignore mirroring
	   */
	  if (det < 0)
	    det = - det;

	  minor = det / major;
	}
      else
        minor = 0.;
    }

  if (xscale)
    *xscale = major;
  if (yscale)
    *yscale = minor;
}

/**
 * ns_pango_matrix_get_slant_ratio:
 * @matrix: a `NsPangoMatrix`
 *
 * Gets the slant ratio of a matrix.
 *
 * For a simple shear matrix in the form:
 *
 *     1 λ
 *     0 1
 *
 * this is simply λ.
 *
 * Returns: the slant ratio of @matrix
 *
 * Since: 1.50
 */
double
ns_pango_matrix_get_slant_ratio (const NsPangoMatrix *matrix)
{
  double x0, y0;
  double x1, y1;

  x0 = 0;
  y0 = 1;
  ns_pango_matrix_transform_distance (matrix, &x0, &y0);

  x1 = 1;
  y1 = 0;
  ns_pango_matrix_transform_distance (matrix, &x1, &y1);

  return (x0 * x1 + y0 * y1) / (x0 * x0 + y0 * y0);
}

/**
 * ns_pango_matrix_transform_distance:
 * @matrix: (nullable): a `NsPangoMatrix`
 * @dx: (inout): in/out X component of a distance vector
 * @dy: (inout): in/out Y component of a distance vector
 *
 * Transforms the distance vector (@dx,@dy) by @matrix.
 *
 * This is similar to [method@Pango.Matrix.transform_point],
 * except that the translation components of the transformation
 * are ignored. The calculation of the returned vector is as follows:
 *
 * ```
 * dx2 = dx1 * xx + dy1 * xy;
 * dy2 = dx1 * yx + dy1 * yy;
 * ```
 *
 * Affine transformations are position invariant, so the same vector
 * always transforms to the same vector. If (@x1,@y1) transforms
 * to (@x2,@y2) then (@x1+@dx1,@y1+@dy1) will transform to
 * (@x1+@dx2,@y1+@dy2) for all values of @x1 and @x2.
 *
 * Since: 1.16
 */
void
ns_pango_matrix_transform_distance (const NsPangoMatrix *matrix,
				 double            *dx,
				 double            *dy)
{
  if (matrix)
    {
      double new_x, new_y;

      new_x = (matrix->xx * *dx + matrix->xy * *dy);
      new_y = (matrix->yx * *dx + matrix->yy * *dy);

      *dx = new_x;
      *dy = new_y;
    }
}
