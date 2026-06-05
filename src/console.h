/* Nordstjernen — developer console window (JS REPL, profiler, debug log). */

#ifndef NS_CONSOLE_H
#define NS_CONSOLE_H

#include <gtk/gtk.h>

#include "window.h"

G_BEGIN_DECLS

void ns_window_console_append(ns_window *w, const char *line);
void ns_window_open_console(ns_window *w);
void ns_window_console_close(ns_window *w);
void on_win_open_console(GSimpleAction *action, GVariant *parameter,
                         gpointer user_data);

G_END_DECLS

#endif
