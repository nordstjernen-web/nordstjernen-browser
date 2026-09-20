/*
 * Copyright (C) 2019-2024 Alexander Borisov
 *
 * Author: Alexander Borisov <borisov@lexbor.com>
 */

#include "lexbor/encoding/encode.h"


#define LXB_ENCODING_ENCODE_ERROR(ctx)                                         \
    do {                                                                       \
        if (ctx->replace_to == NULL) {                                         \
            return LXB_STATUS_ERROR;                                           \
        }                                                                      \
                                                                               \
        if ((ctx->buffer_used + ctx->replace_len) > ctx->buffer_length) {      \
            return LXB_STATUS_SMALL_BUFFER;                                    \
        }                                                                      \
                                                                               \
        memcpy(&ctx->buffer_out[ctx->buffer_used], ctx->replace_to,            \
               ctx->replace_len);                                              \
                                                                               \
        ctx->buffer_used += ctx->replace_len;                                  \
    }                                                                          \
    while (0)


lxb_status_t
lxb_encoding_encode_default(lxb_encoding_encode_t *ctx, const lxb_codepoint_t **cps,
                            const lxb_codepoint_t *end)
{
    return lxb_encoding_encode_utf_8(ctx, cps, end);
}

lxb_status_t
lxb_encoding_encode_auto(lxb_encoding_encode_t *ctx, const lxb_codepoint_t **cps,
                         const lxb_codepoint_t *end)
{
    *cps = end;
    return LXB_STATUS_ERROR;
}

lxb_status_t
lxb_encoding_encode_undefined(lxb_encoding_encode_t *ctx, const lxb_codepoint_t **cps,
                              const lxb_codepoint_t *end)
{
    *cps = end;
    return LXB_STATUS_ERROR;
}

lxb_status_t
lxb_encoding_encode_utf_8(lxb_encoding_encode_t *ctx, const lxb_codepoint_t **cps,
                          const lxb_codepoint_t *end)
{
    lxb_codepoint_t cp;
    const lxb_codepoint_t *p = *cps;

    for (; p < end; p++) {
        cp = *p;

        if (cp < 0x80) {
            if ((ctx->buffer_used + 1) > ctx->buffer_length) {
                *cps = p;

                return LXB_STATUS_SMALL_BUFFER;
            }

            /* 0xxxxxxx */
            ctx->buffer_out[ ctx->buffer_used++ ] = (lxb_char_t) cp;
        }
        else if (cp < 0x800) {
            if ((ctx->buffer_used + 2) > ctx->buffer_length) {
                *cps = p;

                return LXB_STATUS_SMALL_BUFFER;
            }

            /* 110xxxxx 10xxxxxx */
            ctx->buffer_out[ ctx->buffer_used++ ] = (lxb_char_t) (0xC0 | (cp >> 6  ));
            ctx->buffer_out[ ctx->buffer_used++ ] = (lxb_char_t) (0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000) {
            if ((ctx->buffer_used + 3) > ctx->buffer_length) {
                *cps = p;

                return LXB_STATUS_SMALL_BUFFER;
            }

            /* 1110xxxx 10xxxxxx 10xxxxxx */
            ctx->buffer_out[ ctx->buffer_used++ ] = (lxb_char_t) (0xE0 | ((cp >> 12)));
            ctx->buffer_out[ ctx->buffer_used++ ] = (lxb_char_t) (0x80 | ((cp >> 6 ) & 0x3F));
            ctx->buffer_out[ ctx->buffer_used++ ] = (lxb_char_t) (0x80 | ( cp        & 0x3F));
        }
        else if (cp < 0x110000) {
            if ((ctx->buffer_used + 4) > ctx->buffer_length) {
                *cps = p;

                return LXB_STATUS_SMALL_BUFFER;
            }

            /* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
            ctx->buffer_out[ ctx->buffer_used++ ] = (lxb_char_t) (0xF0 | ( cp >> 18));
            ctx->buffer_out[ ctx->buffer_used++ ] = (lxb_char_t) (0x80 | ((cp >> 12) & 0x3F));
            ctx->buffer_out[ ctx->buffer_used++ ] = (lxb_char_t) (0x80 | ((cp >> 6 ) & 0x3F));
            ctx->buffer_out[ ctx->buffer_used++ ] = (lxb_char_t) (0x80 | ( cp        & 0x3F));
        }
        else {
            *cps = p;
            LXB_ENCODING_ENCODE_ERROR(ctx);
        }
    }

    *cps = p;

    return LXB_STATUS_OK;
}

int8_t
lxb_encoding_encode_default_single(lxb_encoding_encode_t *ctx, lxb_char_t **data,
                                   const lxb_char_t *end, lxb_codepoint_t cp)
{
    return lxb_encoding_encode_utf_8_single(ctx, data, end, cp);
}

int8_t
lxb_encoding_encode_auto_single(lxb_encoding_encode_t *ctx, lxb_char_t **data,
                                const lxb_char_t *end, lxb_codepoint_t cp)
{
    return LXB_ENCODING_ENCODE_ERROR;
}

int8_t
lxb_encoding_encode_undefined_single(lxb_encoding_encode_t *ctx, lxb_char_t **data,
                                     const lxb_char_t *end, lxb_codepoint_t cp)
{
    return LXB_ENCODING_ENCODE_ERROR;
}

int8_t
lxb_encoding_encode_utf_8_single(lxb_encoding_encode_t *ctx, lxb_char_t **data,
                                 const lxb_char_t *end, lxb_codepoint_t cp)
{
    if (cp < 0x80) {
        /* 0xxxxxxx */
        *(*data)++ = (lxb_char_t) cp;

        return 1;
    }

    if (cp < 0x800) {
        if ((*data + 2) > end) {
            return LXB_ENCODING_ENCODE_SMALL_BUFFER;
        }

        /* 110xxxxx 10xxxxxx */
        *(*data)++ = (lxb_char_t) (0xC0 | (cp >> 6  ));
        *(*data)++ = (lxb_char_t) (0x80 | (cp & 0x3F));

        return 2;
    }

    if (cp < 0x10000) {
        if ((*data + 3) > end) {
            return LXB_ENCODING_ENCODE_SMALL_BUFFER;
        }

        /* 1110xxxx 10xxxxxx 10xxxxxx */
        *(*data)++ = (lxb_char_t) (0xE0 | ((cp >> 12)));
        *(*data)++ = (lxb_char_t) (0x80 | ((cp >> 6 ) & 0x3F));
        *(*data)++ = (lxb_char_t) (0x80 | ( cp        & 0x3F));

        return 3;
    }

    if (cp < 0x110000) {
        if ((*data + 4) > end) {
            return LXB_ENCODING_ENCODE_SMALL_BUFFER;
        }

        /* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
        *(*data)++ = (lxb_char_t) (0xF0 | ( cp >> 18));
        *(*data)++ = (lxb_char_t) (0x80 | ((cp >> 12) & 0x3F));
        *(*data)++ = (lxb_char_t) (0x80 | ((cp >> 6 ) & 0x3F));
        *(*data)++ = (lxb_char_t) (0x80 | ( cp        & 0x3F));

        return 4;
    }

    return LXB_ENCODING_ENCODE_ERROR;
}

int8_t
lxb_encoding_encode_utf_8_length(lxb_codepoint_t cp)
{
    if (cp < 0x80) {
        return 1;
    }
    else if (cp < 0x800) {
        return 2;
    }
    else if (cp < 0x10000) {
        return 3;
    }
    else if (cp < 0x110000) {
        return 4;
    }

    return 0;
}
