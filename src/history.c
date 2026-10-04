/* Nordstjernen — SQLite-backed browsing history.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#include "history.h"
#include "about_style.h"
#include "config.h"

#include <glib/gstdio.h>
#include <sqlite3.h>
#include <string.h>

#define NS_HISTORY_MAX_ROWS  10000
#define NS_HISTORY_PAGE_ROWS 200

static sqlite3 *g_history_db;
static gboolean g_history_disabled;
static GMutex   g_history_mutex;

static void
history_harden(sqlite3 *db)
{
#ifdef SQLITE_DBCONFIG_DEFENSIVE
    sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, NULL);
#endif
    sqlite3_db_config(db, SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 0, NULL);
#ifdef SQLITE_DBCONFIG_TRUSTED_SCHEMA
    sqlite3_db_config(db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, NULL);
#endif
#ifdef SQLITE_DBCONFIG_DQS_DDL
    sqlite3_db_config(db, SQLITE_DBCONFIG_DQS_DDL, 0, NULL);
#endif
#ifdef SQLITE_DBCONFIG_DQS_DML
    sqlite3_db_config(db, SQLITE_DBCONFIG_DQS_DML, 0, NULL);
#endif
}

static gboolean
history_exec(const char *sql)
{
    if (!g_history_db) return FALSE;
    char *err = NULL;
    int rc = sqlite3_exec(g_history_db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK)
        g_warning("history: sqlite exec failed: %s",
                  err ? err : sqlite3_errstr(rc));
    if (err) sqlite3_free(err);
    return rc == SQLITE_OK;
}

static gboolean
history_is_recordable(const char *url)
{
    if (!url || !*url) return FALSE;
    if (!g_str_has_prefix(url, "http://") && !g_str_has_prefix(url, "https://"))
        return FALSE;
    for (const unsigned char *p = (const unsigned char *)url; *p; p++)
        if (*p < 0x20 || *p == 0x7F) return FALSE;
    return TRUE;
}

static gboolean
history_schema(void)
{
    return history_exec("PRAGMA journal_mode=WAL") &&
           history_exec("PRAGMA synchronous=NORMAL") &&
           history_exec("CREATE TABLE IF NOT EXISTS visits("
                        "url TEXT PRIMARY KEY,"
                        "title TEXT,"
                        "visit_count INTEGER NOT NULL DEFAULT 1,"
                        "last_visit INTEGER NOT NULL)") &&
           history_exec("CREATE INDEX IF NOT EXISTS idx_visits_last "
                        "ON visits(last_visit)");
}

static void
history_prune(void)
{
    if (!g_history_db) return;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(g_history_db,
            "DELETE FROM visits WHERE url NOT IN ("
            "SELECT url FROM visits ORDER BY last_visit DESC LIMIT ?)",
            -1, &st, NULL) != SQLITE_OK)
        return;
    sqlite3_bind_int(st, 1, NS_HISTORY_MAX_ROWS);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void
ns_history_init(void)
{
    g_mutex_lock(&g_history_mutex);
    if (g_history_db || g_history_disabled) {
        g_mutex_unlock(&g_history_mutex);
        return;
    }
    char *dir = g_build_filename(g_get_user_data_dir(), NS_APP_DIR_NAME, NULL);
    g_mkdir_with_parents(dir, 0700);
    g_chmod(dir, 0700);
    char *path = g_build_filename(dir, "history.sqlite", NULL);
    g_free(dir);

    int rc = sqlite3_open_v2(path, &g_history_db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        g_warning("history: could not open %s: %s", path,
                  g_history_db ? sqlite3_errmsg(g_history_db)
                               : sqlite3_errstr(rc));
        if (g_history_db) { sqlite3_close(g_history_db); g_history_db = NULL; }
        g_history_disabled = TRUE;
        g_free(path);
        g_mutex_unlock(&g_history_mutex);
        return;
    }
    history_harden(g_history_db);
    sqlite3_busy_timeout(g_history_db, 2500);
    if (!history_schema()) {
        sqlite3_close(g_history_db);
        g_history_db = NULL;
        g_history_disabled = TRUE;
        g_free(path);
        g_mutex_unlock(&g_history_mutex);
        return;
    }
    g_chmod(path, 0600);
    g_free(path);
    history_prune();
    g_mutex_unlock(&g_history_mutex);
}

void
ns_history_shutdown(void)
{
    g_mutex_lock(&g_history_mutex);
    if (g_history_db) {
        sqlite3_close(g_history_db);
        g_history_db = NULL;
    }
    g_mutex_unlock(&g_history_mutex);
}

void
ns_history_record(const char *url, const char *title)
{
    if (!history_is_recordable(url)) return;
    g_mutex_lock(&g_history_mutex);
    if (!g_history_db) {
        g_mutex_unlock(&g_history_mutex);
        return;
    }
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(g_history_db,
            "INSERT INTO visits(url,title,visit_count,last_visit) "
            "VALUES(?,?,1,?) "
            "ON CONFLICT(url) DO UPDATE SET "
            "visit_count=visit_count+1,last_visit=excluded.last_visit,"
            "title=COALESCE(NULLIF(excluded.title,''),title)",
            -1, &st, NULL) != SQLITE_OK) {
        g_mutex_unlock(&g_history_mutex);
        return;
    }
    sqlite3_bind_text (st, 1, url, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (st, 2, (title && *title) ? title : NULL, -1,
                       SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, g_get_real_time() / G_USEC_PER_SEC);
    sqlite3_step(st);
    sqlite3_finalize(st);
    g_mutex_unlock(&g_history_mutex);
}

void
ns_history_clear(void)
{
    g_mutex_lock(&g_history_mutex);
    history_exec("DELETE FROM visits");
    g_mutex_unlock(&g_history_mutex);
}

static const char k_history_style[] =
    "<style>" NS_ABOUT_BASE_CSS
    ".wrap{max-width:860px;margin:0 auto;padding:28px 24px 56px}\n"
    ".top{display:flex;align-items:center;justify-content:space-between;"
    "gap:16px;margin:18px 0 24px}\n"
    "h1{margin:0;font-size:28px;letter-spacing:-.02em}\n"
    ".filter{flex:0 1 320px;display:flex;align-items:center;gap:10px;"
    "height:42px;padding:0 16px;border-radius:999px;background:var(--card);"
    "border:1px solid var(--line);box-shadow:var(--shadow);"
    "color:var(--faint)}\n"
    ".filter:focus-within{border-color:var(--accent);"
    "box-shadow:0 0 0 3px var(--accent-soft)}\n"
    ".filter input{flex:1 1 auto;min-width:0;border:0;outline:0;"
    "background:transparent;color:var(--text);font:inherit;font-size:14px}\n"
    ".day{margin:0 0 18px}\n"
    ".day h2{margin:0 0 8px 6px;font-size:13px;font-weight:700;"
    "letter-spacing:.06em;text-transform:uppercase;color:var(--faint)}\n"
    ".day ul{list-style:none;margin:0;padding:6px;}\n"
    ".day li a{display:flex;align-items:center;gap:14px;padding:9px 12px;"
    "border-radius:12px;color:var(--text)}\n"
    ".day li a:hover{background:var(--field);text-decoration:none}\n"
    ".av{flex:0 0 auto;display:flex;align-items:center;"
    "justify-content:center;width:32px;height:32px;border-radius:10px;"
    "background:var(--accent-soft);color:var(--accent);font-weight:700;"
    "font-size:14px;text-transform:uppercase}\n"
    ".tx{flex:1 1 auto;min-width:0;display:flex;flex-direction:column}\n"
    ".t{font-size:14.5px;font-weight:600;white-space:nowrap;overflow:hidden;"
    "text-overflow:ellipsis}\n"
    ".u{font-size:12.5px;color:var(--muted);white-space:nowrap;"
    "overflow:hidden;text-overflow:ellipsis}\n"
    ".tm{flex:0 0 auto;font-size:12.5px;color:var(--faint);"
    "font-variant-numeric:tabular-nums}\n"
    ".empty{padding:48px 24px;text-align:center;color:var(--muted)}\n"
    ".empty b{display:block;margin-bottom:4px;font-size:16px;"
    "color:var(--text)}\n"
    "[hidden]{display:none}\n"
    "</style>";

static const char k_history_script[] =
    "<script>\n"
    "var q=document.getElementById('hq');\n"
    "if(q)q.addEventListener('input',function(){\n"
    " var v=q.value.toLowerCase();\n"
    " var days=document.querySelectorAll('.day');\n"
    " for(var i=0;i<days.length;i++){var any=false;\n"
    "  var rows=days[i].querySelectorAll('li');\n"
    "  for(var j=0;j<rows.length;j++){\n"
    "   var hit=!v||rows[j].textContent.toLowerCase().indexOf(v)>=0;\n"
    "   rows[j].hidden=!hit;if(hit)any=true;}\n"
    "  days[i].hidden=!any;}});\n"
    "</script>";

static char *
history_day_label(GDateTime *when, GDateTime *now)
{
    int days = g_date_time_get_day_of_year(now) -
               g_date_time_get_day_of_year(when);
    gboolean same_year =
        g_date_time_get_year(now) == g_date_time_get_year(when);
    if (same_year && days == 0)
        return g_strdup("Today");
    if (same_year && days == 1)
        return g_strdup("Yesterday");
    return g_date_time_format(when, same_year ? "%A, %e %B"
                                              : "%A, %e %B %Y");
}

static char *
history_host(const char *url)
{
    GUri *uri = g_uri_parse(url, G_URI_FLAGS_NONE, NULL);
    const char *host = uri ? g_uri_get_host(uri) : NULL;
    char *out = g_strdup(host && *host ? host : url);
    if (uri)
        g_uri_unref(uri);
    if (g_str_has_prefix(out, "www.")) {
        char *trimmed = g_strdup(out + 4);
        g_free(out);
        out = trimmed;
    }
    return out;
}

static void
history_initial(const char *host, char out[8])
{
    gunichar first = g_utf8_get_char_validated(host, -1);
    if (first != (gunichar)-1 && first != (gunichar)-2 &&
        g_unichar_isalnum(first))
        out[g_unichar_to_utf8(first, out)] = '\0';
    else
        g_strlcpy(out, "\xe2\x80\xa2", 8);
}

static void
history_switch_day(GString *s, char **open_day, const char *day)
{
    if (*open_day && strcmp(*open_day, day) == 0)
        return;
    if (*open_day)
        g_string_append(s, "</ul></section>");
    char *e_day = g_markup_escape_text(day, -1);
    g_string_append_printf(s,
        "<section class=\"day\"><h2>%s</h2><ul class=\"card\">", e_day);
    g_free(e_day);
    g_free(*open_day);
    *open_day = g_strdup(day);
}

static void
history_append_row(GString *s, const char *url, const char *title,
                   GDateTime *when)
{
    char *host = history_host(url);
    char initial[8];
    history_initial(host, initial);
    char *e_url   = g_markup_escape_text(url, -1);
    char *e_host  = g_markup_escape_text(host, -1);
    char *e_init  = g_markup_escape_text(initial, -1);
    char *e_title = g_markup_escape_text((title && *title) ? title : url, -1);
    char *clock   = when ? g_date_time_format(when, "%H:%M") : NULL;
    g_string_append_printf(s,
        "<li><a href=\"%s\"><span class=\"av\">%s</span>"
        "<span class=\"tx\"><span class=\"t\">%s</span>"
        "<span class=\"u\">%s</span></span>"
        "<span class=\"tm\">%s</span></a></li>",
        e_url, e_init, e_title, e_host, clock ? clock : "");
    g_free(clock);
    g_free(e_url);
    g_free(e_host);
    g_free(e_init);
    g_free(e_title);
    g_free(host);
}

static gboolean
history_append_visits(GString *s)
{
    gboolean have = FALSE;
    char *open_day = NULL;
    GDateTime *now = g_date_time_new_now_local();
    g_mutex_lock(&g_history_mutex);
    sqlite3_stmt *st = NULL;
    if (g_history_db &&
        sqlite3_prepare_v2(g_history_db,
            "SELECT url,title,last_visit FROM visits "
            "ORDER BY last_visit DESC LIMIT ?",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, NS_HISTORY_PAGE_ROWS);
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *url   = (const char *)sqlite3_column_text(st, 0);
            const char *title = (const char *)sqlite3_column_text(st, 1);
            if (!url) continue;
            have = TRUE;
            GDateTime *dt =
                g_date_time_new_from_unix_local(sqlite3_column_int64(st, 2));
            char *day = dt ? history_day_label(dt, now) : g_strdup("Earlier");
            history_switch_day(s, &open_day, day);
            history_append_row(s, url, title, dt);
            g_free(day);
            if (dt) g_date_time_unref(dt);
        }
        sqlite3_finalize(st);
    }
    g_mutex_unlock(&g_history_mutex);
    g_date_time_unref(now);
    if (open_day)
        g_string_append(s, "</ul></section>");
    g_free(open_day);
    return have;
}

char *
ns_history_html_page(void)
{
    GString *s = g_string_new(
        "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"color-scheme\" content=\"light dark\">"
        "<title>History</title>");
    g_string_append(s, k_history_style);
    g_string_append(s,
        "</head><body><main class=\"wrap\">"
        "<a class=\"crumb\" href=\"about:start\">\xe2\x86\x90 New Tab</a>"
        "<div class=\"top\"><h1>History</h1>"
        "<label class=\"filter\">"
        "<svg width=\"16\" height=\"16\" viewBox=\"0 0 16 16\" fill=\"none\""
        " stroke=\"currentColor\" stroke-width=\"1.6\""
        " stroke-linecap=\"round\"><circle cx=\"7\" cy=\"7\" r=\"4.6\"/>"
        "<path d=\"M10.4 10.4 14 14\"/></svg>"
        "<input id=\"hq\" type=\"search\" placeholder=\"Search history\""
        " aria-label=\"Search history\" autocomplete=\"off\"></label></div>");
    if (!history_append_visits(s))
        g_string_append(s, "<div class=\"card empty\"><b>No history yet</b>"
                           "Pages you visit will show up here.</div>");
    g_string_append(s, "</main>");
    g_string_append(s, k_history_script);
    g_string_append(s, "</body></html>");
    return g_string_free(s, FALSE);
}
