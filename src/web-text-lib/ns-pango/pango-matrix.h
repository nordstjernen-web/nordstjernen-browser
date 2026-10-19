/* Pango
 * pango-matrix.h: Matrix manipulation routines
 *
 * Copyright (C) 2002, 2006 Red Hat Software
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

#ifndef __NS_PANGO_MATRIX_H__
#define __NS_PANGO_MATRIX_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _PangoMatrix    NsPangoMatrix;

/**
 * NsPangoMatrix:
 * @xx: 1st component of the transformation matrix
 * @xy: 2nd component of the transformation matrix
 * @yx: 3rd component of the transformation matrix
 * @yy: 4th component of the transformation matrix
 * @x0: x translation
 * @y0: y translation
 *
 * A `NsPangoMatrix` specifies a transformation between user-space
 * and device coordinates.
 *
 * The transformation is given by
 *
 * ```
 * x_device = x_user * matrix->xx + y_user * matrix->xy + matrix->x0;
 * y_device = x_user * matrix->yx + y_user * matrix->yy + matrix->y0;
 * ```
 *
 * Since: 1.6
 */
struct _PangoMatrix
{
  double xx;
  double xy;
  double yx;
  double yy;
  double x0;
  double y0;
};

#define NS_TYPE_PANGO_MATRIX (ns_pango_matrix_get_type ())

/**
 * NS_PANGO_MATRIX_INIT:
 *
 * Constant that can be used to initialize a `NsPangoMatrix` to
 * the identity transform.
 *
 * ```
 * NsPangoMatrix matrix = NS_PANGO_MATRIX_INIT;
 * ns_pango_matrix_rotate (&matrix, 45.);
 * ```
 *
 * Since: 1.6
 **/
#define NS_PANGO_MATRIX_INIT { 1., 0., 0., 1., 0., 0. }

/* for NsPangoRectangle */
#include <ns-pango/pango-types.h>

NS_PANGO_AVAILABLE_IN_1_6
GType ns_pango_matrix_get_type (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_1_6
NsPangoMatrix *ns_pango_matrix_copy   (const NsPangoMatrix *matrix);
NS_PANGO_AVAILABLE_IN_1_6
void         ns_pango_matrix_free   (NsPangoMatrix *matrix);

NS_PANGO_AVAILABLE_IN_1_6
void ns_pango_matrix_translate (NsPangoMatrix *matrix,
			     double       tx,
			     double       ty);
NS_PANGO_AVAILABLE_IN_1_6
void ns_pango_matrix_scale     (NsPangoMatrix *matrix,
			     double       scale_x,
			     double       scale_y);
NS_PANGO_AVAILABLE_IN_1_6
void ns_pango_matrix_rotate    (NsPangoMatrix *matrix,
			     double       degrees);
NS_PANGO_AVAILABLE_IN_1_6
void ns_pango_matrix_concat    (NsPangoMatrix       *matrix,
			     const NsPangoMatrix *new_matrix);
NS_PANGO_AVAILABLE_IN_1_16
void ns_pango_matrix_transform_point    (const NsPangoMatrix *matrix,
				      double            *x,
				      double            *y);
NS_PANGO_AVAILABLE_IN_1_16
void ns_pango_matrix_transform_distance (const NsPangoMatrix *matrix,
				      double            *dx,
				      double            *dy);
NS_PANGO_AVAILABLE_IN_1_16
void ns_pango_matrix_transform_rectangle (const NsPangoMatrix *matrix,
				       NsPangoRectangle    *rect);
NS_PANGO_AVAILABLE_IN_1_16
void ns_pango_matrix_transform_pixel_rectangle (const NsPangoMatrix *matrix,
					     NsPangoRectangle    *rect);
NS_PANGO_AVAILABLE_IN_1_12
double ns_pango_matrix_get_font_scale_factor (const NsPangoMatrix *matrix) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_1_38
void ns_pango_matrix_get_font_scale_factors (const NsPangoMatrix *matrix,
					  double *xscale, double *yscale);
NS_PANGO_AVAILABLE_IN_1_50
double ns_pango_matrix_get_slant_ratio (const NsPangoMatrix *matrix) G_GNUC_PURE;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (NsPangoMatrix, ns_pango_matrix_free)

G_END_DECLS

#endif /* __NS_PANGO_MATRIX_H__ */
