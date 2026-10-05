/* Nordstjernen — WebIDL brand checks for the native members of interfaces. */
#ifndef NS_JS_BRAND_H
#define NS_JS_BRAND_H

#include "ns_quickjs.h"

void ns_js_brand_node_interfaces(JSContext *ctx, JSClassID element_cid,
                                 JSClassID attr_cid);

#endif
