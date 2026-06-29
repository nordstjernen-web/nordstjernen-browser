/* Nordstjernen — IMAP/POP3/SMTP email client backend. */

#ifndef NS_MAIL_H
#define NS_MAIL_H

#include <glib.h>

G_BEGIN_DECLS

gboolean ns_mail_is_configured(void);

char    *ns_mail_account_json(void);
gboolean ns_mail_account_save(const char *form_urlencoded);

char    *ns_mail_autoconfig_json(const char *provider_id, const char *email);

gboolean ns_mail_unlock(const char *form_urlencoded);
gboolean ns_mail_set_primary(const char *form_urlencoded);

char *ns_mail_take_pending_shell_key(void);
void  ns_mail_set_session_key(const char *key_b64);

void  ns_mail_refresh(void);
char *ns_mail_status_json(void);

int   ns_mail_poll_unseen(void);

void  ns_mail_open(const char *uid);
char *ns_mail_message_json(void);
gboolean ns_mail_attachment(const char *uid, int index, char **out_ctype,
                            char **out_name, guint8 **out_data, gsize *out_len);

void  ns_mail_send(const char *form_urlencoded);
char *ns_mail_send_status_json(void);

void  ns_mail_shutdown(void);

G_END_DECLS

#endif
