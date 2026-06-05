/* Nordstjernen — supervisor that restarts the browser on crash or hang.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "watchdog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>
#include <glib/gstdio.h>

#ifdef G_OS_WIN32
#include <windows.h>
#else
#include <glib-unix.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#endif

#ifdef __linux__
#include <sys/prctl.h>
#endif

#define NS_WATCHDOG_FLAG           "--watchdog"
#define NS_WATCHDOG_NO_FLAG        "--no-watchdog"
#define NS_WATCHDOG_CHILD_FLAG     "--watchdog-child"
#define NS_WATCHDOG_SESSION_PREFIX "--watchdog-session="
#define NS_WATCHDOG_RECOVER_ENV    "NS_WATCHDOG_RECOVER"

#define NS_WATCHDOG_BEAT_SECS      2
#define NS_WATCHDOG_CHECK_SECS     1
#define NS_WATCHDOG_HANG_MIN_SECS  30
#define NS_WATCHDOG_BACKOFF_MS     1000
#define NS_WATCHDOG_BURST_MAX      5
#define NS_WATCHDOG_BURST_SECS     60
#define NS_WATCHDOG_STOP_GRACE_SECS 3
#define NS_WATCHDOG_HANG_EXIT      70

static gint g_beat;
static int  g_hang_secs;

static gboolean
ns_watchdog_beat(gpointer user_data)
{
    (void)user_data;
    g_atomic_int_inc(&g_beat);
    return G_SOURCE_CONTINUE;
}

static gpointer
ns_watchdog_hang_thread(gpointer user_data)
{
    (void)user_data;
    gint   last = g_atomic_int_get(&g_beat);
    gint64 last_change = g_get_monotonic_time();
    for (;;) {
        g_usleep((gulong)NS_WATCHDOG_CHECK_SECS * G_USEC_PER_SEC);
        gint   cur = g_atomic_int_get(&g_beat);
        gint64 now = g_get_monotonic_time();
        if (cur != last) {
            last = cur;
            last_change = now;
            continue;
        }
        if (now - last_change > (gint64)g_hang_secs * G_USEC_PER_SEC) {
            fprintf(stderr,
                    "ns_watchdog: main loop unresponsive for %ds — exiting for restart\n",
                    g_hang_secs);
            fflush(stderr);
            _Exit(NS_WATCHDOG_HANG_EXIT);
        }
    }
    return NULL;
}

void
ns_watchdog_child_guard_parent_death(void)
{
#ifdef __linux__
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (getppid() == 1)
        _Exit(0);
#endif
}

void
ns_watchdog_child_arm_hang_monitor(int js_budget_ms)
{
    g_hang_secs = js_budget_ms / 1000 + NS_WATCHDOG_HANG_MIN_SECS;
    if (g_hang_secs < NS_WATCHDOG_HANG_MIN_SECS)
        g_hang_secs = NS_WATCHDOG_HANG_MIN_SECS;
    g_message("ns_watchdog: hang monitor armed, exit after %ds unresponsive",
              g_hang_secs);
    g_timeout_add_seconds(NS_WATCHDOG_BEAT_SECS, ns_watchdog_beat, NULL);
    g_thread_unref(g_thread_new("nd-watchdog", ns_watchdog_hang_thread, NULL));
}

static const char *
ns_watchdog_arg_value(int argc, char **argv, const char *prefix)
{
    for (int i = 1; i < argc; i++)
        if (argv[i] && g_str_has_prefix(argv[i], prefix))
            return argv[i] + strlen(prefix);
    return NULL;
}

const char *
ns_watchdog_child_session_arg(int argc, char **argv)
{
    return ns_watchdog_arg_value(argc, argv, NS_WATCHDOG_SESSION_PREFIX);
}

gboolean
ns_watchdog_is_child(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (argv[i] && g_strcmp0(argv[i], NS_WATCHDOG_CHILD_FLAG) == 0)
            return TRUE;
    return FALSE;
}

gboolean
ns_watchdog_child_is_recovery(void)
{
    const char *v = g_getenv(NS_WATCHDOG_RECOVER_ENV);
    return v && g_strcmp0(v, "1") == 0;
}

static gboolean
ns_watchdog_is_oneshot(const char *arg)
{
    return g_strcmp0(arg, "--headless")     == 0 ||
           g_strcmp0(arg, "--print-config") == 0 ||
           g_str_has_prefix(arg, "--dump=")       ||
           g_str_has_prefix(arg, "--eval=")       ||
           g_str_has_prefix(arg, "--inspect=")    ||
           g_str_has_prefix(arg, "--inspect-at=");
}

gboolean
ns_watchdog_should_supervise(int argc, char **argv, gboolean enabled_by_default)
{
    gboolean forced = FALSE;
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;
        if (g_strcmp0(argv[i], NS_WATCHDOG_CHILD_FLAG) == 0) return FALSE;
        if (g_strcmp0(argv[i], NS_WATCHDOG_NO_FLAG) == 0)    return FALSE;
        if (ns_watchdog_is_oneshot(argv[i]))                 return FALSE;
        if (g_strcmp0(argv[i], NS_WATCHDOG_FLAG) == 0)       forced = TRUE;
    }
    return forced || enabled_by_default;
}

typedef struct {
    char      **child_argv;
    char       *session_path;
    GMainLoop  *loop;
    GPid        pid;
    gboolean    have_pid;
    gboolean    stopping;
    guint       watch_id;
    int         burst_count;
    gint64      burst_start_us;
    int         exit_status;
} ns_watchdog;

static void
ns_watchdog_kill(GPid pid, gboolean force)
{
#ifdef G_OS_WIN32
    (void)force;
    TerminateProcess((HANDLE)pid, 1);
#else
    kill((pid_t)pid, force ? SIGKILL : SIGTERM);
#endif
}

static gboolean ns_watchdog_spawn(ns_watchdog *wd, gboolean recover);

static gboolean
ns_watchdog_respawn_cb(gpointer user_data)
{
    ns_watchdog *wd = user_data;
    if (!wd->stopping)
        ns_watchdog_spawn(wd, TRUE);
    return G_SOURCE_REMOVE;
}

static void
ns_watchdog_schedule_restart(ns_watchdog *wd)
{
    gint64 now = g_get_monotonic_time();
    if (now - wd->burst_start_us > (gint64)NS_WATCHDOG_BURST_SECS * G_USEC_PER_SEC) {
        wd->burst_start_us = now;
        wd->burst_count = 0;
    }
    wd->burst_count++;
    if (wd->burst_count > NS_WATCHDOG_BURST_MAX) {
        g_warning("ns_watchdog: child failed %d times in under %ds — giving up",
                  wd->burst_count, NS_WATCHDOG_BURST_SECS);
        wd->exit_status = 1;
        g_main_loop_quit(wd->loop);
        return;
    }
    g_message("ns_watchdog: restarting browser (attempt %d)", wd->burst_count);
    g_timeout_add(NS_WATCHDOG_BACKOFF_MS, ns_watchdog_respawn_cb, wd);
}

static void
ns_watchdog_child_exited(GPid pid, gint status, gpointer user_data)
{
    ns_watchdog *wd = user_data;
    g_spawn_close_pid(pid);
    wd->have_pid = FALSE;
    wd->watch_id = 0;

    if (wd->stopping) {
        g_main_loop_quit(wd->loop);
        return;
    }

    gboolean clean;
#ifdef G_OS_WIN32
    clean = (status == 0);
#else
    clean = WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
    if (clean) {
        wd->exit_status = 0;
        g_main_loop_quit(wd->loop);
        return;
    }

#ifdef G_OS_WIN32
    g_warning("ns_watchdog: browser exited abnormally (code %d)", status);
#else
    if (WIFSIGNALED(status))
        g_warning("ns_watchdog: browser stopped by signal %d", WTERMSIG(status));
    else
        g_warning("ns_watchdog: browser exited with code %d", WEXITSTATUS(status));
#endif
    ns_watchdog_schedule_restart(wd);
}

static gboolean
ns_watchdog_spawn(ns_watchdog *wd, gboolean recover)
{
    GError *err = NULL;
    GPid pid = 0;
    char **envp = g_get_environ();
    if (recover)
        envp = g_environ_setenv(envp, NS_WATCHDOG_RECOVER_ENV, "1", TRUE);
    else
        envp = g_environ_unsetenv(envp, NS_WATCHDOG_RECOVER_ENV);
    gboolean ok = g_spawn_async(NULL, wd->child_argv, envp,
                                G_SPAWN_DO_NOT_REAP_CHILD,
                                NULL, NULL, &pid, &err);
    g_strfreev(envp);
    if (!ok) {
        g_warning("ns_watchdog: failed to launch browser: %s",
                  err ? err->message : "unknown error");
        g_clear_error(&err);
        wd->exit_status = 1;
        g_main_loop_quit(wd->loop);
        return FALSE;
    }
    wd->pid = pid;
    wd->have_pid = TRUE;
    wd->watch_id = g_child_watch_add(pid, ns_watchdog_child_exited, wd);
    return TRUE;
}

#ifndef G_OS_WIN32
static gboolean
ns_watchdog_force_kill_cb(gpointer user_data)
{
    ns_watchdog *wd = user_data;
    if (wd->have_pid)
        ns_watchdog_kill(wd->pid, TRUE);
    return G_SOURCE_REMOVE;
}

static gboolean
ns_watchdog_signal_cb(gpointer user_data)
{
    ns_watchdog *wd = user_data;
    if (!wd->stopping) {
        wd->stopping = TRUE;
        wd->exit_status = 0;
        if (wd->have_pid) {
            ns_watchdog_kill(wd->pid, FALSE);
            g_timeout_add_seconds(NS_WATCHDOG_STOP_GRACE_SECS,
                                  ns_watchdog_force_kill_cb, wd);
        } else {
            g_main_loop_quit(wd->loop);
        }
    }
    return G_SOURCE_CONTINUE;
}
#endif

static char **
ns_watchdog_build_child_argv(const char *self_exe, int argc, char **argv,
                             const char *session_path)
{
    GPtrArray *args = g_ptr_array_new();
    g_ptr_array_add(args, g_strdup(self_exe));
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;
        if (g_strcmp0(argv[i], NS_WATCHDOG_FLAG) == 0) continue;
        if (g_strcmp0(argv[i], NS_WATCHDOG_NO_FLAG) == 0) continue;
        if (g_strcmp0(argv[i], NS_WATCHDOG_CHILD_FLAG) == 0) continue;
        if (g_str_has_prefix(argv[i], NS_WATCHDOG_SESSION_PREFIX)) continue;
        g_ptr_array_add(args, g_strdup(argv[i]));
    }
    g_ptr_array_add(args, g_strdup(NS_WATCHDOG_CHILD_FLAG));
    g_ptr_array_add(args, g_strconcat(NS_WATCHDOG_SESSION_PREFIX, session_path, NULL));
    g_ptr_array_add(args, NULL);
    return (char **)g_ptr_array_free(args, FALSE);
}

int
ns_watchdog_run_supervisor(const char *self_exe, int argc, char **argv)
{
    ns_watchdog wd = { 0 };

    char *uuid = g_uuid_string_random();
    char *name = g_strconcat("nordstjernen-watchdog-", uuid, ".session", NULL);
    wd.session_path = g_build_filename(g_get_user_runtime_dir(), name, NULL);
    g_free(name);
    g_free(uuid);

    wd.child_argv = ns_watchdog_build_child_argv(self_exe, argc, argv,
                                                 wd.session_path);
    wd.loop = g_main_loop_new(NULL, FALSE);
    wd.burst_start_us = g_get_monotonic_time();

#ifndef G_OS_WIN32
    g_unix_signal_add(SIGINT, ns_watchdog_signal_cb, &wd);
    g_unix_signal_add(SIGTERM, ns_watchdog_signal_cb, &wd);
#endif

    g_message("ns_watchdog: supervising browser");

    if (ns_watchdog_spawn(&wd, FALSE))
        g_main_loop_run(wd.loop);

    if (wd.watch_id) g_source_remove(wd.watch_id);
    if (wd.have_pid) {
        ns_watchdog_kill(wd.pid, TRUE);
        g_spawn_close_pid(wd.pid);
    }
    g_main_loop_unref(wd.loop);
    g_unlink(wd.session_path);
    g_strfreev(wd.child_argv);
    g_free(wd.session_path);
    return wd.exit_status;
}
