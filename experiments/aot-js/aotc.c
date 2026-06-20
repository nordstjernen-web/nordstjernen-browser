/* aotc.c — ahead-of-time JavaScript compiler (numeric domain).
   Reuses the QuickJS front end to compile JS to bytecode, proves a
   function is in the safe numeric subset, and lowers its bytecode to C.
   It is sound: it emits native code only when the result is guaranteed
   identical to the interpreter, and declines (exit code 10) otherwise so
   the caller can fall back to interpretation. */

#include "quickjs.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FNS 256
#define DECLINE 10

#define MAX_CALLEES 256

typedef struct {
    JSFunctionBytecode *b;
    const char *name;
    char *mangled;
    int local_ok;
    int eligible;
    const char *callees[MAX_CALLEES];
    int n_callees;
} Fn;

static Fn g_fns[MAX_FNS];
static int g_nfns;
static JSContext *g_ctx;

static JSFunctionBytecode *get_fb(JSValue v) {
    int tag = JS_VALUE_GET_TAG(v);
    if (tag == JS_TAG_FUNCTION_BYTECODE) return JS_VALUE_GET_PTR(v);
    if (tag == JS_TAG_OBJECT) return JS_GetFunctionBytecode(v);
    return NULL;
}

static int find_fn(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < g_nfns; i++)
        if (strcmp(g_fns[i].name, name) == 0) return i;
    return -1;
}

static const char *mangled_of(const char *name) {
    int i = find_fn(name);
    return i >= 0 ? g_fns[i].mangled : NULL;
}

/* A JS name is usable only if it is a non-empty run of C-identifier
   characters; anything else (Unicode, '$', empty) is declined. The emitted
   symbol is always prefixed, so it can never collide with a C keyword, a
   libc/libm symbol, or our own helpers regardless of the JS name. */
static int valid_js_name(const char *name) {
    if (!name || !name[0]) return 0;
    for (const char *c = name; *c; c++)
        if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
              (*c >= '0' && *c <= '9') || *c == '_'))
            return 0;
    return 1;
}

static const char *resolve_atom_fn(JSAtom atom) {
    const char *nm = JS_AtomToCString(g_ctx, atom);
    int i = nm ? find_fn(nm) : -1;
    const char *r = (i >= 0) ? g_fns[i].name : NULL;
    JS_FreeCString(g_ctx, nm);
    return r;
}

/* The single source of truth for the supported opcode set and its stack
   effect. Returns 1 and fills pop/push for a supported opcode, 0 for an
   unsupported one (which makes the whole function ineligible). */
static int op_delta(const uint8_t *code, int pos, int *pop, int *push) {
    int op = code[pos], p = pos + 1;
    int np = 0, ns = 0;
    switch (op) {
    case OP_push_0: case OP_push_1: case OP_push_2: case OP_push_3:
    case OP_push_4: case OP_push_5: case OP_push_6: case OP_push_7:
    case OP_push_minus1: case OP_push_i8: case OP_push_i16: case OP_push_i32:
    case OP_push_const8: case OP_push_const:
    case OP_get_arg0: case OP_get_arg1: case OP_get_arg2: case OP_get_arg3:
    case OP_get_arg:
    case OP_get_loc0: case OP_get_loc1: case OP_get_loc2: case OP_get_loc3:
    case OP_get_loc8: case OP_get_loc: case OP_get_loc_check:
    case OP_get_var_ref0: case OP_get_var_ref1: case OP_get_var_ref2:
    case OP_get_var_ref3: case OP_get_var_ref: case OP_get_var_ref_check:
    case OP_get_var:
        np = 0; ns = 1; break;
    case OP_put_loc0: case OP_put_loc1: case OP_put_loc2: case OP_put_loc3:
    case OP_put_loc8: case OP_put_loc: case OP_put_loc_check:
    case OP_put_loc_check_init:
    case OP_drop: case OP_if_false: case OP_if_false8:
    case OP_if_true: case OP_if_true8: case OP_return:
    case OP_add_loc:
        np = 1; ns = 0; break;
    case OP_set_loc0: case OP_set_loc1: case OP_set_loc2: case OP_set_loc3:
    case OP_set_loc8: case OP_set_loc:
    case OP_inc: case OP_dec: case OP_neg: case OP_plus: case OP_lnot:
    case OP_not:
        np = 1; ns = 1; break;
    case OP_dup: case OP_post_inc: case OP_post_dec:
        np = 1; ns = 2; break;
    case OP_add: case OP_sub: case OP_mul: case OP_div: case OP_mod:
    case OP_pow: case OP_lt: case OP_lte: case OP_gt: case OP_gte:
    case OP_eq: case OP_neq: case OP_strict_eq: case OP_strict_neq:
    case OP_and: case OP_or: case OP_xor:
    case OP_shl: case OP_shr: case OP_sar:
        np = 2; ns = 1; break;
    case OP_goto: case OP_goto8: case OP_goto16:
    case OP_nop: case OP_set_loc_uninitialized:
    case OP_inc_loc: case OP_dec_loc:
        np = 0; ns = 0; break;
    case OP_return_undef:
        np = 0; ns = 0; break;
    case OP_call0: np = 1 + 0; ns = 1; break;
    case OP_call1: np = 1 + 1; ns = 1; break;
    case OP_call2: np = 1 + 2; ns = 1; break;
    case OP_call3: np = 1 + 3; ns = 1; break;
    case OP_call:  np = 1 + get_u16(code + p); ns = 1; break;
    case OP_tail_call: np = 1 + get_u16(code + p); ns = 0; break;
    default:
        return 0;
    }
    *pop = np; *push = ns;
    return 1;
}

static int branch_target(const uint8_t *code, int pos, int *is_uncond) {
    int op = code[pos], p = pos + 1;
    *is_uncond = 0;
    switch (op) {
    case OP_goto:   *is_uncond = 1; return p + get_i32(code + p);
    case OP_goto16: *is_uncond = 1; return p + get_i16(code + p);
    case OP_goto8:  *is_uncond = 1; return p + get_i8(code + p);
    case OP_if_false: case OP_if_true: return p + get_i32(code + p);
    case OP_if_false8: case OP_if_true8: return p + get_i8(code + p);
    }
    return -1;
}

static int terminates(int op) {
    return op == OP_return || op == OP_return_undef || op == OP_tail_call;
}

/* Abstract-interpret the operand-stack depth at each pc. Returns 1 with
   sp_before filled, or 0 if an unsupported opcode or an inconsistent
   depth is found (either makes the function ineligible). */
static int compute_sp(JSFunctionBytecode *b, int *sp_before) {
    const uint8_t *code = b->byte_code_buf;
    int len = b->byte_code_len;
    for (int i = 0; i < len; i++) sp_before[i] = -1;

    int stack[4096], depth[4096], top = 0;
    stack[top] = 0; depth[top] = 0; top++;

    while (top > 0) {
        int pos = stack[--top], d = depth[top];
        if (pos < 0 || pos >= len) return 0;
        if (sp_before[pos] >= 0) {
            if (sp_before[pos] != d) return 0;
            continue;
        }
        sp_before[pos] = d;
        int op = code[pos];
        int pop, push;
        if (!op_delta(code, pos, &pop, &push)) return 0;
        int after = d - pop + push;
        if (after < 0) return 0;
        int sz = short_opcode_info(op).size;
        int uncond;
        int tgt = branch_target(code, pos, &uncond);
        if (tgt >= 0) {
            if (top >= 4090) return 0;
            stack[top] = tgt; depth[top] = after; top++;
        }
        if (!terminates(op) && !uncond) {
            int npos = pos + sz;
            if (npos < len) {
                if (top >= 4090) return 0;
                stack[top] = npos; depth[top] = after; top++;
            }
        }
    }
    return 1;
}

/* Every call must target a statically-known top-level function. The only
   ops that put a function value on the stack are get_var / get_var_ref* (and
   dup propagating one); a call whose function slot was produced any other
   way — e.g. a function-valued argument or local (higher-order code) — is
   not in the numeric subset and is rejected here, before it can reach the
   emitter. Markers are produced and consumed within a basic block, so they
   are cleared conservatively at every branch target. */
static int validate_calls(JSFunctionBytecode *b, int *sp_before) {
    const uint8_t *code = b->byte_code_buf;
    int len = b->byte_code_len, maxstack = b->stack_size + 4;
    char *isfn = calloc(maxstack > 0 ? maxstack : 1, 1);
    char *islabel = calloc(len + 1, 1);
    for (int pos = 0; pos < len; )  {
        int u, t = branch_target(code, pos, &u);
        if (t >= 0 && t <= len) islabel[t] = 1;
        pos += short_opcode_info(code[pos]).size;
    }
    int ok = 1, pos = 0;
    while (pos < len && ok) {
        if (islabel[pos]) for (int i = 0; i < maxstack; i++) isfn[i] = 0;
        int op = code[pos], p = pos + 1, sp = sp_before[pos];
        int pop, push;
        op_delta(code, pos, &pop, &push);
        if (sp < 0) { pos += short_opcode_info(op).size; continue; }
        if (op == OP_get_var) {
            if (sp < maxstack) isfn[sp] = resolve_atom_fn((JSAtom)get_u32(code + p)) != NULL;
        } else if (op == OP_get_var_ref || op == OP_get_var_ref_check) {
            if (sp < maxstack) isfn[sp] = resolve_atom_fn(b->closure_var[get_u16(code + p)].var_name) != NULL;
        } else if (op >= OP_get_var_ref0 && op <= OP_get_var_ref3) {
            if (sp < maxstack) isfn[sp] = resolve_atom_fn(b->closure_var[op - OP_get_var_ref0].var_name) != NULL;
        } else if (op == OP_dup) {
            if (sp < maxstack && sp - 1 >= 0) isfn[sp] = isfn[sp - 1];
        } else if (op == OP_call0 || op == OP_call1 || op == OP_call2 ||
                   op == OP_call3 || op == OP_call || op == OP_tail_call) {
            int argc = (op == OP_call || op == OP_tail_call) ? get_u16(code + p) : (op - OP_call0);
            int fnslot = sp - 1 - argc;
            if (fnslot < 0 || fnslot >= maxstack || !isfn[fnslot]) ok = 0;
            else isfn[fnslot] = 0;
        } else {
            int base = sp - pop;
            for (int i = base; i < base + push && i < maxstack; i++)
                if (i >= 0) isfn[i] = 0;
        }
        pos += short_opcode_info(op).size;
    }
    free(isfn);
    free(islabel);
    return ok;
}

/* A function passes the local check if every opcode is supported, every
   constant it pushes is numeric, every variable/closure reference it reads
   names a known top-level function (the only non-numeric value the subset
   allows, and only as a call target), and its stack depth is consistent. */
static int add_callee(Fn *fn, const char *name) {
    if (!name) return 0;
    for (int i = 0; i < fn->n_callees; i++)
        if (fn->callees[i] == name) return 1;
    if (fn->n_callees >= MAX_CALLEES) return 0;
    fn->callees[fn->n_callees++] = name;
    return 1;
}

static int local_check(Fn *fn, int *sp_scratch) {
    JSFunctionBytecode *b = fn->b;
    const uint8_t *code = b->byte_code_buf;
    int len = b->byte_code_len, pos = 0;
    fn->n_callees = 0;

    if (!valid_js_name(fn->name)) return 0;

    while (pos < len) {
        int op = code[pos], p = pos + 1, pop, push;
        if (!op_delta(code, pos, &pop, &push)) return 0;
        if (op == OP_push_const8 || op == OP_push_const) {
            int idx = (op == OP_push_const8) ? get_u8(code + p) : get_u32(code + p);
            if (idx < 0 || idx >= b->cpool_count) return 0;
            JSValue v = b->cpool[idx];
            int tag = JS_VALUE_GET_TAG(v);
            if (tag != JS_TAG_INT && !JS_TAG_IS_FLOAT64(tag)) return 0;
        } else if (op == OP_get_var) {
            if (!add_callee(fn, resolve_atom_fn((JSAtom)get_u32(code + p)))) return 0;
        } else if (op == OP_get_var_ref || op == OP_get_var_ref_check) {
            int idx = get_u16(code + p);
            if (idx < 0 || idx >= b->closure_var_count) return 0;
            if (!add_callee(fn, resolve_atom_fn(b->closure_var[idx].var_name))) return 0;
        } else if (op >= OP_get_var_ref0 && op <= OP_get_var_ref3) {
            int idx = op - OP_get_var_ref0;
            if (idx >= b->closure_var_count) return 0;
            if (!add_callee(fn, resolve_atom_fn(b->closure_var[idx].var_name))) return 0;
        }
        pos += short_opcode_info(op).size;
    }
    if (!compute_sp(b, sp_scratch)) return 0;
    return validate_calls(b, sp_scratch);
}

/* Greatest fixpoint: assume every locally-OK function is eligible, then
   repeatedly demote any function that calls an unknown or ineligible one
   until nothing changes. This is sound for mutual/cyclic recursion — a
   cycle survives only if every member and every external callee survives. */
static void compute_eligible_all(void) {
    for (int i = 0; i < g_nfns; i++)
        g_fns[i].eligible = g_fns[i].local_ok ? 1 : 0;
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < g_nfns; i++) {
            if (!g_fns[i].eligible) continue;
            for (int j = 0; j < g_fns[i].n_callees; j++) {
                int ci = find_fn(g_fns[i].callees[j]);
                if (ci < 0 || !g_fns[ci].eligible) {
                    g_fns[i].eligible = 0;
                    changed = 1;
                    break;
                }
            }
        }
    }
}

/* ---- emission --------------------------------------------------------- */

static const char **g_slotfn;

static void emit_fn(FILE *o, Fn *fn, int *sp_before) {
    JSFunctionBytecode *b = fn->b;
    const uint8_t *code = b->byte_code_buf;
    int len = b->byte_code_len;
    int maxstack = b->stack_size + 4;
    g_slotfn = calloc(maxstack, sizeof(*g_slotfn));

    fprintf(o, "static double %s(", fn->mangled);
    for (int i = 0; i < b->arg_count; i++)
        fprintf(o, "%sdouble a%d", i ? ", " : "", i);
    if (b->arg_count == 0) fprintf(o, "void");
    fprintf(o, ") {\n");
    for (int i = 0; i < b->var_count; i++)
        fprintf(o, "  double loc%d = 0;\n", i);
    for (int i = 0; i < maxstack; i++)
        fprintf(o, "  double s%d = 0; (void)s%d;\n", i, i);

    /* mark branch targets */
    char *islabel = calloc(len + 1, 1);
    for (int pos = 0; pos < len; ) {
        int u, t = branch_target(code, pos, &u);
        if (t >= 0 && t <= len) islabel[t] = 1;
        pos += short_opcode_info(code[pos]).size;
    }

    int pos = 0;
    while (pos < len) {
        if (islabel[pos]) fprintf(o, "L%d: ;\n", pos);
        int op = code[pos], p = pos + 1;
        int sp = sp_before[pos];
        if (sp < 0) { pos += short_opcode_info(op).size; continue; } /* dead */

        switch (op) {
        case OP_push_0: case OP_push_1: case OP_push_2: case OP_push_3:
        case OP_push_4: case OP_push_5: case OP_push_6: case OP_push_7:
            fprintf(o, "  s%d = %d;\n", sp, op - OP_push_0); g_slotfn[sp] = NULL; break;
        case OP_push_minus1: fprintf(o, "  s%d = -1;\n", sp); g_slotfn[sp] = NULL; break;
        case OP_push_i8:  fprintf(o, "  s%d = %d;\n", sp, get_i8(code + p)); g_slotfn[sp] = NULL; break;
        case OP_push_i16: fprintf(o, "  s%d = %d;\n", sp, get_i16(code + p)); g_slotfn[sp] = NULL; break;
        case OP_push_i32: fprintf(o, "  s%d = %d;\n", sp, (int)get_i32(code + p)); g_slotfn[sp] = NULL; break;
        case OP_push_const8: case OP_push_const: {
            int idx = (op == OP_push_const8) ? get_u8(code + p) : get_u32(code + p);
            JSValue v = b->cpool[idx];
            if (JS_VALUE_GET_TAG(v) == JS_TAG_INT)
                fprintf(o, "  s%d = %d;\n", sp, JS_VALUE_GET_INT(v));
            else
                fprintf(o, "  s%d = %.17g;\n", sp, JS_VALUE_GET_FLOAT64(v));
            g_slotfn[sp] = NULL; break;
        }
        case OP_get_arg0: case OP_get_arg1: case OP_get_arg2: case OP_get_arg3:
            fprintf(o, "  s%d = a%d;\n", sp, op - OP_get_arg0); g_slotfn[sp] = NULL; break;
        case OP_get_arg:
            fprintf(o, "  s%d = a%d;\n", sp, get_u16(code + p)); g_slotfn[sp] = NULL; break;
        case OP_get_loc0: case OP_get_loc1: case OP_get_loc2: case OP_get_loc3:
            fprintf(o, "  s%d = loc%d;\n", sp, op - OP_get_loc0); g_slotfn[sp] = NULL; break;
        case OP_get_loc8:
            fprintf(o, "  s%d = loc%d;\n", sp, get_u8(code + p)); g_slotfn[sp] = NULL; break;
        case OP_get_loc: case OP_get_loc_check:
            fprintf(o, "  s%d = loc%d;\n", sp, get_u16(code + p)); g_slotfn[sp] = NULL; break;
        case OP_put_loc0: case OP_put_loc1: case OP_put_loc2: case OP_put_loc3:
            fprintf(o, "  loc%d = s%d;\n", op - OP_put_loc0, sp - 1); break;
        case OP_put_loc8:
            fprintf(o, "  loc%d = s%d;\n", get_u8(code + p), sp - 1); break;
        case OP_put_loc: case OP_put_loc_check: case OP_put_loc_check_init:
            fprintf(o, "  loc%d = s%d;\n", get_u16(code + p), sp - 1); break;
        case OP_set_loc0: case OP_set_loc1: case OP_set_loc2: case OP_set_loc3:
            fprintf(o, "  loc%d = s%d;\n", op - OP_set_loc0, sp - 1); break;
        case OP_set_loc8:
            fprintf(o, "  loc%d = s%d;\n", get_u8(code + p), sp - 1); break;
        case OP_set_loc:
            fprintf(o, "  loc%d = s%d;\n", get_u16(code + p), sp - 1); break;
        case OP_inc_loc: fprintf(o, "  loc%d += 1;\n", get_u8(code + p)); break;
        case OP_dec_loc: fprintf(o, "  loc%d -= 1;\n", get_u8(code + p)); break;
        case OP_add_loc: fprintf(o, "  loc%d += s%d;\n", get_u8(code + p), sp - 1); break;
        case OP_dup:
            fprintf(o, "  s%d = s%d;\n", sp, sp - 1); g_slotfn[sp] = g_slotfn[sp - 1]; break;
        case OP_drop: g_slotfn[sp - 1] = NULL; break;
        case OP_post_inc:
            fprintf(o, "  s%d = s%d + 1;\n", sp, sp - 1); g_slotfn[sp] = NULL; break;
        case OP_post_dec:
            fprintf(o, "  s%d = s%d - 1;\n", sp, sp - 1); g_slotfn[sp] = NULL; break;
        case OP_inc: fprintf(o, "  s%d = s%d + 1;\n", sp - 1, sp - 1); break;
        case OP_dec: fprintf(o, "  s%d = s%d - 1;\n", sp - 1, sp - 1); break;
        case OP_neg: fprintf(o, "  s%d = -s%d;\n", sp - 1, sp - 1); break;
        case OP_plus: break;
        case OP_lnot: fprintf(o, "  s%d = (s%d == 0);\n", sp - 1, sp - 1); break;
        case OP_not: fprintf(o, "  s%d = (double)(~js_to_int32(s%d));\n", sp - 1, sp - 1); break;
        case OP_add: fprintf(o, "  s%d = s%d + s%d;\n", sp - 2, sp - 2, sp - 1); break;
        case OP_sub: fprintf(o, "  s%d = s%d - s%d;\n", sp - 2, sp - 2, sp - 1); break;
        case OP_mul: fprintf(o, "  s%d = s%d * s%d;\n", sp - 2, sp - 2, sp - 1); break;
        case OP_div: fprintf(o, "  s%d = s%d / s%d;\n", sp - 2, sp - 2, sp - 1); break;
        case OP_mod: fprintf(o, "  s%d = fmod(s%d, s%d);\n", sp - 2, sp - 2, sp - 1); break;
        case OP_pow: fprintf(o, "  s%d = pow(s%d, s%d);\n", sp - 2, sp - 2, sp - 1); break;
        case OP_lt:  fprintf(o, "  s%d = (s%d <  s%d);\n", sp - 2, sp - 2, sp - 1); break;
        case OP_lte: fprintf(o, "  s%d = (s%d <= s%d);\n", sp - 2, sp - 2, sp - 1); break;
        case OP_gt:  fprintf(o, "  s%d = (s%d >  s%d);\n", sp - 2, sp - 2, sp - 1); break;
        case OP_gte: fprintf(o, "  s%d = (s%d >= s%d);\n", sp - 2, sp - 2, sp - 1); break;
        case OP_eq: case OP_strict_eq:
            fprintf(o, "  s%d = (s%d == s%d);\n", sp - 2, sp - 2, sp - 1); break;
        case OP_neq: case OP_strict_neq:
            fprintf(o, "  s%d = (s%d != s%d);\n", sp - 2, sp - 2, sp - 1); break;
        case OP_and:
            fprintf(o, "  s%d = (double)(js_to_int32(s%d) & js_to_int32(s%d));\n", sp - 2, sp - 2, sp - 1); break;
        case OP_or:
            fprintf(o, "  s%d = (double)(js_to_int32(s%d) | js_to_int32(s%d));\n", sp - 2, sp - 2, sp - 1); break;
        case OP_xor:
            fprintf(o, "  s%d = (double)(js_to_int32(s%d) ^ js_to_int32(s%d));\n", sp - 2, sp - 2, sp - 1); break;
        case OP_shl:
            fprintf(o, "  s%d = (double)(int32_t)(js_to_uint32(s%d) << (js_to_uint32(s%d) & 31));\n", sp - 2, sp - 2, sp - 1); break;
        case OP_sar:
            fprintf(o, "  s%d = (double)(js_to_int32(s%d) >> (js_to_uint32(s%d) & 31));\n", sp - 2, sp - 2, sp - 1); break;
        case OP_shr:
            fprintf(o, "  s%d = (double)(js_to_uint32(s%d) >> (js_to_uint32(s%d) & 31));\n", sp - 2, sp - 2, sp - 1); break;
        case OP_if_false: case OP_if_false8: {
            int u, t = branch_target(code, pos, &u);
            fprintf(o, "  if (!(s%d)) goto L%d;\n", sp - 1, t); break;
        }
        case OP_if_true: case OP_if_true8: {
            int u, t = branch_target(code, pos, &u);
            fprintf(o, "  if (s%d) goto L%d;\n", sp - 1, t); break;
        }
        case OP_goto: case OP_goto8: case OP_goto16: {
            int u, t = branch_target(code, pos, &u);
            fprintf(o, "  goto L%d;\n", t); break;
        }
        case OP_call0: case OP_call1: case OP_call2: case OP_call3:
        case OP_call: case OP_tail_call: {
            int argc = (op == OP_call || op == OP_tail_call)
                       ? get_u16(code + p) : (op - OP_call0);
            int fnslot = sp - 1 - argc;
            int ci = (fnslot >= 0) ? find_fn(g_slotfn[fnslot]) : -1;
            if (ci < 0) { fprintf(stderr, "aotc: lost call target\n"); free(islabel); free(g_slotfn); exit(DECLINE); }
            const char *callee = g_fns[ci].mangled;
            int arity = g_fns[ci].b->arg_count;
            size_t cap = 64 + (size_t)arity * 24;
            char *rhs = malloc(cap);
            int off = snprintf(rhs, cap, "%s(", callee);
            for (int i = 0; i < arity; i++) {
                if (i < argc)
                    off += snprintf(rhs + off, cap - off, "%ss%d", i ? ", " : "", fnslot + 1 + i);
                else
                    off += snprintf(rhs + off, cap - off, "%s(0.0/0.0)", i ? ", " : "");
            }
            snprintf(rhs + off, cap - off, ")");
            if (op == OP_tail_call) {
                fprintf(o, "  return %s;\n", rhs);
            } else {
                fprintf(o, "  s%d = %s;\n", fnslot, rhs);
                g_slotfn[fnslot] = NULL;
            }
            free(rhs);
            break;
        }
        case OP_get_var: {
            g_slotfn[sp] = resolve_atom_fn((JSAtom)get_u32(code + p)); break;
        }
        case OP_get_var_ref0: case OP_get_var_ref1:
        case OP_get_var_ref2: case OP_get_var_ref3:
            g_slotfn[sp] = resolve_atom_fn(b->closure_var[op - OP_get_var_ref0].var_name); break;
        case OP_get_var_ref: case OP_get_var_ref_check:
            g_slotfn[sp] = resolve_atom_fn(b->closure_var[get_u16(code + p)].var_name); break;
        case OP_return: fprintf(o, "  return s%d;\n", sp - 1); break;
        case OP_return_undef: fprintf(o, "  return 0;\n"); break;
        case OP_nop: case OP_set_loc_uninitialized: break;
        default: fprintf(stderr, "aotc: internal: op %d\n", op); free(islabel); free(g_slotfn); exit(DECLINE);
        }
        pos += short_opcode_info(op).size;
    }
    fprintf(o, "  return 0;\n}\n\n");
    free(islabel);
    free(g_slotfn);
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
    g_ctx = JS_NewContext(rt);

    JSValue top = JS_Eval(g_ctx, src, n, in,
                          JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(top)) {
        JSValue e = JS_GetException(g_ctx);
        fprintf(stderr, "aotc: compile error: %s\n", JS_ToCString(g_ctx, e));
        return 1;
    }
    JSFunctionBytecode *bt = get_fb(top);

    for (int i = 0; i < bt->cpool_count && g_nfns < MAX_FNS; i++) {
        JSFunctionBytecode *fb = get_fb(bt->cpool[i]);
        if (!fb) continue;
        if (fb->func_name == JS_ATOM_NULL) continue;
        const char *nm = JS_AtomToCString(g_ctx, fb->func_name);
        if (!nm) continue;
        if (!nm[0]) { JS_FreeCString(g_ctx, nm); continue; }
        char *mn = malloc(strlen(nm) + 8);
        sprintf(mn, "jsfn_%s", nm);
        g_fns[g_nfns].b = fb;
        g_fns[g_nfns].name = strdup(nm);
        g_fns[g_nfns].mangled = mn;
        g_fns[g_nfns].eligible = -1;
        g_nfns++;
        JS_FreeCString(g_ctx, nm);
    }
    if (g_nfns == 0) { fprintf(stderr, "aotc: no functions\n"); return DECLINE; }
    if (!entry) entry = g_fns[0].name;

    long maxlen = 1;
    for (int i = 0; i < g_nfns; i++)
        if (g_fns[i].b->byte_code_len > maxlen) maxlen = g_fns[i].b->byte_code_len;
    int *sp = malloc(sizeof(int) * (maxlen + 1));
    for (int i = 0; i < g_nfns; i++)
        g_fns[i].local_ok = local_check(&g_fns[i], sp);
    compute_eligible_all();

    int ei = find_fn(entry);
    if (ei < 0) { fprintf(stderr, "aotc: no entry '%s'\n", entry); return 1; }
    if (!g_fns[ei].eligible) {
        fprintf(stderr, "aotc: '%s' not AOT-eligible; fall back to interpreter\n", entry);
        return DECLINE;
    }

    FILE *o = fopen(out, "w");
    fprintf(o, "/* generated by aotc — ahead-of-time JS->C (numeric) */\n");
    fprintf(o, "#include <stdio.h>\n#include <stdlib.h>\n#include <stdint.h>\n"
               "#include <math.h>\n#include <time.h>\n\n");
    fprintf(o,
        "static int32_t js_to_int32(double d) {\n"
        "  if (d >= -2147483648.0 && d < 2147483648.0) return (int32_t)d;\n"
        "  if (!isfinite(d)) return 0;\n"
        "  double m = fmod(trunc(d), 4294967296.0);\n"
        "  if (m < 0) m += 4294967296.0;\n"
        "  return (int32_t)(uint32_t)m;\n"
        "}\n"
        "static uint32_t js_to_uint32(double d) {\n"
        "  if (d >= 0.0 && d < 4294967296.0) return (uint32_t)d;\n"
        "  return (uint32_t)js_to_int32(d);\n"
        "}\n\n");
    for (int i = 0; i < g_nfns; i++) {
        if (!g_fns[i].eligible) continue;
        fprintf(o, "static double %s(", g_fns[i].mangled);
        int ac = g_fns[i].b->arg_count;
        for (int j = 0; j < ac; j++) fprintf(o, "%sdouble", j ? ", " : "");
        if (ac == 0) fprintf(o, "void");
        fprintf(o, ");\n");
    }
    fprintf(o, "\n");
    for (int i = 0; i < g_nfns; i++) {
        if (!g_fns[i].eligible) continue;
        compute_sp(g_fns[i].b, sp);
        emit_fn(o, &g_fns[i], sp);
    }

    int eac = g_fns[ei].b->arg_count;
    fprintf(o, "int main(int argc, char **argv) {\n  double r = 0;\n");
    for (int i = 0; i < eac; i++)
        fprintf(o, "  double a%d = (argc > %d) ? atof(argv[%d]) : 0;\n", i, i + 1, i + 1);
    fprintf(o, "  long reps = (argc > %d) ? atol(argv[%d]) : 1;\n", eac + 1, eac + 1);
    fprintf(o, "  struct timespec t0, t1;\n");
    fprintf(o, "  clock_gettime(CLOCK_MONOTONIC, &t0);\n");
    fprintf(o, "  for (long k = 0; k < reps; k++) r = %s(", g_fns[ei].mangled);
    for (int i = 0; i < eac; i++) fprintf(o, "%sa%d", i ? ", " : "", i);
    fprintf(o, ");\n");
    fprintf(o, "  clock_gettime(CLOCK_MONOTONIC, &t1);\n");
    fprintf(o, "  double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;\n");
    fprintf(o, "  printf(\"%%.17g\\n\", r);\n");
    fprintf(o, "  fprintf(stderr, \"ms=%%.3f\\n\", ms);\n  return 0;\n}\n");
    fclose(o);

    int nc = 0;
    for (int i = 0; i < g_nfns; i++) nc += g_fns[i].eligible;
    fprintf(stderr, "aotc: AOT-compiled %d/%d function(s), entry '%s' -> %s\n",
            nc, g_nfns, entry, out);
    return 0;
}
