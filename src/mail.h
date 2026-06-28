/* Nordstjernen — IMAP/POP3/SMTP email client backend. */

#ifndef NS_MAIL_H
#define NS_MAIL_H

#include <glib.h>

G_BEGIN_DECLS

gboolean ns_mail_is_configured(void);

char    *ns_mail_account_json(void);
gboolean ns_mail_account_save(const char *form_urlencoded);

void  ns_mail_refresh(void);
char *ns_mail_status_json(void);

void  ns_mail_open(const char *uid);
char *ns_mail_message_json(void);

void  ns_mail_send(const char *form_urlencoded);
char *ns_mail_send_status_json(void);

void  ns_mail_shutdown(void);

G_END_DECLS

#endif
