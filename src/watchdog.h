/* Nordstjernen — supervisor that restarts the browser on crash or hang.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_WATCHDOG_H
#define NS_WATCHDOG_H

#include <glib.h>

gboolean    ns_watchdog_should_supervise(int argc, char **argv, gboolean enabled_by_default);
int         ns_watchdog_run_supervisor(const char *self_exe, int argc, char **argv);

gboolean    ns_watchdog_is_child(int argc, char **argv);
int         ns_watchdog_supervisor_pid(void);
void        ns_watchdog_child_guard_parent_death(void);
void        ns_watchdog_child_arm_hang_monitor(int js_budget_ms);
const char *ns_watchdog_child_session_arg(int argc, char **argv);
gboolean    ns_watchdog_child_is_recovery(void);

#endif
