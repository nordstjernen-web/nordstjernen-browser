/* Nordstjernen — C bridge from the iOS (UIKit) host app to the engine. */

#ifndef NS_IOS_H
#define NS_IOS_H

#include "libnordstjernen.h"

#ifdef __cplusplus
extern "C" {
#endif

int ns_ios_init(const char *data_dir, const char *ca_bundle);

#ifdef __cplusplus
}
#endif

#endif
