/* fuzz_html.c — libFuzzer entry exercising the in-tree lexbor HTML parser. */
#include <lexbor/html/html.h>
#include <stddef.h>
#include <stdint.h>

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    lxb_html_document_t *doc = lxb_html_document_create();
    if (doc != NULL) {
        lxb_html_document_parse(doc, (const lxb_char_t *)data, size);
        lxb_html_document_destroy(doc);
    }
    return 0;
}
