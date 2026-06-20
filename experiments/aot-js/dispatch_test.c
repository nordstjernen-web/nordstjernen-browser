/* dispatch_test.c — proves the JS_SetFunctionAOT dispatch path in the real
   QuickJS engine: a registered numeric function runs native for numeric
   arguments, and falls back to the bytecode interpreter (bit-identically) for
   any non-numeric argument. Built standalone against the same engine the
   renderer uses. */

#include "quickjs.c"

#include <stdio.h>
#include <string.h>

static double add3_correct(const double *x) { return x[0] * x[1] + x[2]; }
static double add3_spy(const double *x) { return x[0] * x[1] + x[2] + 1000000.0; }
static const JSAOTEntry add3_correct_entry = { add3_correct, 3 };
static const JSAOTEntry add3_spy_entry = { add3_spy, 3 };

static double poly_correct(const double *x) {
    double t = x[0];
    return t < 0.5 ? 4 * t * t * t : 1 - pow(-2 * t + 2, 3) / 2;
}
static const JSAOTEntry poly_entry = { poly_correct, 1 };

static int fails = 0;

static double call_num(JSContext *ctx, JSValue f, int n, const double *a) {
    JSValue argv[8], r;
    double out;
    for (int i = 0; i < n; i++) argv[i] = JS_NewFloat64(ctx, a[i]);
    r = JS_Call(ctx, f, JS_UNDEFINED, n, argv);
    JS_ToFloat64(ctx, &out, r);
    JS_FreeValue(ctx, r);
    for (int i = 0; i < n; i++) JS_FreeValue(ctx, argv[i]);
    return out;
}

static void expect(const char *what, double got, double want) {
    int ok = (got == want) || (got != got && want != want);
    printf("  %-46s got=%-12g want=%-12g %s\n", what, got, want, ok ? "OK" : "FAIL");
    if (!ok) fails++;
}

static JSValue get_fn(JSContext *ctx, const char *src) {
    JSValue r = JS_Eval(ctx, src, strlen(src), "<t>", JS_EVAL_TYPE_GLOBAL);
    return r;
}

int main(void) {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);

    JSValue add3 = get_fn(ctx, "function add3(a,b,c){return a*b+c;} add3;");
    double args[3] = { 2, 3, 4 };

    printf("add3(2,3,4): interpreter baseline\n");
    double base = call_num(ctx, add3, 3, args);
    expect("no AOT registered -> interpreter", base, 10);

    printf("with SPY native (returns value+1000000) registered:\n");
    JS_SetFunctionAOT(ctx, add3, &add3_spy_entry);
    expect("numeric args -> native path taken", call_num(ctx, add3, 3, args), 1000010);

    /* non-numeric argument must bail to the interpreter */
    {
        JSValue av[3];
        av[0] = JS_NewFloat64(ctx, 2);
        av[1] = JS_NewString(ctx, "3");      /* a string, not a number */
        av[2] = JS_NewFloat64(ctx, 4);
        JSValue r = JS_Call(ctx, add3, JS_UNDEFINED, 3, av);
        double out; JS_ToFloat64(ctx, &out, r); JS_FreeValue(ctx, r);
        for (int i = 0; i < 3; i++) JS_FreeValue(ctx, av[i]);
        /* "3" coerces to 3 in a*b+c, so interpreter yields 10, NOT the spy's 1000010 */
        expect("string arg -> interpreter bailout (not native)", out, 10);
    }

    /* too few args must bail (missing arg is undefined -> interpreter handles it) */
    {
        double two[2] = { 2, 3 };
        double r = call_num(ctx, add3, 2, two);  /* c=undefined -> NaN */
        expect("argc < arg_count -> interpreter bailout (NaN)", r, 0.0/0.0);
    }

    printf("with CORRECT native registered (must equal interpreter):\n");
    JS_SetFunctionAOT(ctx, add3, &add3_correct_entry);
    expect("add3(2,3,4) native == interpreter", call_num(ctx, add3, 3, args), 10);
    double a2[3] = { -7.5, 4, 100 };
    expect("add3(-7.5,4,100) native == interpreter", call_num(ctx, add3, 3, a2), -7.5*4+100);

    /* unregister restores the interpreter */
    JS_SetFunctionAOT(ctx, add3, NULL);
    expect("unregister -> interpreter again", call_num(ctx, add3, 3, args), 10);

    /* a realistic kernel: easeInOutCubic, native vs interpreter across the range */
    printf("easeInOutCubic: native matches interpreter bit-for-bit:\n");
    JSValue ease = get_fn(ctx,
        "function ease(t){return t<0.5?4*t*t*t:1-Math.pow(-2*t+2,3)/2;} ease;");
    int allmatch = 1;
    for (int i = 0; i <= 100; i++) {
        double t = i / 100.0, in;
        in = call_num(ctx, ease, 1, &t);                 /* interpreter */
        JS_SetFunctionAOT(ctx, ease, &poly_entry);
        double na = call_num(ctx, ease, 1, &t);          /* native */
        JS_SetFunctionAOT(ctx, ease, NULL);
        if (na != in) { allmatch = 0; printf("    mismatch t=%g native=%.17g interp=%.17g\n", t, na, in); }
    }
    expect("101 sample points identical", allmatch ? 1 : 0, 1);

    JS_FreeValue(ctx, add3);
    JS_FreeValue(ctx, ease);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

    printf("\n%s\n", fails == 0 ? "ALL DISPATCH TESTS PASSED" : "DISPATCH TESTS FAILED");
    return fails != 0;
}
