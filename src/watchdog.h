/* Nordstjernen — supervisor that restarts the browser on crash or hang.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef ND_WATCHDOG_H
#define ND_WATCHDOG_H

#include <glib.h>

gboolean    nd_watchdog_should_supervise(int argc, char **argv, gboolean enabled_by_default);
int         nd_watchdog_run_supervisor(const char *self_exe, int argc, char **argv);

gboolean    nd_watchdog_is_child(int argc, char **argv);
void        nd_watchdog_child_guard_parent_death(void);
void        nd_watchdog_child_arm_hang_monitor(int js_budget_ms);
const char *nd_watchdog_child_session_arg(int argc, char **argv);
gboolean    nd_watchdog_child_is_recovery(void);

#endif
