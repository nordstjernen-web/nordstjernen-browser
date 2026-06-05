/* Nordstjernen - force the mobile variant of select sites. */

#ifndef NS_MOBILE_H
#define NS_MOBILE_H

#include <glib.h>

G_BEGIN_DECLS

const char *ns_mobile_user_agent(void);

gboolean ns_mobile_force_host(const char *host);

char *ns_mobile_rewrite_url(const char *url);

G_END_DECLS

#endif
