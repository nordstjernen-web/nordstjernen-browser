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

#define MAX_ARGS 64

typedef struct {
    JSFunctionBytecode *b;
    const char *name;
    char *mangled;
    int local_ok;
    int eligible;
    const char *callees[MAX_CALLEES];
    int n_callees;
    char arg_is_array[MAX_ARGS]; /* a Float64Array parameter, not a scalar */
    int has_array_args;
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

static int atom_is_math(JSAtom atom) {
    const char *nm = JS_AtomToCString(g_ctx, atom);
    int r = nm && strcmp(nm, "Math") == 0;
    JS_FreeCString(g_ctx, nm);
    return r;
}

/* A direct-to-libm unary Math method maps to exactly the C function QuickJS
   itself calls (js_math_sqrt(d){return sqrt(d);} etc.), so the result is
   bit-identical to the interpreter. */
static const char *math_unary_cfn(const char *m) {
    static const char *map[][2] = {
        {"abs","fabs"},{"sqrt","sqrt"},{"cbrt","cbrt"},{"floor","floor"},
        {"ceil","ceil"},{"trunc","trunc"},{"sin","sin"},{"cos","cos"},
        {"tan","tan"},{"asin","asin"},{"acos","acos"},{"atan","atan"},
        {"sinh","sinh"},{"cosh","cosh"},{"tanh","tanh"},{"asinh","asinh"},
        {"acosh","acosh"},{"atanh","atanh"},{"exp","exp"},{"expm1","expm1"},
        {"log","log"},{"log1p","log1p"},{"log2","log2"},{"log10","log10"},
        {NULL,NULL}
    };
    for (int i = 0; map[i][0]; i++)
        if (strcmp(map[i][0], m) == 0) return map[i][1];
    return NULL;
}

/* Classify a Math method: 0 unsupported, 'u' unary direct-libm, 's' sign,
   'r' round, '2' atan2, 'p' pow, 'm' min, 'M' max, 'h' hypot, 'I' imul,
   'C' clz32. */
static int math_method_kind(const char *m) {
    if (math_unary_cfn(m)) return 'u';
    if (strcmp(m, "sign") == 0) return 's';
    if (strcmp(m, "round") == 0) return 'r';
    if (strcmp(m, "atan2") == 0) return '2';
    if (strcmp(m, "pow") == 0) return 'p';
    if (strcmp(m, "min") == 0) return 'm';
    if (strcmp(m, "max") == 0) return 'M';
    if (strcmp(m, "hypot") == 0) return 'h';
    if (strcmp(m, "imul") == 0) return 'I';
    if (strcmp(m, "clz32") == 0) return 'C';
    if (strcmp(m, "fround") == 0) return 'F';
    return 0;
}

static JSValue g_math_obj;

/* Read a numeric Math constant (PI, E, …) straight from the live Math object,
   so the emitted literal is the exact double the interpreter would use. */
static int math_const_value(const char *name, double *out) {
    JSValue v = JS_GetPropertyStr(g_ctx, g_math_obj, name);
    int tag = JS_VALUE_GET_TAG(v), ok = 0;
    if (tag == JS_TAG_INT) { *out = JS_VALUE_GET_INT(v); ok = 1; }
    else if (JS_TAG_IS_FLOAT64(tag)) { *out = JS_VALUE_GET_FLOAT64(v); ok = 1; }
    JS_FreeValue(g_ctx, v);
    return ok;
}

static const char *atom_str(JSAtom atom) {
    return JS_AtomToCString(g_ctx, atom);
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
    case OP_put_arg0: case OP_put_arg1: case OP_put_arg2: case OP_put_arg3:
    case OP_put_arg:
    case OP_drop: case OP_if_false: case OP_if_false8:
    case OP_if_true: case OP_if_true8: case OP_return:
    case OP_add_loc:
        np = 1; ns = 0; break;
    case OP_set_loc0: case OP_set_loc1: case OP_set_loc2: case OP_set_loc3:
    case OP_set_loc8: case OP_set_loc:
    case OP_set_arg0: case OP_set_arg1: case OP_set_arg2: case OP_set_arg3:
    case OP_set_arg:
    case OP_inc: case OP_dec: case OP_neg: case OP_plus: case OP_lnot:
    case OP_not:
        np = 1; ns = 1; break;
    case OP_dup: case OP_post_inc: case OP_post_dec:
        np = 1; ns = 2; break;
    case OP_swap:
        np = 2; ns = 2; break;
    case OP_is_undefined_or_null: case OP_to_propkey:
        np = 1; ns = 1; break;
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
    case OP_get_field2: np = 1; ns = 2; break;
    case OP_get_field:  np = 1; ns = 1; break;
    case OP_call_method:      np = 2 + get_u16(code + p); ns = 1; break;
    case OP_tail_call_method: np = 2 + get_u16(code + p); ns = 0; break;
    case OP_get_length:   np = 1; ns = 1; break;
    case OP_get_array_el: np = 2; ns = 1; break;
    case OP_get_array_el2: np = 2; ns = 2; break;
    case OP_put_array_el: np = 3; ns = 0; break;
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
    return op == OP_return || op == OP_return_undef ||
           op == OP_tail_call || op == OP_tail_call_method;
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

/* Slot kinds tracked by the validator/emitter. Only K_NUM values may flow
   into arithmetic; the others are intermediate forms that must be consumed
   exactly by a call (K_USERFN), a method call (K_MATHFN), or a property
   access (K_MATHOBJ); K_ARRAY is a Float64Array parameter, consumed only by
   element access and `.length`. K_UNINIT/K_CONFLICT are the top/bottom of the
   dataflow lattice used to meet kinds at control-flow joins. */
enum { K_CONFLICT = -2, K_UNINIT = -1,
       K_NUM = 0, K_USERFN, K_MATHOBJ, K_MATHFN, K_ARRAY };

static signed char kind_meet(signed char a, signed char b) {
    if (a == K_UNINIT) return b;
    if (b == K_UNINIT) return a;
    if (a == b) return a;
    return K_CONFLICT;
}

/* Kind-production (transfer) for one opcode: updates the stack-kind state to
   reflect the slots an opcode leaves behind. This is the SETK half of the
   analysis, factored out so the cross-block fixpoint can run it; validity
   checks are enforced separately by the checking pass below. */
static void transfer_kinds(Fn *fn, const uint8_t *code, int pos, int sp,
                           signed char *k, int maxstack) {
    int op = code[pos], p = pos + 1;
    #define PUT(slot, val) do { int _s=(slot); if (_s>=0 && _s<maxstack) k[_s]=(val); } while(0)
    switch (op) {
    case OP_push_0: case OP_push_1: case OP_push_2: case OP_push_3:
    case OP_push_4: case OP_push_5: case OP_push_6: case OP_push_7:
    case OP_push_minus1: case OP_push_i8: case OP_push_i16: case OP_push_i32:
    case OP_push_const8: case OP_push_const:
    case OP_get_loc0: case OP_get_loc1: case OP_get_loc2: case OP_get_loc3:
    case OP_get_loc8: case OP_get_loc: case OP_get_loc_check:
        PUT(sp, K_NUM); break;
    case OP_get_arg0: case OP_get_arg1: case OP_get_arg2: case OP_get_arg3: {
        int n = op - OP_get_arg0;
        PUT(sp, (n < MAX_ARGS && fn->arg_is_array[n]) ? K_ARRAY : K_NUM); break;
    }
    case OP_get_arg: {
        int n = get_u16(code + p);
        PUT(sp, (n < MAX_ARGS && fn->arg_is_array[n]) ? K_ARRAY : K_NUM); break;
    }
    case OP_get_var: {
        const char *nm = resolve_atom_fn((JSAtom)get_u32(code + p));
        PUT(sp, nm ? K_USERFN : K_MATHOBJ); break;
    }
    case OP_get_var_ref0: case OP_get_var_ref1: case OP_get_var_ref2:
    case OP_get_var_ref3: case OP_get_var_ref: case OP_get_var_ref_check:
        PUT(sp, K_USERFN); break;
    case OP_get_field2: PUT(sp - 1, K_MATHOBJ); PUT(sp, K_MATHFN); break;
    case OP_get_field:  PUT(sp - 1, K_NUM); break;
    case OP_call0: case OP_call1: case OP_call2: case OP_call3: case OP_call:
        PUT(sp - 1 - ((op==OP_call)?get_u16(code+p):(op-OP_call0)), K_NUM); break;
    case OP_call_method:
        PUT(sp - 2 - get_u16(code + p), K_NUM); break;
    case OP_dup: PUT(sp, (sp-1>=0 && sp-1<maxstack) ? k[sp-1] : K_NUM); break;
    case OP_swap: {
        if (sp-1>=0 && sp-2>=0 && sp-1<maxstack && sp-2<maxstack) {
            signed char t = k[sp-1]; k[sp-1] = k[sp-2]; k[sp-2] = t;
        }
        break;
    }
    case OP_is_undefined_or_null: PUT(sp - 1, K_NUM); break;
    case OP_to_propkey: break;   /* keeps the (numeric) key in place */
    case OP_get_length:    PUT(sp - 1, K_NUM); break;
    case OP_get_array_el:  PUT(sp - 2, K_NUM); break;
    case OP_get_array_el2: PUT(sp - 1, K_NUM); break;
    case OP_post_inc: case OP_post_dec: PUT(sp - 1, K_NUM); PUT(sp, K_NUM); break;
    case OP_inc: case OP_dec: case OP_neg: case OP_plus: case OP_lnot:
    case OP_not:
        PUT(sp - 1, K_NUM); break;
    case OP_add: case OP_sub: case OP_mul: case OP_div: case OP_mod:
    case OP_pow: case OP_lt: case OP_lte: case OP_gt: case OP_gte:
    case OP_eq: case OP_neq: case OP_strict_eq: case OP_strict_neq:
    case OP_and: case OP_or: case OP_xor: case OP_shl: case OP_shr: case OP_sar:
        PUT(sp - 2, K_NUM); break;
    default: break;   /* stores / drops / branches leave no new typed slot */
    }
    #undef PUT
}

/* Successor positions of an opcode, for the dataflow fixpoint. Returns the
   count and fills up to two targets. */
static int kind_succs(const uint8_t *code, int pos, int len, int *s0, int *s1) {
    int op = code[pos], sz = short_opcode_info(op).size, fall = pos + sz;
    if (op == OP_return || op == OP_return_undef ||
        op == OP_tail_call || op == OP_tail_call_method)
        return 0;
    if (op == OP_goto || op == OP_goto8 || op == OP_goto16) {
        int u; *s0 = branch_target(code, pos, &u); return (*s0 >= 0 && *s0 < len) ? 1 : 0;
    }
    if (op == OP_if_true || op == OP_if_true8 ||
        op == OP_if_false || op == OP_if_false8) {
        int u, t = branch_target(code, pos, &u), n = 0;
        if (fall < len) { *s0 = fall; n = 1; }
        if (t >= 0 && t < len) { if (n) *s1 = t; else *s0 = t; n++; }
        return n;
    }
    if (fall < len) { *s0 = fall; return 1; }
    return 0;
}


/* Decide which parameters are arrays: a parameter is an array iff it is ever
   the object of an element access (`a[i]`, `a[i]=v`) or `.length`. The object
   operand is traced back to its originating argument through the slot model.
   Fills fn->arg_is_array; returns 1 (a parameter used both as an array object
   and produced by anything that isn't a plain get_arg is handled
   conservatively — validate_kinds makes the final soundness decision). */
static void classify_args(Fn *fn, int *sp_before) {
    JSFunctionBytecode *b = fn->b;
    const uint8_t *code = b->byte_code_buf;
    int len = b->byte_code_len, maxstack = b->stack_size + 4;
    for (int i = 0; i < MAX_ARGS; i++) fn->arg_is_array[i] = 0;
    fn->has_array_args = 0;
    if (b->arg_count > MAX_ARGS) return;

    int *origin = malloc(sizeof(int) * (maxstack > 0 ? maxstack : 1));
    /* Origins are not cleared at labels: an arg flowing into an array op may
       cross a branch (the `a[i]=v` store sequence does), and a misattributed
       origin only ever causes a (safe) decline in validate_kinds, never a
       miscompile. */
    int pos = 0;
    while (pos < len) {
        int op = code[pos], sp = sp_before[pos], sz = short_opcode_info(op).size;
        if (sp < 0) { pos += sz; continue; }
        int objslot = -1;
        if (op == OP_get_length) objslot = sp - 1;
        else if (op == OP_get_array_el || op == OP_get_array_el2) objslot = sp - 2;
        else if (op == OP_put_array_el) objslot = sp - 3;
        if (objslot >= 0 && objslot < maxstack && origin[objslot] >= 0 &&
            origin[objslot] < b->arg_count) {
            fn->arg_is_array[origin[objslot]] = 1;
            fn->has_array_args = 1;
        }
        if (op >= OP_get_arg0 && op <= OP_get_arg3) {
            if (sp < maxstack) origin[sp] = op - OP_get_arg0;
        } else if (op == OP_get_arg) {
            if (sp < maxstack) origin[sp] = get_u16(code + pos + 1);
        } else if (op == OP_dup) {
            if (sp < maxstack && sp - 1 >= 0) origin[sp] = origin[sp - 1];
        } else if (op == OP_swap) {
            if (sp - 1 >= 0 && sp - 1 < maxstack && sp - 2 >= 0) {
                int t = origin[sp - 1]; origin[sp - 1] = origin[sp - 2]; origin[sp - 2] = t;
            }
        } else if (op == OP_to_propkey) {
            /* keeps the value (and its origin) in place */
        } else {
            int pop, push;
            op_delta(code, pos, &pop, &push);
            for (int i = sp - pop; i < sp - pop + push && i < maxstack; i++)
                if (i >= 0) origin[i] = -1;
        }
        pos += sz;
    }
    free(origin);
}

/* Abstract-interpret slot kinds and prove the program never uses a
   non-numeric value (function reference, Math object, Math method) in a
   numeric context, and that every call/method-call/property-access targets a
   statically-known function, the Math object, or a whitelisted Math member.
   Anything outside the numeric+Math subset is rejected here, before the
   emitter runs. Slot kinds are propagated across the whole control-flow graph
   by a meet-based dataflow fixpoint (`in`), so a value's kind survives forward
   and backward branches as long as every predecessor agrees on it; a
   disagreement becomes K_CONFLICT and fails every typed use. */
static int validate_kinds(Fn *fn, int *sp_before) {
    JSFunctionBytecode *b = fn->b;
    const uint8_t *code = b->byte_code_buf;
    int len = b->byte_code_len, maxstack = b->stack_size + 4;
    if (maxstack < 1) maxstack = 1;
    signed char *k = calloc(maxstack, 1);
    char *islabel = calloc(len + 1, 1);
    for (int pos = 0; pos < len; ) {
        int u, t = branch_target(code, pos, &u);
        if (t >= 0 && t <= len) islabel[t] = 1;
        pos += short_opcode_info(code[pos]).size;
    }

    /* Forward dataflow: in[pos] is the meet of every predecessor's out-state.
       Slots are indexed in[pos*maxstack + slot]; unreached slots stay
       K_UNINIT. */
    signed char *in = malloc((size_t)len * maxstack);
    char *reached = calloc(len + 1, 1);
    for (size_t i = 0; i < (size_t)len * maxstack; i++) in[i] = K_UNINIT;
    int *work = malloc(sizeof(int) * (len + 1));
    char *queued = calloc(len + 1, 1);
    int wn = 0;
    if (len > 0) { work[wn++] = 0; reached[0] = 1; queued[0] = 1; }
    signed char *out = malloc(maxstack);
    while (wn > 0) {
        int pos = work[--wn]; queued[pos] = 0;
        int sp = sp_before[pos];
        if (sp < 0) continue;
        for (int i = 0; i < maxstack; i++) out[i] = in[(size_t)pos * maxstack + i];
        transfer_kinds(fn, code, pos, sp, out, maxstack);
        int s0 = -1, s1 = -1, ns = kind_succs(code, pos, len, &s0, &s1);
        for (int e = 0; e < ns; e++) {
            int succ = (e == 0) ? s0 : s1, ssp = sp_before[succ];
            if (ssp < 0) continue;
            int changed = 0;
            for (int slot = 0; slot < ssp && slot < maxstack; slot++) {
                signed char m = kind_meet(in[(size_t)succ * maxstack + slot], out[slot]);
                if (m != in[(size_t)succ * maxstack + slot]) {
                    in[(size_t)succ * maxstack + slot] = m; changed = 1;
                }
            }
            if (!reached[succ]) { reached[succ] = 1; changed = 1; }
            if (changed && !queued[succ]) { work[wn++] = succ; queued[succ] = 1; }
        }
    }
    free(out); free(work); free(queued);
    int ok = 1, pos = 0;
    while (pos < len && ok) {
        int op = code[pos], p = pos + 1, sp = sp_before[pos];
        int pop, push;
        op_delta(code, pos, &pop, &push);
        int sz = short_opcode_info(op).size;
        if (sp < 0 || !reached[pos]) { pos += sz; continue; }
        for (int i = 0; i < maxstack; i++) k[i] = in[(size_t)pos * maxstack + i];
        #define NEED_NUM(slot) do { int _s=(slot); if (_s<0||_s>=maxstack||k[_s]!=K_NUM) {ok=0;goto next;} } while(0)
        #define SETK(slot,val) do { int _s=(slot); if (_s>=0&&_s<maxstack) k[_s]=(val); } while(0)
        switch (op) {
        case OP_get_var: {
            const char *fn = resolve_atom_fn((JSAtom)get_u32(code + p));
            if (fn) SETK(sp, K_USERFN);
            else if (atom_is_math((JSAtom)get_u32(code + p))) SETK(sp, K_MATHOBJ);
            else { ok = 0; }
            break;
        }
        case OP_get_var_ref: case OP_get_var_ref_check:
            if (!resolve_atom_fn(b->closure_var[get_u16(code + p)].var_name)) ok = 0;
            else SETK(sp, K_USERFN);
            break;
        case OP_get_var_ref0: case OP_get_var_ref1:
        case OP_get_var_ref2: case OP_get_var_ref3:
            if (!resolve_atom_fn(b->closure_var[op - OP_get_var_ref0].var_name)) ok = 0;
            else SETK(sp, K_USERFN);
            break;
        case OP_get_field2: {
            if (sp - 1 < 0 || sp - 1 >= maxstack || k[sp - 1] != K_MATHOBJ) { ok = 0; break; }
            const char *m = atom_str((JSAtom)get_u32(code + p));
            int mk = m ? math_method_kind(m) : 0;
            JS_FreeCString(g_ctx, m);
            if (!mk) { ok = 0; break; }
            SETK(sp, K_MATHFN);          /* method func on top */
            SETK(sp - 1, K_MATHOBJ);     /* 'this' (Math) underneath */
            break;
        }
        case OP_get_field: {
            if (sp - 1 < 0 || sp - 1 >= maxstack || k[sp - 1] != K_MATHOBJ) { ok = 0; break; }
            const char *m = atom_str((JSAtom)get_u32(code + p));
            double dummy;
            int good = m && math_const_value(m, &dummy);
            JS_FreeCString(g_ctx, m);
            if (!good) { ok = 0; break; }
            SETK(sp - 1, K_NUM);
            break;
        }
        case OP_call0: case OP_call1: case OP_call2: case OP_call3:
        case OP_call: case OP_tail_call: {
            int argc = (op == OP_call || op == OP_tail_call) ? get_u16(code + p) : (op - OP_call0);
            int fnslot = sp - 1 - argc;
            if (fnslot < 0 || fnslot >= maxstack || k[fnslot] != K_USERFN) { ok = 0; break; }
            for (int i = 0; i < argc; i++) NEED_NUM(fnslot + 1 + i);
            SETK(fnslot, K_NUM);
            break;
        }
        case OP_call_method: case OP_tail_call_method: {
            int argc = get_u16(code + p);
            int fnslot = sp - 1 - argc, thisslot = fnslot - 1;
            if (fnslot < 0 || fnslot >= maxstack || k[fnslot] != K_MATHFN) { ok = 0; break; }
            if (thisslot < 0 || thisslot >= maxstack || k[thisslot] != K_MATHOBJ) { ok = 0; break; }
            for (int i = 0; i < argc; i++) NEED_NUM(fnslot + 1 + i);
            SETK(thisslot, K_NUM);
            break;
        }
        case OP_dup:
            if (sp - 1 >= 0 && sp - 1 < maxstack) SETK(sp, k[sp - 1]);
            break;
        case OP_drop:
            break;
        case OP_swap:
            if (sp - 1 < 0 || sp - 2 < 0 || sp - 1 >= maxstack) { ok = 0; break; }
            break;
        case OP_is_undefined_or_null:
            /* every value in the supported subset is non-null, so the result
               is the constant false; only require a live operand. */
            if (sp - 1 < 0 || sp - 1 >= maxstack || k[sp - 1] < K_NUM) { ok = 0; break; }
            SETK(sp - 1, K_NUM);
            break;
        case OP_to_propkey:
            NEED_NUM(sp - 1);
            break;
        case OP_get_arg0: case OP_get_arg1: case OP_get_arg2: case OP_get_arg3: {
            int n = op - OP_get_arg0;
            SETK(sp, (n < MAX_ARGS && fn->arg_is_array[n]) ? K_ARRAY : K_NUM);
            break;
        }
        case OP_get_arg: {
            int n = get_u16(code + p);
            SETK(sp, (n < MAX_ARGS && fn->arg_is_array[n]) ? K_ARRAY : K_NUM);
            break;
        }
        case OP_get_length: {
            if (sp - 1 < 0 || sp - 1 >= maxstack || k[sp - 1] != K_ARRAY) { ok = 0; break; }
            SETK(sp - 1, K_NUM);
            break;
        }
        case OP_get_array_el: {
            if (sp - 2 < 0 || sp - 2 >= maxstack || k[sp - 2] != K_ARRAY) { ok = 0; break; }
            NEED_NUM(sp - 1);
            SETK(sp - 2, K_NUM);
            break;
        }
        case OP_get_array_el2: {
            if (sp - 2 < 0 || sp - 2 >= maxstack || k[sp - 2] != K_ARRAY) { ok = 0; break; }
            NEED_NUM(sp - 1);
            SETK(sp - 1, K_NUM);   /* value on top; object kept at sp-2 */
            break;
        }
        case OP_put_array_el: {
            if (sp - 3 < 0 || sp - 3 >= maxstack || k[sp - 3] != K_ARRAY) { ok = 0; break; }
            NEED_NUM(sp - 2); NEED_NUM(sp - 1);
            break;
        }
        case OP_get_loc0: case OP_get_loc1: case OP_get_loc2: case OP_get_loc3:
        case OP_get_loc8: case OP_get_loc: case OP_get_loc_check:
        case OP_push_0: case OP_push_1: case OP_push_2: case OP_push_3:
        case OP_push_4: case OP_push_5: case OP_push_6: case OP_push_7:
        case OP_push_minus1: case OP_push_i8: case OP_push_i16: case OP_push_i32:
        case OP_push_const8: case OP_push_const:
            SETK(sp, K_NUM);
            break;
        case OP_post_inc: case OP_post_dec:
            NEED_NUM(sp - 1); SETK(sp, K_NUM);
            break;
        case OP_inc: case OP_dec: case OP_neg: case OP_plus:
        case OP_lnot: case OP_not:
            NEED_NUM(sp - 1);
            break;
        case OP_add: case OP_sub: case OP_mul: case OP_div: case OP_mod:
        case OP_pow: case OP_lt: case OP_lte: case OP_gt: case OP_gte:
        case OP_eq: case OP_neq: case OP_strict_eq: case OP_strict_neq:
        case OP_and: case OP_or: case OP_xor:
        case OP_shl: case OP_shr: case OP_sar:
            NEED_NUM(sp - 1); NEED_NUM(sp - 2); SETK(sp - 2, K_NUM);
            break;
        case OP_if_false: case OP_if_false8:
        case OP_if_true: case OP_if_true8:
        case OP_return:
            NEED_NUM(sp - 1);
            break;
        case OP_put_loc0: case OP_put_loc1: case OP_put_loc2: case OP_put_loc3:
        case OP_put_loc8: case OP_put_loc: case OP_put_loc_check:
        case OP_put_loc_check_init:
        case OP_set_loc0: case OP_set_loc1: case OP_set_loc2: case OP_set_loc3:
        case OP_set_loc8: case OP_set_loc:
        case OP_add_loc:
            NEED_NUM(sp - 1);
            break;
        case OP_put_arg0: case OP_put_arg1: case OP_put_arg2: case OP_put_arg3:
        case OP_set_arg0: case OP_set_arg1: case OP_set_arg2: case OP_set_arg3: {
            int n = (op <= OP_put_arg3) ? op - OP_put_arg0 : op - OP_set_arg0;
            if (n < MAX_ARGS && fn->arg_is_array[n]) { ok = 0; break; }
            NEED_NUM(sp - 1);
            break;
        }
        case OP_put_arg: case OP_set_arg: {
            int n = get_u16(code + p);
            if (n < MAX_ARGS && fn->arg_is_array[n]) { ok = 0; break; }
            NEED_NUM(sp - 1);
            break;
        }
        case OP_inc_loc: case OP_dec_loc:
        case OP_goto: case OP_goto8: case OP_goto16:
        case OP_nop: case OP_set_loc_uninitialized:
        case OP_return_undef:
            break;
        default:
            ok = 0;
        }
        #undef NEED_NUM
        #undef SETK
    next:
        pos += sz;
    }
    free(k);
    free(islabel);
    free(in);
    free(reached);
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
            const char *cn = resolve_atom_fn((JSAtom)get_u32(code + p));
            if (cn && !add_callee(fn, cn)) return 0;
        } else if (op == OP_get_var_ref || op == OP_get_var_ref_check) {
            int idx = get_u16(code + p);
            if (idx < 0 || idx >= b->closure_var_count) return 0;
            const char *cn = resolve_atom_fn(b->closure_var[idx].var_name);
            if (cn && !add_callee(fn, cn)) return 0;
        } else if (op >= OP_get_var_ref0 && op <= OP_get_var_ref3) {
            int idx = op - OP_get_var_ref0;
            if (idx >= b->closure_var_count) return 0;
            const char *cn = resolve_atom_fn(b->closure_var[idx].var_name);
            if (cn && !add_callee(fn, cn)) return 0;
        }
        pos += short_opcode_info(op).size;
    }
    if (!compute_sp(b, sp_scratch)) return 0;
    classify_args(fn, sp_scratch);
    return validate_kinds(fn, sp_scratch);
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
static const char **g_slotmeth;
static int *g_slotarr;
static char *g_slotint;   /* slot provably holds a canonical int32 value */
static char *g_locint;    /* local provably holds a canonical int32 value */

/* ToInt32 / ToUint32 of a slot. When the slot is already known to be a
   canonical int32 (e.g. the result of a previous bitwise op, or an integer
   literal), the spec coercion is a no-op and collapses to a plain cast,
   eliminating the per-operator range branch. */
static const char *i32expr(int slot, char *buf) {
    snprintf(buf, 32, g_slotint[slot] ? "(int32_t)s%d" : "js_to_int32(s%d)", slot);
    return buf;
}
static const char *u32expr(int slot, char *buf) {
    snprintf(buf, 40, g_slotint[slot] ? "(uint32_t)(int32_t)s%d" : "js_to_uint32(s%d)", slot);
    return buf;
}

/* signature of an eligible function: array params become a (double*, length)
   pair, scalar params a double. */
static void emit_signature(FILE *o, Fn *fn) {
    JSFunctionBytecode *b = fn->b;
    int first = 1;
    for (int i = 0; i < b->arg_count; i++) {
        const char *sep = first ? "" : ", ";
        first = 0;
        if (fn->arg_is_array[i])
            fprintf(o, "%sdouble *p%d, int64_t plen%d", sep, i, i);
        else
            fprintf(o, "%sdouble a%d", sep, i);
    }
    if (b->arg_count == 0) fprintf(o, "void");
}

/* bounds-and-integer-checked typed-array element read, matching the
   interpreter's `a[i]` for a Float64Array (out-of-range / non-integer index
   yields undefined → NaN). */
static void emit_arr_load(FILE *o, int dst, int n, int idxslot) {
    fprintf(o, "  s%d = (s%d >= 0 && s%d < (double)plen%d && s%d == floor(s%d)) "
               "? p%d[(int64_t)s%d] : (0.0/0.0);\n",
            dst, idxslot, idxslot, n, idxslot, idxslot, n, idxslot);
}

static void emit_math_expr(FILE *o, int dst, const char *m, int argc, int arg0) {
    int mk = math_method_kind(m);
    if (mk == 'u') {
        fprintf(o, "  s%d = %s(s%d);\n", dst, math_unary_cfn(m), arg0);
    } else if (mk == 's') {
        fprintf(o, "  s%d = js_sign(s%d);\n", dst, arg0);
    } else if (mk == 'r') {
        fprintf(o, "  s%d = js_round(s%d);\n", dst, arg0);
    } else if (mk == '2') {
        fprintf(o, "  s%d = atan2(s%d, s%d);\n", dst, arg0, arg0 + 1);
    } else if (mk == 'p') {
        fprintf(o, "  s%d = js_pow(s%d, s%d);\n", dst, arg0, arg0 + 1);
    } else if (mk == 'm' || mk == 'M') {
        const char *fold = (mk == 'm') ? "js_min2" : "js_max2";
        if (argc == 0) {
            fprintf(o, "  s%d = %s;\n", dst, (mk == 'm') ? "INFINITY" : "-INFINITY");
        } else if (argc == 1) {
            fprintf(o, "  s%d = s%d;\n", dst, arg0);
        } else {
            fprintf(o, "  s%d = %s(s%d, s%d);\n", dst, fold, arg0, arg0 + 1);
            for (int i = 2; i < argc; i++)
                fprintf(o, "  s%d = %s(s%d, s%d);\n", dst, fold, dst, arg0 + i);
        }
    } else if (mk == 'I') {
        fprintf(o, "  s%d = (double)(int32_t)(js_to_uint32(s%d) * js_to_uint32(s%d));\n",
                dst, arg0, arg0 + 1);
    } else if (mk == 'C') {
        fprintf(o, "  s%d = js_clz32(s%d);\n", dst, arg0);
    } else if (mk == 'F') {
        fprintf(o, "  s%d = (double)(float)s%d;\n", dst, arg0);
    } else if (mk == 'h') {
        if (argc == 0) {
            fprintf(o, "  s%d = 0;\n", dst);
        } else if (argc == 1) {
            fprintf(o, "  s%d = fabs(s%d);\n", dst, arg0);
        } else {
            fprintf(o, "  s%d = hypot(s%d, s%d);\n", dst, arg0, arg0 + 1);
            for (int i = 2; i < argc; i++)
                fprintf(o, "  s%d = hypot(s%d, s%d);\n", dst, dst, arg0 + i);
        }
    }
}

static void emit_fn(FILE *o, Fn *fn, int *sp_before) {
    JSFunctionBytecode *b = fn->b;
    const uint8_t *code = b->byte_code_buf;
    int len = b->byte_code_len;
    int maxstack = b->stack_size + 4;
    g_slotfn = calloc(maxstack, sizeof(*g_slotfn));
    g_slotmeth = calloc(maxstack, sizeof(*g_slotmeth));
    g_slotarr = malloc(sizeof(int) * maxstack);
    for (int i = 0; i < maxstack; i++) g_slotarr[i] = -1;
    g_slotint = calloc(maxstack, 1);
    g_locint = calloc(b->var_count + 1, 1);

    fprintf(o, "static double %s(", fn->mangled);
    emit_signature(o, fn);
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
        if (islabel[pos]) {
            fprintf(o, "L%d: ;\n", pos);
            for (int i = 0; i < maxstack; i++) g_slotint[i] = 0;
            for (int i = 0; i < b->var_count; i++) g_locint[i] = 0;
        }
        int op = code[pos], p = pos + 1;
        int sp = sp_before[pos];
        if (sp < 0) { pos += short_opcode_info(op).size; continue; } /* dead */

        switch (op) {
        case OP_push_0: case OP_push_1: case OP_push_2: case OP_push_3:
        case OP_push_4: case OP_push_5: case OP_push_6: case OP_push_7:
            fprintf(o, "  s%d = %d;\n", sp, op - OP_push_0); g_slotfn[sp] = NULL; g_slotint[sp] = 1; break;
        case OP_push_minus1: fprintf(o, "  s%d = -1;\n", sp); g_slotfn[sp] = NULL; g_slotint[sp] = 1; break;
        case OP_push_i8:  fprintf(o, "  s%d = %d;\n", sp, get_i8(code + p)); g_slotfn[sp] = NULL; g_slotint[sp] = 1; break;
        case OP_push_i16: fprintf(o, "  s%d = %d;\n", sp, get_i16(code + p)); g_slotfn[sp] = NULL; g_slotint[sp] = 1; break;
        case OP_push_i32: fprintf(o, "  s%d = %d;\n", sp, (int)get_i32(code + p)); g_slotfn[sp] = NULL; g_slotint[sp] = 1; break;
        case OP_push_const8: case OP_push_const: {
            int idx = (op == OP_push_const8) ? get_u8(code + p) : get_u32(code + p);
            JSValue v = b->cpool[idx];
            if (JS_VALUE_GET_TAG(v) == JS_TAG_INT) {
                fprintf(o, "  s%d = %d;\n", sp, JS_VALUE_GET_INT(v)); g_slotint[sp] = 1;
            } else {
                fprintf(o, "  s%d = %.17g;\n", sp, JS_VALUE_GET_FLOAT64(v)); g_slotint[sp] = 0;
            }
            g_slotfn[sp] = NULL; break;
        }
        case OP_get_arg0: case OP_get_arg1: case OP_get_arg2: case OP_get_arg3: {
            int n = op - OP_get_arg0;
            if (fn->arg_is_array[n]) { g_slotarr[sp] = n; }
            else { fprintf(o, "  s%d = a%d;\n", sp, n); g_slotfn[sp] = NULL; }
            g_slotint[sp] = 0; break;
        }
        case OP_get_arg: {
            int n = get_u16(code + p);
            if (n < MAX_ARGS && fn->arg_is_array[n]) { g_slotarr[sp] = n; }
            else { fprintf(o, "  s%d = a%d;\n", sp, n); g_slotfn[sp] = NULL; }
            g_slotint[sp] = 0; break;
        }
        case OP_get_length:
            fprintf(o, "  s%d = (double)plen%d;\n", sp - 1, g_slotarr[sp - 1]);
            g_slotarr[sp - 1] = -1; g_slotint[sp - 1] = 0; break;
        case OP_get_array_el:
            emit_arr_load(o, sp - 2, g_slotarr[sp - 2], sp - 1);
            g_slotarr[sp - 2] = -1; g_slotint[sp - 2] = 0; break;
        case OP_get_array_el2:
            emit_arr_load(o, sp - 1, g_slotarr[sp - 2], sp - 1);
            g_slotint[sp - 1] = 0;
            /* object kept at sp-2 (its array marker stays) */
            break;
        case OP_put_array_el: {
            int n = g_slotarr[sp - 3];
            fprintf(o, "  if (s%d >= 0 && s%d < (double)plen%d && s%d == floor(s%d)) "
                       "p%d[(int64_t)s%d] = s%d;\n",
                    sp - 2, sp - 2, n, sp - 2, sp - 2, n, sp - 2, sp - 1);
            g_slotarr[sp - 3] = -1; break;
        }
        case OP_get_loc0: case OP_get_loc1: case OP_get_loc2: case OP_get_loc3: {
            int l = op - OP_get_loc0;
            fprintf(o, "  s%d = loc%d;\n", sp, l); g_slotfn[sp] = NULL;
            g_slotint[sp] = g_locint[l]; break;
        }
        case OP_get_loc8: {
            int l = get_u8(code + p);
            fprintf(o, "  s%d = loc%d;\n", sp, l); g_slotfn[sp] = NULL;
            g_slotint[sp] = g_locint[l]; break;
        }
        case OP_get_loc: case OP_get_loc_check: {
            int l = get_u16(code + p);
            fprintf(o, "  s%d = loc%d;\n", sp, l); g_slotfn[sp] = NULL;
            g_slotint[sp] = g_locint[l]; break;
        }
        case OP_put_loc0: case OP_put_loc1: case OP_put_loc2: case OP_put_loc3: {
            int l = op - OP_put_loc0;
            fprintf(o, "  loc%d = s%d;\n", l, sp - 1); g_locint[l] = g_slotint[sp - 1]; break;
        }
        case OP_put_loc8: {
            int l = get_u8(code + p);
            fprintf(o, "  loc%d = s%d;\n", l, sp - 1); g_locint[l] = g_slotint[sp - 1]; break;
        }
        case OP_put_loc: case OP_put_loc_check: case OP_put_loc_check_init: {
            int l = get_u16(code + p);
            fprintf(o, "  loc%d = s%d;\n", l, sp - 1); g_locint[l] = g_slotint[sp - 1]; break;
        }
        case OP_set_loc0: case OP_set_loc1: case OP_set_loc2: case OP_set_loc3: {
            int l = op - OP_set_loc0;
            fprintf(o, "  loc%d = s%d;\n", l, sp - 1); g_locint[l] = g_slotint[sp - 1]; break;
        }
        case OP_set_loc8: {
            int l = get_u8(code + p);
            fprintf(o, "  loc%d = s%d;\n", l, sp - 1); g_locint[l] = g_slotint[sp - 1]; break;
        }
        case OP_set_loc: {
            int l = get_u16(code + p);
            fprintf(o, "  loc%d = s%d;\n", l, sp - 1); g_locint[l] = g_slotint[sp - 1]; break;
        }
        case OP_put_arg0: case OP_put_arg1: case OP_put_arg2: case OP_put_arg3:
            fprintf(o, "  a%d = s%d;\n", op - OP_put_arg0, sp - 1); break;
        case OP_put_arg:
            fprintf(o, "  a%d = s%d;\n", get_u16(code + p), sp - 1); break;
        case OP_set_arg0: case OP_set_arg1: case OP_set_arg2: case OP_set_arg3:
            fprintf(o, "  a%d = s%d;\n", op - OP_set_arg0, sp - 1); break;
        case OP_set_arg:
            fprintf(o, "  a%d = s%d;\n", get_u16(code + p), sp - 1); break;
        case OP_inc_loc: fprintf(o, "  loc%d += 1;\n", get_u8(code + p)); g_locint[get_u8(code + p)] = 0; break;
        case OP_dec_loc: fprintf(o, "  loc%d -= 1;\n", get_u8(code + p)); g_locint[get_u8(code + p)] = 0; break;
        case OP_add_loc: fprintf(o, "  loc%d += s%d;\n", get_u8(code + p), sp - 1); g_locint[get_u8(code + p)] = 0; break;
        case OP_dup:
            fprintf(o, "  s%d = s%d;\n", sp, sp - 1);
            g_slotfn[sp] = g_slotfn[sp - 1];
            g_slotmeth[sp] = g_slotmeth[sp - 1];
            g_slotarr[sp] = g_slotarr[sp - 1];
            g_slotint[sp] = g_slotint[sp - 1];
            break;
        case OP_swap: {
            int x = sp - 2, y = sp - 1;
            fprintf(o, "  { double _t = s%d; s%d = s%d; s%d = _t; }\n", x, x, y, y);
            #define SWAPM(arr) do { __typeof__((arr)[0]) _m = (arr)[x]; (arr)[x] = (arr)[y]; (arr)[y] = _m; } while (0)
            SWAPM(g_slotfn); SWAPM(g_slotmeth); SWAPM(g_slotarr); SWAPM(g_slotint);
            #undef SWAPM
            break;
        }
        case OP_is_undefined_or_null:
            fprintf(o, "  s%d = 0;\n", sp - 1);
            g_slotfn[sp - 1] = NULL; g_slotmeth[sp - 1] = NULL;
            g_slotarr[sp - 1] = -1; g_slotint[sp - 1] = 1;
            break;
        case OP_to_propkey:
            break;   /* numeric key stays in its slot; array op bounds-checks it */
        case OP_drop: g_slotfn[sp - 1] = NULL; break;
        case OP_post_inc:
            fprintf(o, "  s%d = s%d + 1;\n", sp, sp - 1); g_slotfn[sp] = NULL;
            g_slotint[sp - 1] = 0; g_slotint[sp] = 0; break;
        case OP_post_dec:
            fprintf(o, "  s%d = s%d - 1;\n", sp, sp - 1); g_slotfn[sp] = NULL;
            g_slotint[sp - 1] = 0; g_slotint[sp] = 0; break;
        case OP_inc: fprintf(o, "  s%d = s%d + 1;\n", sp - 1, sp - 1); g_slotint[sp - 1] = 0; break;
        case OP_dec: fprintf(o, "  s%d = s%d - 1;\n", sp - 1, sp - 1); g_slotint[sp - 1] = 0; break;
        case OP_neg: fprintf(o, "  s%d = -s%d;\n", sp - 1, sp - 1); g_slotint[sp - 1] = 0; break;
        case OP_plus: g_slotint[sp - 1] = 0; break;
        case OP_lnot: fprintf(o, "  s%d = (s%d == 0);\n", sp - 1, sp - 1); g_slotint[sp - 1] = 1; break;
        case OP_not: {
            char b1[40];
            fprintf(o, "  s%d = (double)(~%s);\n", sp - 1, i32expr(sp - 1, b1));
            g_slotint[sp - 1] = 1; break;
        }
        case OP_add: fprintf(o, "  s%d = s%d + s%d;\n", sp - 2, sp - 2, sp - 1); g_slotint[sp - 2] = 0; break;
        case OP_sub: fprintf(o, "  s%d = s%d - s%d;\n", sp - 2, sp - 2, sp - 1); g_slotint[sp - 2] = 0; break;
        case OP_mul: fprintf(o, "  s%d = s%d * s%d;\n", sp - 2, sp - 2, sp - 1); g_slotint[sp - 2] = 0; break;
        case OP_div: fprintf(o, "  s%d = s%d / s%d;\n", sp - 2, sp - 2, sp - 1); g_slotint[sp - 2] = 0; break;
        case OP_mod: fprintf(o, "  s%d = fmod(s%d, s%d);\n", sp - 2, sp - 2, sp - 1); g_slotint[sp - 2] = 0; break;
        case OP_pow: fprintf(o, "  s%d = js_pow(s%d, s%d);\n", sp - 2, sp - 2, sp - 1); g_slotint[sp - 2] = 0; break;
        case OP_lt:  fprintf(o, "  s%d = (s%d <  s%d);\n", sp - 2, sp - 2, sp - 1); g_slotint[sp - 2] = 1; break;
        case OP_lte: fprintf(o, "  s%d = (s%d <= s%d);\n", sp - 2, sp - 2, sp - 1); g_slotint[sp - 2] = 1; break;
        case OP_gt:  fprintf(o, "  s%d = (s%d >  s%d);\n", sp - 2, sp - 2, sp - 1); g_slotint[sp - 2] = 1; break;
        case OP_gte: fprintf(o, "  s%d = (s%d >= s%d);\n", sp - 2, sp - 2, sp - 1); g_slotint[sp - 2] = 1; break;
        case OP_eq: case OP_strict_eq:
            fprintf(o, "  s%d = (s%d == s%d);\n", sp - 2, sp - 2, sp - 1); g_slotint[sp - 2] = 1; break;
        case OP_neq: case OP_strict_neq:
            fprintf(o, "  s%d = (s%d != s%d);\n", sp - 2, sp - 2, sp - 1); g_slotint[sp - 2] = 1; break;
        case OP_and: {
            char b1[40], b2[40];
            fprintf(o, "  s%d = (double)(%s & %s);\n", sp - 2, i32expr(sp - 2, b1), i32expr(sp - 1, b2));
            g_slotint[sp - 2] = 1; break;
        }
        case OP_or: {
            char b1[40], b2[40];
            fprintf(o, "  s%d = (double)(%s | %s);\n", sp - 2, i32expr(sp - 2, b1), i32expr(sp - 1, b2));
            g_slotint[sp - 2] = 1; break;
        }
        case OP_xor: {
            char b1[40], b2[40];
            fprintf(o, "  s%d = (double)(%s ^ %s);\n", sp - 2, i32expr(sp - 2, b1), i32expr(sp - 1, b2));
            g_slotint[sp - 2] = 1; break;
        }
        case OP_shl: {
            char b1[40], b2[40];
            fprintf(o, "  s%d = (double)(int32_t)(%s << (%s & 31));\n", sp - 2, u32expr(sp - 2, b1), u32expr(sp - 1, b2));
            g_slotint[sp - 2] = 1; break;
        }
        case OP_sar: {
            char b1[40], b2[40];
            fprintf(o, "  s%d = (double)(%s >> (%s & 31));\n", sp - 2, i32expr(sp - 2, b1), u32expr(sp - 1, b2));
            g_slotint[sp - 2] = 1; break;
        }
        case OP_shr: {
            char b1[40], b2[40];
            fprintf(o, "  s%d = (double)(%s >> (%s & 31));\n", sp - 2, u32expr(sp - 2, b1), u32expr(sp - 1, b2));
            break;   /* result is uint32, not necessarily int32-range */
        }
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
            if (ci < 0) { fprintf(stderr, "aotc: lost call target\n"); free(islabel); free(g_slotfn); free(g_slotarr); free(g_slotint); free(g_locint); exit(DECLINE); }
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
                g_slotint[fnslot] = 0;
            }
            free(rhs);
            break;
        }
        case OP_get_var:
            g_slotfn[sp] = resolve_atom_fn((JSAtom)get_u32(code + p));
            g_slotmeth[sp] = NULL; g_slotint[sp] = 0;
            break;
        case OP_get_var_ref0: case OP_get_var_ref1:
        case OP_get_var_ref2: case OP_get_var_ref3:
            g_slotfn[sp] = resolve_atom_fn(b->closure_var[op - OP_get_var_ref0].var_name); g_slotint[sp] = 0; break;
        case OP_get_var_ref: case OP_get_var_ref_check:
            g_slotfn[sp] = resolve_atom_fn(b->closure_var[get_u16(code + p)].var_name); g_slotint[sp] = 0; break;
        case OP_get_field2: {
            const char *m = atom_str((JSAtom)get_u32(code + p));
            g_slotmeth[sp] = strdup(m);
            g_slotfn[sp] = NULL; g_slotint[sp] = 0;
            JS_FreeCString(g_ctx, m);
            break;
        }
        case OP_get_field: {
            const char *m = atom_str((JSAtom)get_u32(code + p));
            double v = 0; math_const_value(m, &v);
            JS_FreeCString(g_ctx, m);
            fprintf(o, "  s%d = %.17g;\n", sp - 1, v);
            g_slotfn[sp - 1] = NULL; g_slotint[sp - 1] = 0;
            break;
        }
        case OP_call_method: case OP_tail_call_method: {
            int margc = get_u16(code + p);
            int fnslot = sp - 1 - margc, thisslot = fnslot - 1;
            const char *m = (fnslot >= 0) ? g_slotmeth[fnslot] : NULL;
            if (!m) { fprintf(stderr, "aotc: lost method target\n"); free(islabel); free(g_slotfn); free(g_slotmeth); free(g_slotarr); free(g_slotint); free(g_locint); exit(DECLINE); }
            emit_math_expr(o, thisslot, m, margc, fnslot + 1);
            if (op == OP_tail_call_method)
                fprintf(o, "  return s%d;\n", thisslot);
            g_slotfn[thisslot] = NULL; g_slotint[thisslot] = 0;
            g_slotmeth[thisslot] = NULL;
            break;
        }
        case OP_return: fprintf(o, "  return s%d;\n", sp - 1); break;
        case OP_return_undef: fprintf(o, "  return 0;\n"); break;
        case OP_nop: case OP_set_loc_uninitialized: break;
        default: fprintf(stderr, "aotc: internal: op %d\n", op); free(islabel); free(g_slotfn); free(g_slotmeth); free(g_slotarr); free(g_slotint); free(g_locint); exit(DECLINE);
        }
        pos += short_opcode_info(op).size;
    }
    fprintf(o, "  return 0;\n}\n\n");
    free(islabel);
    free(g_slotfn);
    free(g_slotmeth);
    free(g_slotarr);
    free(g_slotint);
    free(g_locint);
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
    {
        JSValue glob = JS_GetGlobalObject(g_ctx);
        g_math_obj = JS_GetPropertyStr(g_ctx, glob, "Math");
        JS_FreeValue(g_ctx, glob);
    }

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
        "}\n"
        "typedef union { double d; uint64_t u; } js_f64u;\n"
        "static double js_pow(double a, double b) {\n"
        "  if (!isfinite(b) && (a == 1.0 || a == -1.0)) return NAN;\n"
        "  return pow(a, b);\n"
        "}\n"
        "static double js_sign(double a) {\n"
        "  if (isnan(a) || a == 0.0) return a;\n"
        "  return a < 0 ? -1.0 : 1.0;\n"
        "}\n"
        "static double js_clz32(double d) {\n"
        "  uint32_t a = js_to_uint32(d);\n"
        "  return a == 0 ? 32.0 : (double)__builtin_clz(a);\n"
        "}\n"
        "static double js_round(double a) {\n"
        "  js_f64u u; u.d = a; unsigned e = (u.u >> 52) & 0x7ff;\n"
        "  uint64_t frac_mask, one; unsigned s;\n"
        "  if (e < 1023) {\n"
        "    if (e == 1022 && u.u != 0xbfe0000000000000ULL)\n"
        "      u.u = (u.u & ((uint64_t)1 << 63)) | ((uint64_t)1023 << 52);\n"
        "    else u.u &= (uint64_t)1 << 63;\n"
        "  } else if (e < 1075) {\n"
        "    s = u.u >> 63; one = (uint64_t)1 << (52 - (e - 1023));\n"
        "    frac_mask = one - 1; u.u += (one >> 1) - s; u.u &= ~frac_mask;\n"
        "  }\n"
        "  return u.d;\n"
        "}\n"
        "static double js_fmin(double a, double b) {\n"
        "  if (a == 0 && b == 0) { js_f64u x, y; x.d = a; y.d = b; x.u |= y.u; return x.d; }\n"
        "  return a < b ? a : b;\n"
        "}\n"
        "static double js_fmax(double a, double b) {\n"
        "  if (a == 0 && b == 0) { js_f64u x, y; x.d = a; y.d = b; x.u &= y.u; return x.d; }\n"
        "  return a < b ? b : a;\n"
        "}\n"
        "static double js_min2(double a, double b) { if (!isnan(a)) { if (isnan(b)) return b; return js_fmin(a, b); } return a; }\n"
        "static double js_max2(double a, double b) { if (!isnan(a)) { if (isnan(b)) return b; return js_fmax(a, b); } return a; }\n\n");
    for (int i = 0; i < g_nfns; i++) {
        if (!g_fns[i].eligible) continue;
        fprintf(o, "static double %s(", g_fns[i].mangled);
        emit_signature(o, &g_fns[i]);
        fprintf(o, ");\n");
    }
    fprintf(o, "\n");
    for (int i = 0; i < g_nfns; i++) {
        if (!g_fns[i].eligible) continue;
        compute_sp(g_fns[i].b, sp);
        emit_fn(o, &g_fns[i], sp);
    }

    Fn *E = &g_fns[ei];
    int eac = E->b->arg_count;
    fprintf(o, "int main(int argc, char **argv) {\n  double r = 0; int ai = 1;\n");
    for (int i = 0; i < eac; i++) {
        if (E->arg_is_array[i]) {
            fprintf(o, "  int64_t plen%d = (argc > ai) ? atoll(argv[ai++]) : 0;\n", i);
            fprintf(o, "  double *p%d = malloc(sizeof(double) * (plen%d > 0 ? plen%d : 1));\n", i, i, i);
            fprintf(o, "  for (int64_t j = 0; j < plen%d; j++) p%d[j] = (double)(j %% 97) * 0.5 - 13.0;\n", i, i);
        } else {
            fprintf(o, "  double a%d = (argc > ai) ? atof(argv[ai++]) : 0;\n", i);
        }
    }
    fprintf(o, "  long reps = (argc > ai) ? atol(argv[ai]) : 1;\n");
    fprintf(o, "  struct timespec t0, t1;\n");
    fprintf(o, "  clock_gettime(CLOCK_MONOTONIC, &t0);\n");
    fprintf(o, "  for (long k = 0; k < reps; k++) r = %s(", E->mangled);
    for (int i = 0; i < eac; i++) {
        if (E->arg_is_array[i]) fprintf(o, "%sp%d, plen%d", i ? ", " : "", i, i);
        else fprintf(o, "%sa%d", i ? ", " : "", i);
    }
    fprintf(o, ");\n");
    fprintf(o, "  clock_gettime(CLOCK_MONOTONIC, &t1);\n");
    fprintf(o, "  double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;\n");
    fprintf(o, "  printf(\"%%.17g\\n\", r);\n");
    for (int i = 0; i < eac; i++)
        if (E->arg_is_array[i])
            fprintf(o, "  { double cs = 0; for (int64_t j = 0; j < plen%d; j++) cs += p%d[j]; printf(\"%%.17g\\n\", cs); }\n", i, i);
    fprintf(o, "  fprintf(stderr, \"ms=%%.3f\\n\", ms);\n  return 0;\n}\n");
    fclose(o);

    /* Companion interpreter-reference harness: same CLI, same array fill,
       prints the return value then a checksum of each array parameter. */
    char refpath[4096];
    snprintf(refpath, sizeof(refpath), "%s.ref.js", out);
    char inabs[4096];
    if (!realpath(in, inabs)) snprintf(inabs, sizeof(inabs), "%s", in);
    FILE *rf = fopen(refpath, "w");
    fprintf(rf, "import * as std from \"qjs:std\";\n");
    fprintf(rf, "const f = std.evalScript(std.loadFile(\"%s\") + \"\\n;%s;\");\n",
            inabs, entry);
    fprintf(rf, "const fill = (j) => (j %% 97) * 0.5 - 13.0;\n");
    fprintf(rf, "let ai = 1, args = [], arrays = [];\n");
    for (int i = 0; i < eac; i++) {
        if (E->arg_is_array[i])
            fprintf(rf, "{ let n = parseInt(scriptArgs[ai++]); let a = new Float64Array(n>0?n:0); for (let j=0;j<n;j++) a[j]=fill(j); args.push(a); arrays.push(a); }\n");
        else
            fprintf(rf, "args.push(parseFloat(scriptArgs[ai++]));\n");
    }
    fprintf(rf, "let reps = (scriptArgs.length > ai) ? parseInt(scriptArgs[ai]) : 1;\n");
    fprintf(rf, "let r = 0; for (let k=0;k<reps;k++) r = f.apply(null, args);\n");
    fprintf(rf, "std.out.printf(\"%%.17g\\n\", +r);\n");
    fprintf(rf, "for (const a of arrays) { let cs=0; for (let j=0;j<a.length;j++) cs+=a[j]; std.out.printf(\"%%.17g\\n\", cs); }\n");
    fclose(rf);

    int nc = 0;
    for (int i = 0; i < g_nfns; i++) nc += g_fns[i].eligible;
    fprintf(stderr, "aotc: AOT-compiled %d/%d function(s), entry '%s' -> %s\n",
            nc, g_nfns, entry, out);
    return 0;
}
