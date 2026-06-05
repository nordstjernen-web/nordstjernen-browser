/* Nordstjernen — hands audio/video URLs off to an external player.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_MEDIA_H
#define NS_MEDIA_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef void (*ns_media_open_uri_fn)(GtkWindow *parent, const char *uri);

void ns_media_set_open_uri_handler(ns_media_open_uri_fn fn);

void ns_media_broker_start(void);

gboolean ns_media_launch_external(GtkWindow *parent, const char *url,
                                  gboolean is_video, gboolean stream);

G_END_DECLS

#endif
