/* Nordstjernen — hardened SQLite open and exec for the profile stores. */

#ifndef NS_DB_H
#define NS_DB_H

#include <glib.h>
#include <sqlite3.h>

G_BEGIN_DECLS

void     ns_db_harden(sqlite3 *db);
sqlite3 *ns_db_open(const char *path, int flags, const char *tag);
gboolean ns_db_exec(sqlite3 *db, const char *sql, const char *tag);

G_END_DECLS

#endif
