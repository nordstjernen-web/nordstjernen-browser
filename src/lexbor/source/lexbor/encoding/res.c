/*
 * Copyright (C) 2019 Alexander Borisov
 *
 * Author: Alexander Borisov <borisov@lexbor.com>
 */

#include "lexbor/encoding/base.h"
#include "lexbor/encoding/encode.h"
#include "lexbor/encoding/decode.h"


LXB_API const lxb_encoding_data_t lxb_encoding_res_map[LXB_ENCODING_LAST_ENTRY] =
{
    {LXB_ENCODING_DEFAULT, lxb_encoding_encode_default, lxb_encoding_decode_default,
     lxb_encoding_encode_default_single, lxb_encoding_decode_default_single, (lxb_char_t *) "DEFAULT"},
    {LXB_ENCODING_AUTO, lxb_encoding_encode_auto, lxb_encoding_decode_auto,
     lxb_encoding_encode_auto_single, lxb_encoding_decode_auto_single, (lxb_char_t *) "AUTO"},
    {LXB_ENCODING_UNDEFINED, lxb_encoding_encode_undefined, lxb_encoding_decode_undefined,
     lxb_encoding_encode_undefined_single, lxb_encoding_decode_undefined_single, (lxb_char_t *) "UNDEFINED"},
    {LXB_ENCODING_UTF_8, lxb_encoding_encode_utf_8, lxb_encoding_decode_utf_8,
     lxb_encoding_encode_utf_8_single, lxb_encoding_decode_utf_8_single, (lxb_char_t *) "UTF-8"}
};
