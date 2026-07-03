/* Nordstjernen - force the mobile variant of select sites.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "mobile.h"

#include "net.h"

#include <string.h>

static const char k_mobile_ua[] =
    "Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) "
    "AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 "
    "Mobile/15E148 Safari/604.1";

const char *
ns_mobile_user_agent(void)
{
    return k_mobile_ua;
}

static gboolean
host_eq(const char *host, const char *want)
{
    return host && g_ascii_strcasecmp(host, want) == 0;
}

static gboolean
facebook_host(const char *host)
{
    return host_eq(host, "facebook.com")        ||
           host_eq(host, "www.facebook.com")    ||
           host_eq(host, "web.facebook.com")    ||
           host_eq(host, "mobile.facebook.com") ||
           host_eq(host, "m.facebook.com");
}

#if defined(__ANDROID__)
static gboolean
host_suffix(const char *host, const char *suffix)
{
    if (!host || !suffix) return FALSE;
    gsize hlen = strlen(host);
    gsize slen = strlen(suffix);
    return hlen >= slen &&
           g_ascii_strcasecmp(host + hlen - slen, suffix) == 0;
}

static gboolean
wikipedia_site_host(const char *host)
{
    return host_eq(host, "wikipedia.org")        ||
           host_eq(host, "www.wikipedia.org")    ||
           host_eq(host, "m.wikipedia.org")      ||
           host_suffix(host, ".wikipedia.org");
}
#endif

gboolean
ns_mobile_force_host(const char *host)
{
    if (facebook_host(host))
        return TRUE;
#if defined(__ANDROID__)
    if (wikipedia_site_host(host))
        return TRUE;
#endif
    return FALSE;
}

