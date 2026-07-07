/* Nordstjernen — C bridge from the iOS (UIKit) host app to the engine. */

#ifndef NS_IOS_H
#define NS_IOS_H

#include "libnordstjernen.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the engine once. data_dir is a writable directory (the app's
 * Library directory) the engine uses for config, cache and cookies; ca_bundle
 * is the path to the bundled cacert.pem that libcurl verifies TLS against.
 * Both may be NULL. Returns 0 on success; safe to call more than once (later
 * calls are no-ops that return 0). */
int ns_ios_init(const char *data_dir, const char *ca_bundle);

#ifdef __cplusplus
}
#endif

#endif
