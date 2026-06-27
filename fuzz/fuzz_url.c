/* fuzz_url.c — libFuzzer entry exercising the in-tree lexbor WHATWG URL parser. */
#include <lexbor/url/url.h>
#include <stddef.h>
#include <stdint.h>

static lxb_url_parser_t *g_parser;

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0)
        return 0;
    if (g_parser == NULL) {
        g_parser = lxb_url_parser_create();
        if (g_parser == NULL)
            return 0;
        if (lxb_url_parser_init(g_parser, NULL) != LXB_STATUS_OK) {
            lxb_url_parser_destroy(g_parser, true);
            g_parser = NULL;
            return 0;
        }
    }
    lxb_url_t *u = lxb_url_parse(g_parser, NULL, (const lxb_char_t *)data, size);
    if (u != NULL)
        lxb_url_destroy(u);
    lxb_url_parser_clean(g_parser);
    return 0;
}
