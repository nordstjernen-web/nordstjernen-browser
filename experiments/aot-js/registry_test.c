/* registry_test.c — end-to-end: aotc emits a linkable registry from
   tests/framework.js, this driver links it against the real engine, evaluates
   the framework source, registers every kernel via ns_aot_register, and checks
   that the native-dispatched result equals a second, unregistered context
   (the interpreter) across many inputs. Proves the compile -> register ->
   native-dispatch pipeline with aotc's actual output. */

#include "quickjs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int ns_aot_register(JSContext *ctx, JSValueConst obj);

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *s = malloc(n + 1);
    if (fread(s, 1, n, f) != (size_t)n) { perror("read"); exit(2); }
    s[n] = 0; *len = n; fclose(f);
    return f ? s : NULL;
}

static double callf(JSContext *ctx, const char *name, int argc, const double *a) {
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, g, name);
    JSValue argv[4], r;
    for (int i = 0; i < argc; i++) argv[i] = JS_NewFloat64(ctx, a[i]);
    r = JS_Call(ctx, fn, JS_UNDEFINED, argc, argv);
    double out; JS_ToFloat64(ctx, &out, r);
    for (int i = 0; i < argc; i++) JS_FreeValue(ctx, argv[i]);
    JS_FreeValue(ctx, r); JS_FreeValue(ctx, fn); JS_FreeValue(ctx, g);
    return out;
}

struct Case { const char *name; int argc; double a[3]; };

int main(void) {
    size_t len;
    char *src = slurp("experiments/aot-js/tests/framework.js", &len);

    JSRuntime *rt = JS_NewRuntime();
    JSContext *aot = JS_NewContext(rt);     /* will have AOT registered */
    JSContext *ref = JS_NewContext(rt);     /* pure interpreter */
    JS_FreeValue(aot, JS_Eval(aot, src, len, "fw.js", JS_EVAL_TYPE_GLOBAL));
    JS_FreeValue(ref, JS_Eval(ref, src, len, "fw.js", JS_EVAL_TYPE_GLOBAL));

    JSValue g = JS_GetGlobalObject(aot);
    int registered = ns_aot_register(aot, g);
    JS_FreeValue(aot, g);
    printf("registered %d AOT kernels\n", registered);

    struct Case cases[] = {
        { "getHighestPriorityLane", 1, { 96 } },
        { "pickArbitraryLaneIndex", 1, { 1024 } },
        { "mergeLanes", 2, { 3, 12 } },
        { "isSubsetOfLanes", 2, { 7, 3 } },
        { "swing", 1, { 0.37 } },
        { "easeInOutQuad", 1, { 0.8 } },
        { "easeOutCubic", 1, { 0.42 } },
        { "easeInOutCubic", 1, { 0.63 } },
        { "lerp", 3, { 5, 25, 0.3 } },
        { "clamp01", 1, { 1.7 } },
        { "hue2rgb", 3, { 0.2, 0.8, 0.1 } },
    };
    int n = sizeof(cases) / sizeof(cases[0]), fails = 0;

    /* sweep each unary kernel across its domain, plus the fixed cases */
    for (int c = 0; c < n; c++) {
        double na = callf(aot, cases[c].name, cases[c].argc, cases[c].a);
        double in = callf(ref, cases[c].name, cases[c].argc, cases[c].a);
        int ok = (na == in) || (na != na && in != in);
        printf("  %-24s native=%-20.12g interp=%-20.12g %s\n",
               cases[c].name, na, in, ok ? "OK" : "FAIL");
        if (!ok) fails++;
    }

    int sweepfail = 0;
    const char *unary[] = { "swing", "easeInOutQuad", "easeOutCubic", "easeInOutCubic", "clamp01" };
    for (int u = 0; u < 5; u++)
        for (int i = -20; i <= 120; i++) {
            double t = i / 100.0;
            double na = callf(aot, unary[u], 1, &t);
            double in = callf(ref, unary[u], 1, &t);
            if (!(na == in || (na != na && in != in))) { sweepfail++;
                printf("  SWEEP MISMATCH %s(%g): %.17g vs %.17g\n", unary[u], t, na, in); }
        }
    printf("  domain sweep (5 kernels x 141 points): %s\n", sweepfail ? "FAIL" : "OK");
    fails += sweepfail;

    JS_FreeContext(aot); JS_FreeContext(ref); JS_FreeRuntime(rt); free(src);
    printf("\n%s\n", fails == 0 ? "REGISTRY PIPELINE PASSED" : "REGISTRY PIPELINE FAILED");
    return fails != 0;
}
