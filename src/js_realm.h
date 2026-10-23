/* Nordstjernen — native ShadowRealm and AsyncContext for the QuickJS engine. */
#ifndef NS_JS_REALM_H
#define NS_JS_REALM_H

#include "ns_quickjs.h"

void ns_js_realm_install(JSContext *ctx, JSValueConst global);
bool ns_js_eval_compile_allowed(JSContext *ctx);

#endif
