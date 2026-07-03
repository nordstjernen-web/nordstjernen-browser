/* Nordstjernen — GTK tabbed browser window over the out-of-process renderer.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NORDSTJERNEN_GTK_PROCWINDOW_H
#define NORDSTJERNEN_GTK_PROCWINDOW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Run the process-per-tab GTK browser (IPC renderer). Owns its own
 * GtkApplication; returns the application's exit status. When run under the
 * watchdog supervisor, session_path is where the window records its open tab
 * URLs (NULL when unsupervised), and recover requests reopening that session
 * after a crash. */
int ns_procapp_run(const char *startup_url, const char *session_path,
                   gboolean recover, gboolean private_mode);

/* Request a fixed initial window size instead of the maximized default. */
void ns_procapp_set_window_size(int width, int height);

G_END_DECLS

#endif
