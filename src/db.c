/* Nordstjernen — hardened SQLite open and exec for the profile stores. */

#include "db.h"

#include <string.h>
#include <glib/gstdio.h>

void
ns_db_harden(sqlite3 *db)
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

sqlite3 *
ns_db_open(const char *path, int flags, const char *tag)
{
    sqlite3 *db = NULL;
    int rc = sqlite3_open_v2(path, &db, flags, NULL);
    if (rc != SQLITE_OK) {
        g_warning("%s: could not open %s: %s", tag, path,
                  db ? sqlite3_errmsg(db) : sqlite3_errstr(rc));
        if (db) sqlite3_close(db);
        return NULL;
    }
    if (path && *path && strcmp(path, ":memory:") != 0)
        g_chmod(path, 0600);
    ns_db_harden(db);
    sqlite3_busy_timeout(db, 2500);
    return db;
}

gboolean
ns_db_exec(sqlite3 *db, const char *sql, const char *tag)
{
    if (!db) return FALSE;
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK)
        g_warning("%s: sqlite exec failed: %s", tag,
                  err ? err : sqlite3_errstr(rc));
    if (err) sqlite3_free(err);
    return rc == SQLITE_OK;
}
