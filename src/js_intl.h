/* Nordstjernen — native ECMA-402 (Intl) implementation for the QuickJS engine. */
#ifndef NS_JS_INTL_H
#define NS_JS_INTL_H

#include <quickjs.h>

void ns_js_intl_install(JSContext *ctx, JSValueConst global);

#endif
