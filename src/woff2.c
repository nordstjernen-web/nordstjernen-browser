/* Nordstjernen — WOFF2 web font decoder: Brotli stream, glyf/loca and hmtx transforms. */

#include "woff2.h"

#include <brotli/decode.h>
#include <stdlib.h>
#include <string.h>

#define W2_TAG(a, b, c, d) \
    (((guint32)(a) << 24) | ((guint32)(b) << 16) | ((guint32)(c) << 8) | (guint32)(d))
#define W2_MAX_OUTPUT (64u * 1024u * 1024u)
#define W2_MAX_TABLES 1024

typedef struct {
    const guint8 *p;
    gsize         len;
    gsize         pos;
    gboolean      bad;
} w2_reader;

typedef struct {
    guint32  tag;
    gboolean transformed;
    guint32  orig_len;
    guint32  stream_len;
    gsize    stream_off;
    guint8  *data;
    gsize    data_len;
} w2_table;

typedef struct {
    gint32   x, y;
    gboolean on_curve;
} w2_point;

static const char *const w2_known_tags[63] = {
    "cmap", "head", "hhea", "hmtx", "maxp", "name", "OS/2", "post",
    "cvt ", "fpgm", "glyf", "loca", "prep", "CFF ", "VORG", "EBDT",
    "EBLC", "gasp", "hdmx", "kern", "LTSH", "PCLT", "VDMX", "vhea",
    "vmtx", "BASE", "GDEF", "GPOS", "GSUB", "EBSC", "JSTF", "MATH",
    "CBDT", "CBLC", "COLR", "CPAL", "SVG ", "sbix", "acnt", "avar",
    "bdat", "bloc", "bsln", "cvar", "fdsc", "feat", "fmtx", "fvar",
    "gvar", "hsty", "just", "lcar", "mort", "morx", "opbd", "prop",
    "trak", "Zapf", "Silf", "Glat", "Gloc", "Feat", "Sill",
};

static const guint8 *
rd_bytes(w2_reader *r, gsize n)
{
    if (r->bad || n > r->len - r->pos) {
        r->bad = TRUE;
        return NULL;
    }
    const guint8 *at = r->p + r->pos;
    r->pos += n;
    return at;
}

static guint8
rd_u8(w2_reader *r)
{
    const guint8 *b = rd_bytes(r, 1);
    return b ? b[0] : 0;
}

static guint16
rd_u16(w2_reader *r)
{
    const guint8 *b = rd_bytes(r, 2);
    return b ? (guint16)((b[0] << 8) | b[1]) : 0;
}

static guint32
rd_u32(w2_reader *r)
{
    const guint8 *b = rd_bytes(r, 4);
    return b ? ((guint32)b[0] << 24) | ((guint32)b[1] << 16) |
               ((guint32)b[2] << 8) | b[3]
             : 0;
}

static guint32
rd_base128(w2_reader *r)
{
    guint32 value = 0;
    for (int i = 0; i < 5; i++) {
        guint8 b = rd_u8(r);
        if (r->bad || (i == 0 && b == 0x80) || (value & 0xFE000000u)) {
            r->bad = TRUE;
            return 0;
        }
        value = (value << 7) | (b & 0x7F);
        if (!(b & 0x80))
            return value;
    }
    r->bad = TRUE;
    return 0;
}

static guint16
rd_255u16(w2_reader *r)
{
    guint8 code = rd_u8(r);
    if (code == 253)
        return rd_u16(r);
    if (code == 255)
        return (guint16)(rd_u8(r) + 253);
    if (code == 254)
        return (guint16)(rd_u8(r) + 506);
    return code;
}

static w2_reader
sub_reader(w2_reader *r, guint32 n)
{
    w2_reader s = { NULL, 0, 0, TRUE };
    const guint8 *at = rd_bytes(r, n);
    if (at) {
        s.p = at;
        s.len = n;
        s.bad = FALSE;
    }
    return s;
}

static void
put_u16(GByteArray *out, guint16 v)
{
    guint8 b[2] = { (guint8)(v >> 8), (guint8)v };
    g_byte_array_append(out, b, 2);
}

static void
put_u32(GByteArray *out, guint32 v)
{
    guint8 b[4] = { (guint8)(v >> 24), (guint8)(v >> 16), (guint8)(v >> 8),
                    (guint8)v };
    g_byte_array_append(out, b, 4);
}

static void
pad4(GByteArray *out)
{
    static const guint8 zero[3] = { 0, 0, 0 };
    if (out->len & 3)
        g_byte_array_append(out, zero, 4 - (out->len & 3));
}

static guint16
be16_at(const guint8 *p)
{
    return (guint16)((p[0] << 8) | p[1]);
}

static gint32
with_sign(int flag, gint32 base)
{
    return (flag & 1) ? base : -base;
}

static gboolean
triplet_decode(w2_reader *glyphs, const guint8 *flags, guint32 n_points,
               w2_point *out)
{
    gint32 x = 0, y = 0;
    for (guint32 i = 0; i < n_points; i++) {
        int flag = flags[i] & 0x7F;
        gsize n = flag < 84 ? 1 : flag < 120 ? 2 : flag < 124 ? 3 : 4;
        const guint8 *b = rd_bytes(glyphs, n);
        if (!b)
            return FALSE;
        gint32 dx, dy;
        if (flag < 10) {
            dx = 0;
            dy = with_sign(flag, ((flag & 14) << 7) + b[0]);
        } else if (flag < 20) {
            dx = with_sign(flag, (((flag - 10) & 14) << 7) + b[0]);
            dy = 0;
        } else if (flag < 84) {
            int b0 = flag - 20;
            dx = with_sign(flag, 1 + (b0 & 0x30) + (b[0] >> 4));
            dy = with_sign(flag >> 1, 1 + ((b0 & 0x0C) << 2) + (b[0] & 0x0F));
        } else if (flag < 120) {
            int b0 = flag - 84;
            dx = with_sign(flag, 1 + ((b0 / 12) << 8) + b[0]);
            dy = with_sign(flag >> 1, 1 + (((b0 % 12) >> 2) << 8) + b[1]);
        } else if (flag < 124) {
            dx = with_sign(flag, (b[0] << 4) + (b[1] >> 4));
            dy = with_sign(flag >> 1, ((b[1] & 0x0F) << 8) + b[2]);
        } else {
            dx = with_sign(flag, (b[0] << 8) + b[1]);
            dy = with_sign(flag >> 1, (b[2] << 8) + b[3]);
        }
        x += dx;
        y += dy;
        out[i].x = x;
        out[i].y = y;
        out[i].on_curve = !(flags[i] & 0x80);
    }
    return TRUE;
}

typedef struct {
    w2_reader n_contours;
    w2_reader n_points;
    w2_reader flags;
    w2_reader glyphs;
    w2_reader composite;
    w2_reader bbox;
    w2_reader instructions;
    const guint8 *bbox_bitmap;
    const guint8 *overlap_bitmap;
} w2_glyf_streams;

static gboolean
copy_instructions(w2_glyf_streams *s, GByteArray *out)
{
    guint16 n = rd_255u16(&s->glyphs);
    const guint8 *code = rd_bytes(&s->instructions, n);
    if (s->glyphs.bad || !code)
        return FALSE;
    put_u16(out, n);
    g_byte_array_append(out, code, n);
    return TRUE;
}

static gboolean
copy_bbox(w2_glyf_streams *s, GByteArray *out, gint16 *x_min)
{
    const guint8 *b = rd_bytes(&s->bbox, 8);
    if (!b)
        return FALSE;
    *x_min = (gint16)be16_at(b);
    g_byte_array_append(out, b, 8);
    return TRUE;
}

static gboolean
write_composite_glyph(w2_glyf_streams *s, GByteArray *out, gint16 *x_min)
{
    put_u16(out, 0xFFFF);
    if (!copy_bbox(s, out, x_min))
        return FALSE;
    gsize start = s->composite.pos;
    gboolean more = TRUE, instructions = FALSE;
    while (more) {
        guint16 flags = rd_u16(&s->composite);
        gsize args = 2 + ((flags & 0x0001) ? 4 : 2);
        if (flags & 0x0008)
            args += 2;
        else if (flags & 0x0040)
            args += 4;
        else if (flags & 0x0080)
            args += 8;
        if (!rd_bytes(&s->composite, args))
            return FALSE;
        instructions |= (flags & 0x0100) != 0;
        more = (flags & 0x0020) != 0;
    }
    g_byte_array_append(out, s->composite.p + start, s->composite.pos - start);
    return !instructions || copy_instructions(s, out);
}

static gboolean
write_simple_glyph(w2_glyf_streams *s, guint32 glyph, gint16 n_contours,
                   gboolean has_bbox, GByteArray *out, gint16 *x_min)
{
    guint16 *end_points = g_new(guint16, n_contours);
    guint32 n_points = 0;
    for (gint16 c = 0; c < n_contours; c++) {
        n_points += rd_255u16(&s->n_points);
        if (s->n_points.bad || n_points == 0 || n_points > 0xFFFF) {
            g_free(end_points);
            return FALSE;
        }
        end_points[c] = (guint16)(n_points - 1);
    }
    const guint8 *flags = rd_bytes(&s->flags, n_points);
    w2_point *points = flags ? g_new(w2_point, n_points) : NULL;
    if (!points || !triplet_decode(&s->glyphs, flags, n_points, points)) {
        g_free(points);
        g_free(end_points);
        return FALSE;
    }

    gint32 bx0 = points[0].x, by0 = points[0].y, bx1 = bx0, by1 = by0;
    for (guint32 i = 1; i < n_points; i++) {
        bx0 = MIN(bx0, points[i].x);
        by0 = MIN(by0, points[i].y);
        bx1 = MAX(bx1, points[i].x);
        by1 = MAX(by1, points[i].y);
    }
    put_u16(out, (guint16)n_contours);
    gboolean ok = TRUE;
    if (has_bbox) {
        ok = copy_bbox(s, out, x_min);
    } else {
        *x_min = (gint16)bx0;
        put_u16(out, (guint16)bx0);
        put_u16(out, (guint16)by0);
        put_u16(out, (guint16)bx1);
        put_u16(out, (guint16)by1);
    }
    for (gint16 c = 0; ok && c < n_contours; c++)
        put_u16(out, end_points[c]);
    ok = ok && copy_instructions(s, out);

    gboolean overlap = s->overlap_bitmap &&
        (s->overlap_bitmap[glyph >> 3] & (0x80 >> (glyph & 7)));
    for (guint32 i = 0; ok && i < n_points; i++) {
        guint8 f = points[i].on_curve ? 0x01 : 0x00;
        if (i == 0 && overlap)
            f |= 0x40;
        g_byte_array_append(out, &f, 1);
    }
    gint32 prev = 0;
    for (guint32 i = 0; ok && i < n_points; i++) {
        put_u16(out, (guint16)(gint16)(points[i].x - prev));
        prev = points[i].x;
    }
    prev = 0;
    for (guint32 i = 0; ok && i < n_points; i++) {
        put_u16(out, (guint16)(gint16)(points[i].y - prev));
        prev = points[i].y;
    }
    g_free(points);
    g_free(end_points);
    return ok;
}

static gboolean
reconstruct_glyf(const guint8 *data, gsize len, w2_table *glyf, w2_table *loca,
                 gint16 **x_mins, guint16 *num_glyphs_out)
{
    w2_reader r = { data, len, 0, FALSE };
    rd_u16(&r);
    guint16 options = rd_u16(&r);
    guint16 num_glyphs = rd_u16(&r);
    rd_u16(&r);
    guint32 sizes[7];
    for (int i = 0; i < 7; i++)
        sizes[i] = rd_u32(&r);
    if (r.bad)
        return FALSE;

    w2_glyf_streams s;
    s.n_contours = sub_reader(&r, sizes[0]);
    s.n_points = sub_reader(&r, sizes[1]);
    s.flags = sub_reader(&r, sizes[2]);
    s.glyphs = sub_reader(&r, sizes[3]);
    s.composite = sub_reader(&r, sizes[4]);
    s.bbox = sub_reader(&r, sizes[5]);
    s.instructions = sub_reader(&r, sizes[6]);
    gsize bitmap_len = 4u * (((gsize)num_glyphs + 31u) / 32u);
    s.bbox_bitmap = rd_bytes(&s.bbox, bitmap_len);
    s.overlap_bitmap = (options & 1)
        ? rd_bytes(&r, ((gsize)num_glyphs + 7u) / 8u) : NULL;
    if (r.bad || s.bbox.bad)
        return FALSE;

    GByteArray *out = g_byte_array_sized_new((guint)MIN(len * 2, W2_MAX_OUTPUT));
    guint32 *offsets = g_new0(guint32, (gsize)num_glyphs + 1);
    gint16 *mins = g_new0(gint16, MAX(num_glyphs, 1));
    gboolean ok = TRUE;
    for (guint32 g = 0; ok && g < num_glyphs; g++) {
        offsets[g] = out->len;
        gint16 n_contours = (gint16)rd_u16(&s.n_contours);
        gboolean has_bbox = (s.bbox_bitmap[g >> 3] & (0x80 >> (g & 7))) != 0;
        if (s.n_contours.bad)
            ok = FALSE;
        else if (n_contours == 0)
            ok = !has_bbox;
        else if (n_contours == -1)
            ok = has_bbox && write_composite_glyph(&s, out, &mins[g]);
        else if (n_contours > 0)
            ok = write_simple_glyph(&s, g, n_contours, has_bbox, out, &mins[g]);
        else
            ok = FALSE;
        pad4(out);
        ok = ok && out->len <= W2_MAX_OUTPUT;
    }
    if (!ok) {
        g_byte_array_free(out, TRUE);
        g_free(offsets);
        g_free(mins);
        return FALSE;
    }
    offsets[num_glyphs] = out->len;

    GByteArray *loca_out = g_byte_array_sized_new(((guint)num_glyphs + 1) * 4);
    for (guint32 g = 0; g <= num_glyphs; g++)
        put_u32(loca_out, offsets[g]);
    g_free(offsets);

    glyf->data_len = out->len;
    glyf->data = g_byte_array_free(out, FALSE);
    loca->data_len = loca_out->len;
    loca->data = g_byte_array_free(loca_out, FALSE);
    *x_mins = mins;
    *num_glyphs_out = num_glyphs;
    return TRUE;
}

static gboolean
reconstruct_hmtx(const guint8 *data, gsize len, guint16 num_hmetrics,
                 guint16 num_glyphs, const gint16 *x_mins, w2_table *hmtx)
{
    if (num_hmetrics == 0 || num_hmetrics > num_glyphs)
        return FALSE;
    w2_reader r = { data, len, 0, FALSE };
    guint8 flags = rd_u8(&r);
    guint16 *advance = g_new(guint16, num_hmetrics);
    for (guint32 i = 0; i < num_hmetrics; i++)
        advance[i] = rd_u16(&r);
    gint16 *lsb = g_new(gint16, num_glyphs);
    for (guint32 i = 0; i < num_glyphs; i++) {
        gboolean explicit = i < num_hmetrics ? !(flags & 1) : !(flags & 2);
        lsb[i] = explicit ? (gint16)rd_u16(&r) : x_mins[i];
    }
    gboolean ok = !r.bad;
    if (ok) {
        GByteArray *out = g_byte_array_sized_new((guint)num_hmetrics * 2 +
                                                 (guint)num_glyphs * 2);
        for (guint32 i = 0; i < num_glyphs; i++) {
            if (i < num_hmetrics)
                put_u16(out, advance[i]);
            put_u16(out, (guint16)lsb[i]);
        }
        hmtx->data_len = out->len;
        hmtx->data = g_byte_array_free(out, FALSE);
    }
    g_free(advance);
    g_free(lsb);
    return ok;
}

static w2_table *
find_table(w2_table *tables, guint16 n, guint32 tag)
{
    for (guint16 i = 0; i < n; i++)
        if (tables[i].tag == tag)
            return &tables[i];
    return NULL;
}

static gint
table_tag_cmp(gconstpointer a, gconstpointer b)
{
    guint32 ta = (*(w2_table *const *)a)->tag;
    guint32 tb = (*(w2_table *const *)b)->tag;
    return ta < tb ? -1 : ta > tb ? 1 : 0;
}

static guint32
table_checksum(const guint8 *p, gsize len)
{
    guint32 sum = 0;
    for (gsize i = 0; i < len; i += 4) {
        guint32 word = 0;
        for (gsize k = 0; k < 4; k++)
            word = (word << 8) | (i + k < len ? p[i + k] : 0);
        sum += word;
    }
    return sum;
}

static guint8 *
assemble_sfnt(guint32 flavor, w2_table *tables, guint16 n, gsize *out_len)
{
    w2_table **order = g_new(w2_table *, n);
    for (guint16 i = 0; i < n; i++)
        order[i] = &tables[i];
    qsort(order, n, sizeof *order, table_tag_cmp);

    guint16 selector = 0;
    while ((1u << (selector + 1)) <= n)
        selector++;
    guint16 range = (guint16)((1u << selector) * 16);

    GByteArray *out = g_byte_array_new();
    put_u32(out, flavor);
    put_u16(out, n);
    put_u16(out, range);
    put_u16(out, selector);
    put_u16(out, (guint16)(n * 16 - range));
    gsize offset = 12 + (gsize)n * 16;
    for (guint16 i = 0; i < n; i++) {
        put_u32(out, order[i]->tag);
        put_u32(out, table_checksum(order[i]->data, order[i]->data_len));
        put_u32(out, (guint32)offset);
        put_u32(out, (guint32)order[i]->data_len);
        offset += (order[i]->data_len + 3) & ~(gsize)3;
    }
    for (guint16 i = 0; i < n; i++) {
        g_byte_array_append(out, order[i]->data, (guint)order[i]->data_len);
        pad4(out);
    }
    g_free(order);
    *out_len = out->len;
    return g_byte_array_free(out, FALSE);
}

gboolean
ns_woff2_is_woff2(const guint8 *data, gsize len)
{
    return data && len >= 4 && memcmp(data, "wOF2", 4) == 0;
}

static gboolean
read_directory(w2_reader *r, w2_table *tables, guint16 n, gsize *stream_total)
{
    gsize total = 0;
    for (guint16 i = 0; i < n; i++) {
        guint8 flags = rd_u8(r);
        guint8 index = flags & 0x3F;
        guint32 tag = index == 63 ? rd_u32(r)
                                  : W2_TAG(w2_known_tags[index][0],
                                           w2_known_tags[index][1],
                                           w2_known_tags[index][2],
                                           w2_known_tags[index][3]);
        guint8 version = flags >> 6;
        gboolean outline = tag == W2_TAG('g', 'l', 'y', 'f') ||
                           tag == W2_TAG('l', 'o', 'c', 'a');
        tables[i].tag = tag;
        tables[i].orig_len = rd_base128(r);
        if (outline)
            tables[i].transformed = version == 0;
        else if (tag == W2_TAG('h', 'm', 't', 'x'))
            tables[i].transformed = version == 1;
        else
            tables[i].transformed = FALSE;
        if ((outline && version != 0 && version != 3) ||
            (!outline && version != 0 && !tables[i].transformed))
            return FALSE;
        tables[i].stream_len = tables[i].transformed ? rd_base128(r)
                                                     : tables[i].orig_len;
        if (r->bad || tables[i].stream_len > W2_MAX_OUTPUT)
            return FALSE;
        tables[i].stream_off = total;
        total += tables[i].stream_len;
        if (total > W2_MAX_OUTPUT)
            return FALSE;
    }
    *stream_total = total;
    return TRUE;
}

static gboolean
reconstruct_tables(const guint8 *stream, w2_table *tables, guint16 n)
{
    w2_table *glyf = find_table(tables, n, W2_TAG('g', 'l', 'y', 'f'));
    w2_table *loca = find_table(tables, n, W2_TAG('l', 'o', 'c', 'a'));
    w2_table *hmtx = find_table(tables, n, W2_TAG('h', 'm', 't', 'x'));
    w2_table *head = find_table(tables, n, W2_TAG('h', 'e', 'a', 'd'));
    w2_table *hhea = find_table(tables, n, W2_TAG('h', 'h', 'e', 'a'));
    gint16 *x_mins = NULL;
    guint16 num_glyphs = 0;

    if ((glyf && glyf->transformed) != (loca && loca->transformed))
        return FALSE;
    if (glyf && glyf->transformed) {
        if (!loca || loca->stream_len != 0 || !head || head->orig_len < 54 ||
            !reconstruct_glyf(stream + glyf->stream_off, glyf->stream_len,
                              glyf, loca, &x_mins, &num_glyphs))
            return FALSE;
    }
    if (hmtx && hmtx->transformed) {
        gboolean ok = x_mins && hhea && hhea->orig_len >= 36 &&
            reconstruct_hmtx(stream + hmtx->stream_off, hmtx->stream_len,
                             be16_at(stream + hhea->stream_off + 34),
                             num_glyphs, x_mins, hmtx);
        if (!ok) {
            g_free(x_mins);
            return FALSE;
        }
    }
    g_free(x_mins);

    for (guint16 i = 0; i < n; i++) {
        if (tables[i].data)
            continue;
        if (tables[i].transformed)
            return FALSE;
        tables[i].data = g_memdup2(stream + tables[i].stream_off,
                                   tables[i].stream_len);
        tables[i].data_len = tables[i].stream_len;
    }
    if (head && head->data_len >= 54) {
        memset(head->data + 8, 0, 4);
        if (glyf && glyf->transformed) {
            head->data[50] = 0;
            head->data[51] = 1;
        }
    }
    return TRUE;
}

guint8 *
ns_woff2_to_sfnt(const guint8 *data, gsize len, gsize *out_len,
                 gboolean *out_cff)
{
    if (!ns_woff2_is_woff2(data, len) || len < 48)
        return NULL;
    w2_reader r = { data, len, 4, FALSE };
    guint32 flavor = rd_u32(&r);
    rd_u32(&r);
    guint16 num_tables = rd_u16(&r);
    rd_u16(&r);
    rd_u32(&r);
    guint32 compressed_len = rd_u32(&r);
    r.pos = 48;
    if (flavor == W2_TAG('t', 't', 'c', 'f') || num_tables == 0 ||
        num_tables > W2_MAX_TABLES)
        return NULL;

    w2_table *tables = g_new0(w2_table, num_tables);
    gsize stream_total = 0;
    guint8 *stream = NULL;
    guint8 *sfnt = NULL;
    const guint8 *compressed = NULL;
    if (!read_directory(&r, tables, num_tables, &stream_total) ||
        !(compressed = rd_bytes(&r, compressed_len)))
        goto out;

    stream = g_try_malloc(MAX(stream_total, 1));
    size_t decoded = stream_total;
    if (!stream ||
        BrotliDecoderDecompress(compressed_len, compressed, &decoded, stream) !=
            BROTLI_DECODER_RESULT_SUCCESS ||
        decoded != stream_total)
        goto out;

    if (reconstruct_tables(stream, tables, num_tables)) {
        sfnt = assemble_sfnt(flavor, tables, num_tables, out_len);
        if (out_cff)
            *out_cff = flavor == W2_TAG('O', 'T', 'T', 'O');
    }

out:
    for (guint16 i = 0; i < num_tables; i++)
        g_free(tables[i].data);
    g_free(tables);
    g_free(stream);
    return sfnt;
}
