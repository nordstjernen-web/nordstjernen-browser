/* Nordstjernen — native Temporal date/time API for the QuickJS engine. */
#ifndef NS_JS_DATE_H
#define NS_JS_DATE_H

#include <quickjs.h>

void ns_js_temporal_install(JSContext *ctx, JSValueConst global);

#endif
