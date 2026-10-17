/* Nordstjernen — GTK tabbed browser window over the out-of-process renderer.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#ifndef NORDSTJERNEN_GTK_PROCWINDOW_H
#define NORDSTJERNEN_GTK_PROCWINDOW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

int ns_procapp_run(const char *startup_url, const char *session_path,
                   gboolean recover, gboolean private_mode);

void ns_procapp_set_window_size(int width, int height);

G_END_DECLS

#endif
