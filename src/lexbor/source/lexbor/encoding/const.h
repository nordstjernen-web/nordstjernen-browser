/*
 * Copyright (C) 2019 Alexander Borisov
 *
 * Author: Alexander Borisov <lex.borisov@gmail.com>
 */

#ifndef LEXBOR_ENCODING_CONST_H
#define LEXBOR_ENCODING_CONST_H

typedef enum {
    LXB_ENCODING_DEFAULT    = 0x00,
    LXB_ENCODING_AUTO       = 0x01,
    LXB_ENCODING_UNDEFINED  = 0x02,
    LXB_ENCODING_UTF_8      = 0x03,
    LXB_ENCODING_LAST_ENTRY = 0x04
}
lxb_encoding_t;

#endif /* LEXBOR_ENCODING_CONST_H */
