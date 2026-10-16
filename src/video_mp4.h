/* Nordstjernen — MP4 playback over the in-tree demuxer and the OS H.264 decoder, for builds without libav.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#ifndef NS_VIDEO_MP4_H
#define NS_VIDEO_MP4_H

#include <glib.h>

#include "texture.h"

G_BEGIN_DECLS

typedef struct ns_mp4_player ns_mp4_player;

ns_mp4_player *ns_mp4_player_new(const guint8 *bytes, gsize len, int *out_w,
                                 int *out_h, double *out_duration,
                                 double *out_end, int *out_has_audio);
void     ns_mp4_player_free(ns_mp4_player *p);
gboolean ns_mp4_player_next(ns_mp4_player *p, ns_texture **out_tex,
                            double *out_pts);
gboolean ns_mp4_player_seek(ns_mp4_player *p, double seconds);
void     ns_mp4_player_skip_before(ns_mp4_player *p, double seconds);
gboolean ns_mp4_player_extend(ns_mp4_player *p, const guint8 *bytes, gsize len,
                              double *out_end);
gboolean ns_mp4_probe_chunk_range(const guint8 *init, gsize init_len,
                                  const guint8 *chunk, gsize chunk_len,
                                  double *out_start, double *out_end);

G_END_DECLS

#endif
