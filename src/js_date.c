/* Nordstjernen — native Temporal date/time API over QuickJS, ICU-free. */

#include "js_date.h"

#include <glib.h>

void
ns_js_temporal_install(JSContext *ctx, JSValueConst global)
{
    JSAtom atom = JS_NewAtom(ctx, "Temporal");
    int has = JS_HasProperty(ctx, global, atom);
    JS_FreeAtom(ctx, atom);
    if (has <= 0)
        JS_SetPropertyStr(ctx, global, "Temporal", JS_NewObject(ctx));
}
