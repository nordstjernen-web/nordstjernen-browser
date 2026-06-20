/* aotc.c — proof-of-concept ahead-of-time JavaScript compiler.
   Reuses the real QuickJS front end to compile JS to bytecode, then
   translates a numeric subset of that bytecode straight to C. */

#include "quickjs.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char **items;
    int len;
    int cap;
} ExprStack;

static void es_push(ExprStack *s, const char *fmt, ...) {
    if (s->len >= s->cap) {
        s->cap = s->cap ? s->cap * 2 : 16;
        s->items = realloc(s->items, s->cap * sizeof(char *));
    }
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    s->items[s->len++] = strdup(buf);
}

static char *es_pop(ExprStack *s) {
    if (s->len <= 0) {
        fprintf(stderr, "aotc: stack underflow\n");
        exit(2);
    }
    return s->items[--s->len];
}

static char *es_peek(ExprStack *s) {
    if (s->len <= 0) {
        fprintf(stderr, "aotc: stack underflow on peek\n");
        exit(2);
    }
    return s->items[s->len - 1];
}

static JSFunctionBytecode *get_fb(JSValue v) {
    int tag = JS_VALUE_GET_TAG(v);
    if (tag == JS_TAG_FUNCTION_BYTECODE)
        return JS_VALUE_GET_PTR(v);
    if (tag == JS_TAG_OBJECT)
        return JS_GetFunctionBytecode(v);
    return NULL;
}

typedef struct {
    JSFunctionBytecode *b;
    const char *name;
} Fn;

static Fn g_fns[64];
static int g_nfns;

static const char *fn_name_for(JSContext *ctx, JSAtom atom) {
    const char *nm = JS_AtomToCString(ctx, atom);
    for (int i = 0; i < g_nfns; i++) {
        if (strcmp(g_fns[i].name, nm) == 0) {
            JS_FreeCString(ctx, nm);
            return g_fns[i].name;
        }
    }
    JS_FreeCString(ctx, nm);
    return NULL;
}

static int *collect_targets(JSFunctionBytecode *b, int *out_n) {
    int cap = 16, n = 0;
    int *t = malloc(cap * sizeof(int));
    const uint8_t *code = b->byte_code_buf;
    int len = b->byte_code_len, pos = 0;
    while (pos < len) {
        int op = code[pos];
        int sz = short_opcode_info(op).size;
        int operand_pos = pos + 1;
        int target = -1;
        switch (op) {
        case OP_goto: case OP_if_true: case OP_if_false:
            target = operand_pos + get_i32(code + operand_pos); break;
        case OP_goto16:
            target = operand_pos + get_i16(code + operand_pos); break;
        case OP_goto8: case OP_if_true8: case OP_if_false8:
            target = operand_pos + get_i8(code + operand_pos); break;
        }
        if (target >= 0) {
            if (n >= cap) { cap *= 2; t = realloc(t, cap * sizeof(int)); }
            t[n++] = target;
        }
        pos += sz;
    }
    *out_n = n;
    return t;
}

static int is_target(int *t, int n, int pos) {
    for (int i = 0; i < n; i++) if (t[i] == pos) return 1;
    return 0;
}

static const char *marker_fn(const char *m) {
    if (strncmp(m, "FUNC:", 5) == 0) return m + 5;
    return NULL;
}

static void emit_call(FILE *o, ExprStack *st, int *tc, int argc) {
    char *args[8];
    for (int i = argc - 1; i >= 0; i--) args[i] = es_pop(st);
    char *fn = es_pop(st);
    const char *name = marker_fn(fn);
    if (!name) {
        fprintf(stderr, "aotc: call of non-static-function value '%s'\n", fn);
        exit(3);
    }
    int t = (*tc)++;
    fprintf(o, "  double t%d = %s(", t, name);
    for (int i = 0; i < argc; i++) fprintf(o, "%s%s", i ? ", " : "", args[i]);
    fprintf(o, ");\n");
    es_push(st, "t%d", t);
}

static void translate(JSContext *ctx, FILE *o, JSFunctionBytecode *b, const char *name) {
    int ntargets;
    int *targets = collect_targets(b, &ntargets);

    fprintf(o, "static double %s(", name);
    for (int i = 0; i < b->arg_count; i++)
        fprintf(o, "%sdouble a%d", i ? ", " : "", i);
    if (b->arg_count == 0) fprintf(o, "void");
    fprintf(o, ") {\n");
    for (int i = 0; i < b->var_count; i++)
        fprintf(o, "  double loc%d = 0;\n", i);

    ExprStack st = {0};
    int tc = 0;
    const uint8_t *code = b->byte_code_buf;
    int len = b->byte_code_len, pos = 0;

    while (pos < len) {
        if (is_target(targets, ntargets, pos))
            fprintf(o, "L%d: ;\n", pos);
        int op = code[pos];
        int sz = short_opcode_info(op).size;
        int p = pos + 1;

        switch (op) {
        case OP_push_0: case OP_push_1: case OP_push_2: case OP_push_3:
        case OP_push_4: case OP_push_5: case OP_push_6: case OP_push_7:
            es_push(&st, "%d", op - OP_push_0); break;
        case OP_push_minus1: es_push(&st, "-1"); break;
        case OP_push_i8:  es_push(&st, "%d", get_i8(code + p)); break;
        case OP_push_i16: es_push(&st, "%d", get_i16(code + p)); break;
        case OP_push_i32: es_push(&st, "%d", (int)get_i32(code + p)); break;
        case OP_push_const8: case OP_push_const: {
            int idx = (op == OP_push_const8) ? get_u8(code + p) : get_u32(code + p);
            JSValue v = b->cpool[idx];
            int tag = JS_VALUE_GET_TAG(v);
            if (tag == JS_TAG_INT) es_push(&st, "%d", JS_VALUE_GET_INT(v));
            else if (JS_TAG_IS_FLOAT64(tag)) es_push(&st, "%.17g", JS_VALUE_GET_FLOAT64(v));
            else { fprintf(stderr, "aotc: non-numeric const\n"); exit(3); }
            break;
        }
        case OP_get_arg0: case OP_get_arg1: case OP_get_arg2: case OP_get_arg3:
            es_push(&st, "a%d", op - OP_get_arg0); break;
        case OP_get_arg: es_push(&st, "a%d", get_u16(code + p)); break;
        case OP_get_loc0: case OP_get_loc1: case OP_get_loc2: case OP_get_loc3:
            es_push(&st, "loc%d", op - OP_get_loc0); break;
        case OP_get_loc8: es_push(&st, "loc%d", get_u8(code + p)); break;
        case OP_get_loc: case OP_get_loc_check: es_push(&st, "loc%d", get_u16(code + p)); break;
        case OP_put_loc0: case OP_put_loc1: case OP_put_loc2: case OP_put_loc3: {
            char *v = es_pop(&st);
            fprintf(o, "  loc%d = %s;\n", op - OP_put_loc0, v);
            break;
        }
        case OP_put_loc8: {
            char *v = es_pop(&st);
            fprintf(o, "  loc%d = %s;\n", get_u8(code + p), v);
            break;
        }
        case OP_put_loc: case OP_put_loc_check: case OP_put_loc_check_init: {
            char *v = es_pop(&st);
            fprintf(o, "  loc%d = %s;\n", get_u16(code + p), v);
            break;
        }
        case OP_set_loc0: case OP_set_loc1: case OP_set_loc2: case OP_set_loc3: {
            char *v = es_peek(&st);
            fprintf(o, "  loc%d = %s;\n", op - OP_set_loc0, v);
            break;
        }
        case OP_set_loc8: {
            char *v = es_peek(&st);
            fprintf(o, "  loc%d = %s;\n", get_u8(code + p), v);
            break;
        }
        case OP_set_loc: {
            char *v = es_peek(&st);
            fprintf(o, "  loc%d = %s;\n", get_u16(code + p), v);
            break;
        }
        case OP_inc_loc: fprintf(o, "  loc%d += 1;\n", get_u8(code + p)); break;
        case OP_dec_loc: fprintf(o, "  loc%d -= 1;\n", get_u8(code + p)); break;
        case OP_add_loc: {
            char *v = es_pop(&st);
            fprintf(o, "  loc%d += %s;\n", get_u8(code + p), v);
            break;
        }
        case OP_post_inc: case OP_post_dec: {
            char *v = es_pop(&st);
            int told = tc++, tnew = tc++;
            fprintf(o, "  double t%d = %s;\n", told, v);
            fprintf(o, "  double t%d = %s %s 1;\n", tnew, v,
                    op == OP_post_inc ? "+" : "-");
            es_push(&st, "t%d", told);
            es_push(&st, "t%d", tnew);
            break;
        }
        case OP_inc: case OP_dec: case OP_neg: case OP_plus: {
            char *v = es_pop(&st);
            int t = tc++;
            const char *e = op == OP_inc ? "+ 1" : op == OP_dec ? "- 1" :
                            op == OP_neg ? "* -1" : "";
            fprintf(o, "  double t%d = %s %s;\n", t, v, e);
            es_push(&st, "t%d", t);
            break;
        }
        case OP_add: case OP_sub: case OP_mul: case OP_div: {
            char *r = es_pop(&st), *l = es_pop(&st);
            char c = op == OP_add ? '+' : op == OP_sub ? '-' : op == OP_mul ? '*' : '/';
            int t = tc++;
            fprintf(o, "  double t%d = %s %c %s;\n", t, l, c, r);
            es_push(&st, "t%d", t);
            break;
        }
        case OP_mod: {
            char *r = es_pop(&st), *l = es_pop(&st);
            int t = tc++;
            fprintf(o, "  double t%d = fmod(%s, %s);\n", t, l, r);
            es_push(&st, "t%d", t);
            break;
        }
        case OP_pow: {
            char *r = es_pop(&st), *l = es_pop(&st);
            int t = tc++;
            fprintf(o, "  double t%d = pow(%s, %s);\n", t, l, r);
            es_push(&st, "t%d", t);
            break;
        }
        case OP_lt: case OP_lte: case OP_gt: case OP_gte:
        case OP_eq: case OP_neq: case OP_strict_eq: case OP_strict_neq: {
            char *r = es_pop(&st), *l = es_pop(&st);
            const char *c = op == OP_lt ? "<" : op == OP_lte ? "<=" :
                            op == OP_gt ? ">" : op == OP_gte ? ">=" :
                            (op == OP_eq || op == OP_strict_eq) ? "==" : "!=";
            int t = tc++;
            fprintf(o, "  double t%d = (%s %s %s);\n", t, l, c, r);
            es_push(&st, "t%d", t);
            break;
        }
        case OP_if_false: case OP_if_false8: {
            int target = (op == OP_if_false) ? p + get_i32(code + p)
                                             : p + get_i8(code + p);
            char *c = es_pop(&st);
            fprintf(o, "  if (!(%s)) goto L%d;\n", c, target);
            break;
        }
        case OP_if_true: case OP_if_true8: {
            int target = (op == OP_if_true) ? p + get_i32(code + p)
                                            : p + get_i8(code + p);
            char *c = es_pop(&st);
            fprintf(o, "  if (%s) goto L%d;\n", c, target);
            break;
        }
        case OP_goto: { int target = p + get_i32(code + p); fprintf(o, "  goto L%d;\n", target); break; }
        case OP_goto16: { int target = p + get_i16(code + p); fprintf(o, "  goto L%d;\n", target); break; }
        case OP_goto8: { int target = p + get_i8(code + p); fprintf(o, "  goto L%d;\n", target); break; }
        case OP_call0: case OP_call1: case OP_call2: case OP_call3:
            emit_call(o, &st, &tc, op - OP_call0); break;
        case OP_call: emit_call(o, &st, &tc, get_u16(code + p)); break;
        case OP_get_var: {
            JSAtom atom = get_u32(code + p);
            const char *fn = fn_name_for(ctx, atom);
            if (!fn) {
                const char *nm = JS_AtomToCString(ctx, atom);
                fprintf(stderr, "aotc: unsupported get_var '%s'\n", nm);
                exit(3);
            }
            es_push(&st, "FUNC:%s", fn);
            break;
        }
        case OP_get_var_ref0: case OP_get_var_ref1:
        case OP_get_var_ref2: case OP_get_var_ref3: {
            int idx = op - OP_get_var_ref0;
            const char *fn = fn_name_for(ctx, b->closure_var[idx].var_name);
            if (!fn) { fprintf(stderr, "aotc: unsupported var_ref\n"); exit(3); }
            es_push(&st, "FUNC:%s", fn);
            break;
        }
        case OP_get_var_ref: case OP_get_var_ref_check: {
            int idx = get_u16(code + p);
            const char *fn = fn_name_for(ctx, b->closure_var[idx].var_name);
            if (!fn) { fprintf(stderr, "aotc: unsupported var_ref\n"); exit(3); }
            es_push(&st, "FUNC:%s", fn);
            break;
        }
        case OP_dup: es_push(&st, "%s", es_peek(&st)); break;
        case OP_drop: es_pop(&st); break;
        case OP_nop: case OP_set_loc_uninitialized: break;
        case OP_return: { char *v = es_pop(&st); fprintf(o, "  return %s;\n", v); break; }
        case OP_return_undef: fprintf(o, "  return 0;\n"); break;
        default:
            fprintf(stderr, "aotc: unsupported opcode %d (%s) at pc %d in %s\n",
                    op,
#ifdef ENABLE_DUMPS
                    short_opcode_info(op).name,
#else
                    "?",
#endif
                    pos, name);
            exit(3);
        }
        pos += sz;
    }
    fprintf(o, "  return 0;\n}\n\n");
    free(targets);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s input.js output.c [entry]\n", argv[0]);
        return 1;
    }
    const char *in = argv[1], *out = argv[2];
    const char *entry = argc > 3 ? argv[3] : NULL;

    FILE *f = fopen(in, "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = malloc(n + 1);
    if (fread(src, 1, n, f) != (size_t)n) { perror("read"); return 1; }
    src[n] = 0;
    fclose(f);

    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);

    JSValue top = JS_Eval(ctx, src, n, in,
                          JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(top)) {
        JSValue e = JS_GetException(ctx);
        const char *s = JS_ToCString(ctx, e);
        fprintf(stderr, "aotc: compile error: %s\n", s);
        return 1;
    }
    JSFunctionBytecode *bt = get_fb(top);

    for (int i = 0; i < bt->cpool_count && g_nfns < 64; i++) {
        JSFunctionBytecode *fb = get_fb(bt->cpool[i]);
        if (!fb) continue;
        const char *nm = JS_AtomToCString(ctx, fb->func_name);
        if (!nm || !nm[0]) continue;
        g_fns[g_nfns].b = fb;
        g_fns[g_nfns].name = strdup(nm);
        g_nfns++;
        JS_FreeCString(ctx, nm);
    }
    if (g_nfns == 0) {
        fprintf(stderr, "aotc: no top-level functions to compile\n");
        return 1;
    }
    if (!entry) entry = g_fns[0].name;

    FILE *o = fopen(out, "w");
    fprintf(o, "/* generated by aotc — ahead-of-time JS->C */\n");
    fprintf(o, "#include <stdio.h>\n#include <stdlib.h>\n#include <math.h>\n\n");
    for (int i = 0; i < g_nfns; i++) {
        fprintf(o, "static double %s(", g_fns[i].name);
        int ac = g_fns[i].b->arg_count;
        for (int j = 0; j < ac; j++) fprintf(o, "%sdouble", j ? ", " : "");
        if (ac == 0) fprintf(o, "void");
        fprintf(o, ");\n");
    }
    fprintf(o, "\n");
    for (int i = 0; i < g_nfns; i++)
        translate(ctx, o, g_fns[i].b, g_fns[i].name);

    int eac = 0;
    for (int i = 0; i < g_nfns; i++)
        if (strcmp(g_fns[i].name, entry) == 0) eac = g_fns[i].b->arg_count;

    fprintf(o, "#include <time.h>\n");
    fprintf(o, "int main(int argc, char **argv) {\n");
    fprintf(o, "  double r = 0;\n");
    for (int i = 0; i < eac; i++)
        fprintf(o, "  double a%d = (argc > %d) ? atof(argv[%d]) : 0;\n", i, i + 1, i + 1);
    fprintf(o, "  long reps = (argc > %d) ? atol(argv[%d]) : 1;\n", eac + 1, eac + 1);
    fprintf(o, "  struct timespec ts0, ts1;\n");
    fprintf(o, "  clock_gettime(CLOCK_MONOTONIC, &ts0);\n");
    fprintf(o, "  for (long k = 0; k < reps; k++) r = %s(", entry);
    for (int i = 0; i < eac; i++) fprintf(o, "%sa%d", i ? ", " : "", i);
    fprintf(o, ");\n");
    fprintf(o, "  clock_gettime(CLOCK_MONOTONIC, &ts1);\n");
    fprintf(o, "  double ms = (ts1.tv_sec - ts0.tv_sec) * 1e3 + (ts1.tv_nsec - ts0.tv_nsec) / 1e6;\n");
    fprintf(o, "  printf(\"%%.17g\\n\", r);\n");
    fprintf(o, "  fprintf(stderr, \"ms=%%.3f\\n\", ms);\n  return 0;\n}\n");
    fclose(o);

    fprintf(stderr, "aotc: compiled %d function(s), entry '%s' -> %s\n",
            g_nfns, entry, out);
    return 0;
}
