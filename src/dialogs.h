/* Nordstjernen — chrome dialogs: About, Settings, and the bookmarks list.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_DIALOGS_H
#define NS_DIALOGS_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

void on_about_clicked     (GtkButton *button, gpointer user_data);
void on_logo_clicked      (GtkButton *button, gpointer user_data);
void on_settings_clicked  (GtkButton *button, gpointer user_data);
void on_bookmarks_clicked (GtkButton *button, gpointer user_data);

G_END_DECLS

#endif
