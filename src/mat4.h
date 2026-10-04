/* Nordstjernen — 4x4 transform matrices for CSS 3D rendering.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#ifndef NS_MAT4_H
#define NS_MAT4_H

#include <math.h>
#include <string.h>

typedef struct ns_mat4 {
    double m[16];
} ns_mat4;

static inline void
ns_mat4_identity(ns_mat4 *out)
{
    memset(out->m, 0, sizeof(out->m));
    out->m[0] = out->m[5] = out->m[10] = out->m[15] = 1.0;
}

static inline void
ns_mat4_multiply(const ns_mat4 *a, const ns_mat4 *b, ns_mat4 *out)
{
    ns_mat4 r;
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 4; col++) {
            double s = 0;
            for (int k = 0; k < 4; k++)
                s += a->m[row * 4 + k] * b->m[k * 4 + col];
            r.m[row * 4 + col] = s;
        }
    *out = r;
}

static inline void
ns_mat4_translate(ns_mat4 *m, double x, double y, double z)
{
    ns_mat4 t;
    ns_mat4_identity(&t);
    t.m[3] = x;
    t.m[7] = y;
    t.m[11] = z;
    ns_mat4_multiply(m, &t, m);
}

static inline void
ns_mat4_scale(ns_mat4 *m, double x, double y, double z)
{
    ns_mat4 t;
    ns_mat4_identity(&t);
    t.m[0] = x;
    t.m[5] = y;
    t.m[10] = z;
    ns_mat4_multiply(m, &t, m);
}

static inline void
ns_mat4_rotate_axis(ns_mat4 *m, double x, double y, double z, double deg)
{
    double len = sqrt(x * x + y * y + z * z);
    if (len < 1e-12) return;
    x /= len; y /= len; z /= len;
    double rad = deg * M_PI / 180.0;
    double c = cos(rad), s = sin(rad), ic = 1.0 - c;
    ns_mat4 t;
    ns_mat4_identity(&t);
    t.m[0] = c + x * x * ic;
    t.m[1] = x * y * ic - z * s;
    t.m[2] = x * z * ic + y * s;
    t.m[4] = y * x * ic + z * s;
    t.m[5] = c + y * y * ic;
    t.m[6] = y * z * ic - x * s;
    t.m[8] = z * x * ic - y * s;
    t.m[9] = z * y * ic + x * s;
    t.m[10] = c + z * z * ic;
    ns_mat4_multiply(m, &t, m);
}

static inline void
ns_mat4_skew(ns_mat4 *m, double ax_deg, double ay_deg)
{
    ns_mat4 t;
    ns_mat4_identity(&t);
    t.m[1] = tan(ax_deg * M_PI / 180.0);
    t.m[4] = tan(ay_deg * M_PI / 180.0);
    ns_mat4_multiply(m, &t, m);
}

static inline void
ns_mat4_affine2d(ns_mat4 *m, double a, double b, double c, double d,
                 double e, double f)
{
    ns_mat4 t;
    ns_mat4_identity(&t);
    t.m[0] = a;
    t.m[1] = c;
    t.m[3] = e;
    t.m[4] = b;
    t.m[5] = d;
    t.m[7] = f;
    ns_mat4_multiply(m, &t, m);
}

static inline void
ns_mat4_perspective(ns_mat4 *m, double d)
{
    if (d <= 0) return;
    ns_mat4 t;
    ns_mat4_identity(&t);
    t.m[14] = -1.0 / d;
    ns_mat4_multiply(m, &t, m);
}

static inline void
ns_mat4_apply(const ns_mat4 *m, double x, double y, double z,
              double *ox, double *oy, double *oz, double *ow)
{
    *ox = m->m[0] * x + m->m[1] * y + m->m[2] * z + m->m[3];
    *oy = m->m[4] * x + m->m[5] * y + m->m[6] * z + m->m[7];
    *oz = m->m[8] * x + m->m[9] * y + m->m[10] * z + m->m[11];
    *ow = m->m[12] * x + m->m[13] * y + m->m[14] * z + m->m[15];
}

static inline int
ns_mat4_is_affine2d(const ns_mat4 *m)
{
    const double e = 1e-9;
    return fabs(m->m[2]) < e && fabs(m->m[6]) < e &&
           fabs(m->m[8]) < e && fabs(m->m[9]) < e &&
           fabs(m->m[10] - 1.0) < e && fabs(m->m[11]) < e &&
           fabs(m->m[12]) < e && fabs(m->m[13]) < e &&
           fabs(m->m[14]) < e && fabs(m->m[15] - 1.0) < e;
}

#endif
