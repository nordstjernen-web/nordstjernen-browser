/* Nordstjernen — right-click context menu over the rendered document.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_CTXMENU_H
#define NS_CTXMENU_H

#include <gtk/gtk.h>

#include "window.h"

G_BEGIN_DECLS

void ns_install_ctx_actions(ns_window *w);
void ns_on_drawing_right_pressed(GtkGestureClick *gesture, int n_press,
                                 double x, double y, gpointer user_data);
void ns_on_drawing_long_pressed(GtkGestureLongPress *gesture,
                                double x, double y, gpointer user_data);

G_END_DECLS

#endif
