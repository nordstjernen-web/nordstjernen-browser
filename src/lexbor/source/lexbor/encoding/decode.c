/*
 * Copyright (C) 2019-2024 Alexander Borisov
 *
 * Author: Alexander Borisov <borisov@lexbor.com>
 */

#include "lexbor/encoding/decode.h"


#define LXB_ENCODING_DECODE_UTF_8_BOUNDARY(_lower, _upper, _cont)              \
    {                                                                          \
        ch = *p;                                                               \
                                                                               \
        if (ch < _lower || ch > _upper) {                                      \
            ctx->u.utf_8.lower = 0x00;                                         \
            ctx->u.utf_8.need = 0;                                             \
                                                                               \
            LXB_ENCODING_DECODE_ERROR_BEGIN {                                  \
                *data = p;                                                     \
                ctx->have_error = true;                                        \
            }                                                                  \
            LXB_ENCODING_DECODE_ERROR_END();                                   \
                                                                               \
            _cont;                                                             \
        }                                                                      \
        else {                                                                 \
            p++;                                                               \
            need--;                                                            \
            ctx->codepoint = (ctx->codepoint << 6) | (ch & 0x3F);              \
        }                                                                      \
    }

#define LXB_ENCODING_DECODE_UTF_8_BOUNDARY_SET(first, two, f_lower, s_upper)   \
    do {                                                                       \
        if (ch == first) {                                                     \
            ctx->u.utf_8.lower = f_lower;                                      \
            ctx->u.utf_8.upper = 0xBF;                                         \
        }                                                                      \
        else if (ch == two) {                                                  \
            ctx->u.utf_8.lower = 0x80;                                         \
            ctx->u.utf_8.upper = s_upper;                                      \
        }                                                                      \
    }                                                                          \
    while (0)

#define LXB_ENCODING_DECODE_APPEND_WO_CHECK(ctx, cp)                           \
    do {                                                                       \
        (ctx)->buffer_out[(ctx)->buffer_used++] = (cp);                        \
    }                                                                          \
    while (0)

#define LXB_ENCODING_DECODE_CHECK_OUT(ctx)                                     \
    do {                                                                       \
        if ((ctx)->buffer_used >= (ctx)->buffer_length) {                      \
            return LXB_STATUS_SMALL_BUFFER;                                    \
        }                                                                      \
    }                                                                          \
    while (0)

#define LXB_ENCODING_DECODE_ERROR_BEGIN                                        \
    do {                                                                       \
        if (ctx->replace_to == NULL) {                                         \
            return LXB_STATUS_ERROR;                                           \
        }                                                                      \
                                                                               \
        if ((ctx->buffer_used + ctx->replace_len) > ctx->buffer_length) {      \
            do

#define LXB_ENCODING_DECODE_ERROR_END()                                        \
            while (0);                                                         \
                                                                               \
            return LXB_STATUS_SMALL_BUFFER;                                    \
        }                                                                      \
                                                                               \
        memcpy(&ctx->buffer_out[ctx->buffer_used], ctx->replace_to,            \
               sizeof(lxb_codepoint_t) * ctx->replace_len);                    \
                                                                               \
        ctx->buffer_used += ctx->replace_len;                                  \
    }                                                                          \
    while (0)

#define LXB_ENCODING_DECODE_UTF_8_BOUNDARY_SINGLE(lower, upper)                \
    do {                                                                       \
        ch = **data;                                                           \
                                                                               \
        if (ch < lower || ch > upper) {                                        \
            goto failed;                                                       \
        }                                                                      \
                                                                               \
        (*data)++;                                                             \
        needed--;                                                              \
        ctx->codepoint = (ctx->codepoint << 6) | (ch & 0x3F);                  \
    }                                                                          \
    while (0)

#define LXB_ENCODING_DECODE_UTF_8_BOUNDARY_SET_SINGLE(first, two, f_lower,     \
                                                      s_upper)                 \
    do {                                                                       \
        if (ch == first) {                                                     \
            ctx->u.utf_8.lower = f_lower;                                      \
            ctx->u.utf_8.upper = 0xBF;                                         \
        }                                                                      \
        else if (ch == two) {                                                  \
            ctx->u.utf_8.lower = 0x80;                                         \
            ctx->u.utf_8.upper = s_upper;                                      \
        }                                                                      \
    }                                                                          \
    while (0)


lxb_status_t
lxb_encoding_decode_default(lxb_encoding_decode_t *ctx,
                            const lxb_char_t **data, const lxb_char_t *end)
{
    return lxb_encoding_decode_utf_8(ctx, data, end);
}

lxb_status_t
lxb_encoding_decode_auto(lxb_encoding_decode_t *ctx,
                         const lxb_char_t **data, const lxb_char_t *end)
{
    *data = end;
    return LXB_STATUS_ERROR;
}

lxb_status_t
lxb_encoding_decode_undefined(lxb_encoding_decode_t *ctx,
                              const lxb_char_t **data, const lxb_char_t *end)
{
    *data = end;
    return LXB_STATUS_ERROR;
}

lxb_status_t
lxb_encoding_decode_utf_8(lxb_encoding_decode_t *ctx,
                          const lxb_char_t **data, const lxb_char_t *end)
{
    unsigned need;
    lxb_char_t ch;
    const lxb_char_t *p = *data;

    ctx->status = LXB_STATUS_OK;

    if (ctx->have_error) {
        ctx->have_error = false;

        LXB_ENCODING_DECODE_ERROR_BEGIN {
            ctx->have_error = true;
        }
        LXB_ENCODING_DECODE_ERROR_END();
    }

    if (ctx->u.utf_8.need != 0) {
        if (p >= end) {
            ctx->status = LXB_STATUS_CONTINUE;

            return LXB_STATUS_CONTINUE;
        }

        LXB_ENCODING_DECODE_CHECK_OUT(ctx);

        need = ctx->u.utf_8.need;
        ctx->u.utf_8.need = 0;

        if (ctx->u.utf_8.lower != 0x00) {
            LXB_ENCODING_DECODE_UTF_8_BOUNDARY(ctx->u.utf_8.lower,
                                               ctx->u.utf_8.upper, goto begin);
            ctx->u.utf_8.lower = 0x00;
        }

        goto decode;
    }

begin:

    while (p < end) {
        if (ctx->buffer_used >= ctx->buffer_length) {
            *data = p;

            return LXB_STATUS_SMALL_BUFFER;
        }

        ch = *p++;

        if (ch < 0x80) {
            LXB_ENCODING_DECODE_APPEND_WO_CHECK(ctx, ch);
            continue;
        }
        else if (ch <= 0xDF) {
            if (ch < 0xC2) {
                LXB_ENCODING_DECODE_ERROR_BEGIN {
                    *data = p - 1;
                }
                LXB_ENCODING_DECODE_ERROR_END();

                continue;
            }

            need = 1;
            ctx->codepoint = ch & 0x1F;
        }
        else if (ch < 0xF0) {
            need = 2;
            ctx->codepoint = ch & 0x0F;

            if (p == end) {
                LXB_ENCODING_DECODE_UTF_8_BOUNDARY_SET(0xE0, 0xED, 0xA0, 0x9F);

                *data = p;

                ctx->u.utf_8.need = need;
                ctx->status = LXB_STATUS_CONTINUE;

                return LXB_STATUS_CONTINUE;
            }

            if (ch == 0xE0) {
                LXB_ENCODING_DECODE_UTF_8_BOUNDARY(0xA0, 0xBF, continue);
            }
            else if (ch == 0xED) {
                LXB_ENCODING_DECODE_UTF_8_BOUNDARY(0x80, 0x9F, continue);
            }
        }
        else if (ch < 0xF5) {
            need = 3;
            ctx->codepoint = ch & 0x07;

            if (p == end) {
                LXB_ENCODING_DECODE_UTF_8_BOUNDARY_SET(0xF0, 0xF4, 0x90, 0x8F);

                *data = p;

                ctx->u.utf_8.need = need;
                ctx->status = LXB_STATUS_CONTINUE;

                return LXB_STATUS_CONTINUE;
            }

            if (ch == 0xF0) {
                LXB_ENCODING_DECODE_UTF_8_BOUNDARY(0x90, 0xBF, continue);
            }
            else if (ch == 0xF4) {
                LXB_ENCODING_DECODE_UTF_8_BOUNDARY(0x80, 0x8F, continue);
            }
        }
        else {
            LXB_ENCODING_DECODE_ERROR_BEGIN {
                *data = p - 1;
            }
            LXB_ENCODING_DECODE_ERROR_END();

            continue;
        }

    decode:

        do {
            if (p >= end) {
                *data = p;

                ctx->u.utf_8.need = need;
                ctx->status = LXB_STATUS_CONTINUE;

                return LXB_STATUS_CONTINUE;
            }

            ch = *p++;

            if (ch < 0x80 || ch > 0xBF) {
                p--;

                ctx->u.utf_8.need = 0;

                LXB_ENCODING_DECODE_ERROR_BEGIN {
                    *data = p;
                    ctx->have_error = true;
                }
                LXB_ENCODING_DECODE_ERROR_END();

                break;
            }

            ctx->codepoint = (ctx->codepoint << 6) | (ch & 0x3F);

            if (--need == 0) {
                LXB_ENCODING_DECODE_APPEND_WO_CHECK(ctx, ctx->codepoint);

                break;
            }
        }
        while (true);
    }

    *data = p;

    return LXB_STATUS_OK;
}

lxb_codepoint_t
lxb_encoding_decode_default_single(lxb_encoding_decode_t *ctx,
                                 const lxb_char_t **data, const lxb_char_t *end)
{
    return lxb_encoding_decode_utf_8_single(ctx, data, end);
}

lxb_codepoint_t
lxb_encoding_decode_auto_single(lxb_encoding_decode_t *ctx,
                                const lxb_char_t **data, const lxb_char_t *end)
{
    return LXB_ENCODING_DECODE_ERROR;
}

lxb_codepoint_t
lxb_encoding_decode_undefined_single(lxb_encoding_decode_t *ctx,
                                 const lxb_char_t **data, const lxb_char_t *end)
{
    return LXB_ENCODING_DECODE_ERROR;
}

lxb_codepoint_t
lxb_encoding_decode_utf_8_single(lxb_encoding_decode_t *ctx,
                                 const lxb_char_t **data, const lxb_char_t *end)
{
    unsigned needed;
    lxb_char_t ch;
    const lxb_char_t *p;

    if (ctx->u.utf_8.need != 0) {
        needed = ctx->u.utf_8.need;
        ctx->u.utf_8.need = 0;

        if (ctx->u.utf_8.lower != 0x00) {
            LXB_ENCODING_DECODE_UTF_8_BOUNDARY_SINGLE(ctx->u.utf_8.lower,
                                                      ctx->u.utf_8.upper);
            ctx->u.utf_8.lower = 0x00;
        }

        goto decode;
    }

    ch = *(*data)++;

    if (ch < 0x80) {
        return ch;
    }
    else if (ch <= 0xDF) {
        if (ch < 0xC2) {
            return LXB_ENCODING_DECODE_ERROR;
        }

        needed = 1;
        ctx->codepoint = ch & 0x1F;
    }
    else if (ch < 0xF0) {
        needed = 2;
        ctx->codepoint = ch & 0x0F;

        if (*data == end) {
            LXB_ENCODING_DECODE_UTF_8_BOUNDARY_SET_SINGLE(0xE0, 0xED,
                                                          0xA0, 0x9F);
            goto next;
        }

        if (ch == 0xE0) {
            LXB_ENCODING_DECODE_UTF_8_BOUNDARY_SINGLE(0xA0, 0xBF);
        }
        else if (ch == 0xED) {
            LXB_ENCODING_DECODE_UTF_8_BOUNDARY_SINGLE(0x80, 0x9F);
        }
    }
    else if (ch < 0xF5) {
        needed = 3;
        ctx->codepoint = ch & 0x07;

        if (*data == end) {
            LXB_ENCODING_DECODE_UTF_8_BOUNDARY_SET_SINGLE(0xF0, 0xF4,
                                                          0x90, 0x8F);

            goto next;
        }

        if (ch == 0xF0) {
            LXB_ENCODING_DECODE_UTF_8_BOUNDARY_SINGLE(0x90, 0xBF);
        }
        else if (ch == 0xF4) {
            LXB_ENCODING_DECODE_UTF_8_BOUNDARY_SINGLE(0x80, 0x8F);
        }
    }
    else {
        return LXB_ENCODING_DECODE_ERROR;
    }

decode:

    for (p = *data; p < end; p++) {
        ch = *p;

        if (ch < 0x80 || ch > 0xBF) {
            *data = p;

            goto failed;
        }

        ctx->codepoint = (ctx->codepoint << 6) | (ch & 0x3F);

        if (--needed == 0) {
            *data = p + 1;

            return ctx->codepoint;
        }
    }

    *data = p;

next:

    ctx->u.utf_8.need = needed;

    return LXB_ENCODING_DECODE_CONTINUE;

failed:

    ctx->u.utf_8.lower = 0x00;
    ctx->u.utf_8.need = 0;

    return LXB_ENCODING_DECODE_ERROR;
}

lxb_codepoint_t
lxb_encoding_decode_valid_utf_8_single(const lxb_char_t **data,
                                       const lxb_char_t *end)
{
    lxb_codepoint_t cp;
    const lxb_char_t *p = *data;

    if (*p < 0x80){
        /* 0xxxxxxx */

        if (end - p < 1) {
            *data = end;
            return LXB_ENCODING_DECODE_ERROR;
        }

        cp = (lxb_codepoint_t) *p;

        (*data) += 1;
    }
    else if ((*p & 0xe0) == 0xc0) {
        /* 110xxxxx 10xxxxxx */

        if (end - p < 2) {
            *data = end;
            return LXB_ENCODING_DECODE_ERROR;
        }

        cp  = (p[0] ^ (0xC0 & p[0])) << 6;
        cp |= (p[1] ^ (0x80 & p[1]));

        (*data) += 2;
    }
    else if ((*p & 0xf0) == 0xe0) {
        /* 1110xxxx 10xxxxxx 10xxxxxx */

        if (end - p < 3) {
            *data = end;
            return LXB_ENCODING_DECODE_ERROR;
        }

        cp  = (p[0] ^ (0xE0 & p[0])) << 12;
        cp |= (p[1] ^ (0x80 & p[1])) << 6;
        cp |= (p[2] ^ (0x80 & p[2]));

        (*data) += 3;
    }
    else if ((*p & 0xf8) == 0xf0) {
        /* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */

        if (end - p < 4) {
            *data = end;
            return LXB_ENCODING_DECODE_ERROR;
        }

        cp  = (p[0] ^ (0xF0 & p[0])) << 18;
        cp |= (p[1] ^ (0x80 & p[1])) << 12;
        cp |= (p[2] ^ (0x80 & p[2])) << 6;
        cp |= (p[3] ^ (0x80 & p[3]));

        (*data) += 4;
    }
    else {
        (*data)++;

        return LXB_ENCODING_DECODE_ERROR;
    }

    return cp;
}

lxb_codepoint_t
lxb_encoding_decode_valid_utf_8_single_reverse(const lxb_char_t **end,
                                               const lxb_char_t *begin)
{
    lxb_codepoint_t cp;
    const lxb_char_t *p = *end;

    while (p > begin) {
        p -= 1;

        if (*p < 0x80){
            cp = (lxb_codepoint_t) *p;

            (*end) = p;
            return cp;
        }
        else if ((*p & 0xe0) == 0xc0) {
            /* 110xxxxx 10xxxxxx */

            if (*end - p < 2) {
                *end = p;
                return LXB_ENCODING_DECODE_ERROR;
            }

            cp  = (p[0] ^ (0xC0 & p[0])) << 6;
            cp |= (p[1] ^ (0x80 & p[1]));

            (*end) = p;
            return cp;
        }
        else if ((*p & 0xf0) == 0xe0) {
            /* 1110xxxx 10xxxxxx 10xxxxxx */

            if (*end - p < 3) {
                *end = p;
                return LXB_ENCODING_DECODE_ERROR;
            }

            cp  = (p[0] ^ (0xE0 & p[0])) << 12;
            cp |= (p[1] ^ (0x80 & p[1])) << 6;
            cp |= (p[2] ^ (0x80 & p[2]));

            (*end) = p;
            return cp;
        }
        else if ((*p & 0xf8) == 0xf0) {
            /* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */

            if (*end - p < 4) {
                *end = p;
                return LXB_ENCODING_DECODE_ERROR;
            }

            cp  = (p[0] ^ (0xF0 & p[0])) << 18;
            cp |= (p[1] ^ (0x80 & p[1])) << 12;
            cp |= (p[2] ^ (0x80 & p[2])) << 6;
            cp |= (p[3] ^ (0x80 & p[3]));

            (*end) = p;
            return cp;
        }
        else if (*end - p >= 4) {
            break;
        }
    }

    *end = p;

    return LXB_ENCODING_DECODE_ERROR;
}

uint8_t
lxb_encoding_decode_utf_8_length(lxb_char_t data)
{

    if (data < 0x80){
        return 1;
    }
    else if ((data & 0xe0) == 0xc0) {
        return 2;
    }
    else if ((data & 0xf0) == 0xe0) {
        return 3;
    }
    else if ((data & 0xf8) == 0xf0) {
        return 4;
    }

    return 0;
}
