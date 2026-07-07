/* Nordstjernen — C bridge from the iOS (UIKit) host app to the engine. */

#include "ns_ios.h"

#include <stdlib.h>

static int g_inited;

int
ns_ios_init(const char *data_dir, const char *ca_bundle)
{
    if (g_inited)
        return 0;
    if (data_dir && *data_dir) {
        setenv("HOME", data_dir, 1);
        setenv("XDG_CONFIG_HOME", data_dir, 1);
        setenv("XDG_CACHE_HOME", data_dir, 1);
        setenv("XDG_DATA_HOME", data_dir, 1);
    }
    if (ca_bundle && *ca_bundle)
        setenv("CURL_CA_BUNDLE", ca_bundle, 1);
    int rc = ns_browser_init();
    if (rc == 0)
        g_inited = 1;
    return rc;
}
