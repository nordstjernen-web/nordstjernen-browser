/*
 * Copyright (C) 2019 Alexander Borisov
 *
 * Author: Alexander Borisov <borisov@lexbor.com>
 */

#ifndef LEXBOR_ENCODING_DECODE_H
#define LEXBOR_ENCODING_DECODE_H

#ifdef __cplusplus
extern "C" {
#endif


#include "lexbor/encoding/base.h"


LXB_API lxb_status_t
lxb_encoding_decode_default(lxb_encoding_decode_t *ctx,
                            const lxb_char_t **data, const lxb_char_t *end);

LXB_API lxb_status_t
lxb_encoding_decode_auto(lxb_encoding_decode_t *ctx,
                         const lxb_char_t **data, const lxb_char_t *end);

LXB_API lxb_status_t
lxb_encoding_decode_undefined(lxb_encoding_decode_t *ctx,
                              const lxb_char_t **data, const lxb_char_t *end);

LXB_API lxb_status_t
lxb_encoding_decode_utf_8(lxb_encoding_decode_t *ctx,
                          const lxb_char_t **data, const lxb_char_t *end);

/*
 * Single
 */
LXB_API lxb_codepoint_t
lxb_encoding_decode_default_single(lxb_encoding_decode_t *ctx,
                                   const lxb_char_t **data, const lxb_char_t *end);

LXB_API lxb_codepoint_t
lxb_encoding_decode_auto_single(lxb_encoding_decode_t *ctx,
                                const lxb_char_t **data, const lxb_char_t *end);

LXB_API lxb_codepoint_t
lxb_encoding_decode_undefined_single(lxb_encoding_decode_t *ctx,
                                     const lxb_char_t **data, const lxb_char_t *end);

LXB_API lxb_codepoint_t
lxb_encoding_decode_utf_8_single(lxb_encoding_decode_t *ctx,
                                 const lxb_char_t **data, const lxb_char_t *end);

LXB_API lxb_codepoint_t
lxb_encoding_decode_valid_utf_8_single(const lxb_char_t **data,
                                       const lxb_char_t *end);

LXB_API lxb_codepoint_t
lxb_encoding_decode_valid_utf_8_single_reverse(const lxb_char_t **end,
                                               const lxb_char_t *begin);

LXB_API uint8_t
lxb_encoding_decode_utf_8_length(lxb_char_t data);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LEXBOR_ENCODING_DECODE_H */
