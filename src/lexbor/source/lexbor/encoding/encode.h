/*
 * Copyright (C) 2019 Alexander Borisov
 *
 * Author: Alexander Borisov <borisov@lexbor.com>
 */

#ifndef LEXBOR_ENCODING_ENCODE_H
#define LEXBOR_ENCODING_ENCODE_H

#ifdef __cplusplus
extern "C" {
#endif


#include "lexbor/encoding/base.h"


LXB_API lxb_status_t
lxb_encoding_encode_default(lxb_encoding_encode_t *ctx, const lxb_codepoint_t **cp,
                            const lxb_codepoint_t *end);

LXB_API lxb_status_t
lxb_encoding_encode_auto(lxb_encoding_encode_t *ctx, const lxb_codepoint_t **cp,
                         const lxb_codepoint_t *end);

LXB_API lxb_status_t
lxb_encoding_encode_undefined(lxb_encoding_encode_t *ctx, const lxb_codepoint_t **cp,
                              const lxb_codepoint_t *end);

LXB_API lxb_status_t
lxb_encoding_encode_utf_8(lxb_encoding_encode_t *ctx, const lxb_codepoint_t **cp,
                          const lxb_codepoint_t *end);

/*
 * Single
 */
LXB_API int8_t
lxb_encoding_encode_default_single(lxb_encoding_encode_t *ctx, lxb_char_t **data,
                                   const lxb_char_t *end, lxb_codepoint_t cp);

LXB_API int8_t
lxb_encoding_encode_auto_single(lxb_encoding_encode_t *ctx, lxb_char_t **data,
                                const lxb_char_t *end, lxb_codepoint_t cp);

LXB_API int8_t
lxb_encoding_encode_undefined_single(lxb_encoding_encode_t *ctx, lxb_char_t **data,
                                     const lxb_char_t *end, lxb_codepoint_t cp);

LXB_API int8_t
lxb_encoding_encode_utf_8_single(lxb_encoding_encode_t *ctx, lxb_char_t **data,
                                 const lxb_char_t *end, lxb_codepoint_t cp);

LXB_API int8_t
lxb_encoding_encode_utf_8_length(lxb_codepoint_t cp);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LEXBOR_ENCODING_ENCODE_H */
