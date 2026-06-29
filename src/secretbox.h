/* Nordstjernen — authenticated secret sealing (PBKDF2-SHA256 + AES-256-GCM). */

#ifndef NS_SECRETBOX_H
#define NS_SECRETBOX_H

#include <glib.h>

G_BEGIN_DECLS

gboolean ns_secretbox_is_sealed(const char *s);
char    *ns_secretbox_seal(const char *plaintext, const char *password);
char    *ns_secretbox_open(const char *blob, const char *password);

G_END_DECLS

#endif
