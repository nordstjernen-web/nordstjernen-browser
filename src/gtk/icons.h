/* Nordstjernen — the shell's own icons, drawn by the in-engine SVG renderer.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#ifndef NORDSTJERNEN_GTK_ICONS_H
#define NORDSTJERNEN_GTK_ICONS_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

GdkPaintable *ns_icon_lookup(const char *name);

GtkWidget *ns_icon_image_new(const char *name);

void ns_icon_image_set(GtkImage *image, const char *name);

GtkWidget *ns_icon_button_new(const char *name);

void ns_icon_button_set(GtkButton *button, const char *name);

void ns_icon_menu_button_set(GtkMenuButton *button, const char *name);

void ns_icon_entry_set(GtkEntry *entry, GtkEntryIconPosition pos,
                       const char *name);

void ns_icon_install_window_icon(const char *name);

G_END_DECLS

#endif
