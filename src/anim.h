/* Nordstjernen — CSS transitions and @keyframes animation engine.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_ANIM_H
#define NS_ANIM_H

#include <glib.h>

#include "css.h"
#include "dom.h"

G_BEGIN_DECLS

typedef struct ns_anim ns_anim;

ns_anim *ns_anim_new(void);
void     ns_anim_free(ns_anim *a);

void     ns_anim_load_from_stylesheet(ns_anim *a, const ns_css_stylesheet *sh);

void     ns_anim_observe(ns_anim *a, const ns_node *dom,
                         const ns_style *style, gint64 now_us);

gboolean ns_anim_tick(ns_anim *a, gint64 now_us);

typedef void (*ns_anim_event_cb)(const ns_node *node, const char *type,
                                 const char *name, double elapsed_ms,
                                 gpointer user);
void     ns_anim_drain_events(ns_anim *a, ns_anim_event_cb cb, gpointer user);

gboolean                 ns_anim_get_opacity   (ns_anim *a,
                                                const ns_node *dom,
                                                double *out_opacity);
const ns_css_transform  *ns_anim_get_transform (ns_anim *a,
                                                const ns_node *dom);
gboolean                 ns_anim_get_color     (ns_anim *a,
                                                const ns_node *dom,
                                                ns_css_anim_target which,
                                                guint8 out_rgba[4]);

void     ns_anim_prune(ns_anim *a, GHashTable *live);

void     ns_anim_rebase(ns_anim *a, gint64 base_us);

G_END_DECLS

#endif
