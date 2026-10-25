#include "gpos.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Minimal GPOS horizontal pair-kerning reader.  stb_truetype ignores GPOS,
 * but the design fonts carry their Latin kerning only in GPOS; without it
 * centred/right-aligned lines end up a few pixels wider than the mock.
 *
 * Only the single PairPos lookup with the largest class matrix is used (the
 * horizontal kern lookup in these fonts).  ValueRecords other than XAdvance
 * are skipped. */
typedef struct {
    int format; /* 1 or 2 */
    const unsigned char *gpos;
    int value1_size;
    int value2_size;
    int xadv_offset; /* -1 when ValueFormat1 has no XAdvance */
    int num_glyphs;
    uint16_t *coverage;
    int coverage_count;
    /* format 1 */
    uint32_t *pair_set_offsets;
    int pair_set_count;
    /* format 2 */
    uint16_t *class1;
    uint16_t *class2;
    int class1_count;
    int class2_count;
    int16_t *matrix;
} KernSub;

struct KernTable {
    const unsigned char *gpos;
    KernSub *subs;
    int count;
};

static uint16_t rd16(const unsigned char *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t rd32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int value_size(uint16_t fields)
{
    int n = 0;
    int i;
    for (i = 0; i < 8; i++) {
        if ((fields & (1u << i)) != 0) {
            n += 2;
        }
    }
    return n;
}

static int xadvance_offset(uint16_t fields)
{
    int n = 0;
    if ((fields & 0x0004u) == 0) {
        return -1;
    }
    if ((fields & 0x0001u) != 0) {
        n += 2;
    }
    if ((fields & 0x0002u) != 0) {
        n += 2;
    }
    return n;
}

static uint16_t *parse_coverage(const unsigned char *gpos, uint32_t offset,
                                int *out_count)
{
    const unsigned char *p = gpos + offset;
    uint16_t format = rd16(p);
    uint16_t *glyphs = NULL;
    int count = 0;
    int i;
    if (format == 1) {
        count = rd16(p + 2);
        glyphs = (uint16_t *)malloc((size_t)count * sizeof(uint16_t));
        if (glyphs == NULL) {
            return NULL;
        }
        for (i = 0; i < count; i++) {
            glyphs[i] = rd16(p + 4 + 2 * i);
        }
    } else if (format == 2) {
        int ranges = rd16(p + 2);
        count = 0;
        for (i = 0; i < ranges; i++) {
            uint16_t start = rd16(p + 4 + 6 * i);
            uint16_t end = rd16(p + 6 + 6 * i);
            count += (int)end - (int)start + 1;
        }
        glyphs = (uint16_t *)malloc((size_t)count * sizeof(uint16_t));
        if (glyphs == NULL) {
            return NULL;
        }
        count = 0;
        for (i = 0; i < ranges; i++) {
            uint16_t start = rd16(p + 4 + 6 * i);
            uint16_t end = rd16(p + 6 + 6 * i);
            uint16_t g;
            for (g = start; g <= end; g++) {
                glyphs[count++] = g;
            }
        }
    }
    *out_count = count;
    return glyphs;
}

static void parse_class_def(const unsigned char *gpos, uint32_t offset,
                            uint16_t *classes, int num_glyphs)
{
    const unsigned char *p = gpos + offset;
    uint16_t format = rd16(p);
    int i;
    if (format == 1) {
        uint16_t start = rd16(p + 2);
        uint16_t count = rd16(p + 4);
        for (i = 0; i < count; i++) {
            int g = (int)start + i;
            if (g >= 0 && g < num_glyphs) {
                classes[g] = rd16(p + 6 + 2 * i);
            }
        }
    } else if (format == 2) {
        uint16_t ranges = rd16(p + 2);
        for (i = 0; i < ranges; i++) {
            uint16_t start = rd16(p + 4 + 6 * i);
            uint16_t end = rd16(p + 6 + 6 * i);
            uint16_t cls = rd16(p + 8 + 6 * i);
            int g;
            for (g = start; g <= (int)end && g < num_glyphs; g++) {
                classes[g] = cls;
            }
        }
    }
}

static int coverage_contains(const KernSub *sub, int glyph)
{
    int lo = 0;
    int hi = sub->coverage_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int v = sub->coverage[mid];
        if (v == glyph) {
            return 1;
        }
        if (v < glyph) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return 0;
}

static int coverage_index(const KernSub *sub, int glyph)
{
    int lo = 0;
    int hi = sub->coverage_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int v = sub->coverage[mid];
        if (v == glyph) {
            return mid;
        }
        if (v < glyph) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return -1;
}

void kern_free(KernTable *kern)
{
    int i;
    if (kern == NULL) {
        return;
    }
    for (i = 0; i < kern->count; i++) {
        KernSub *sub = &kern->subs[i];
        free(sub->coverage);
        free(sub->pair_set_offsets);
        free(sub->class1);
        free(sub->class2);
        free(sub->matrix);
    }
    free(kern->subs);
    free(kern);
}

static int parse_pair_subtable(KernTable *kern, KernSub *sub, uint32_t offset,
                               int num_glyphs)
{
    const unsigned char *gpos = kern->gpos;
    const unsigned char *p = gpos + offset;
    uint16_t format = rd16(p);
    uint16_t value1 = rd16(p + 4);
    uint16_t value2 = rd16(p + 6);
    sub->gpos = gpos;
    sub->format = format;
    sub->value1_size = value_size(value1);
    sub->value2_size = value_size(value2);
    sub->xadv_offset = xadvance_offset(value1);
    sub->num_glyphs = num_glyphs;
    sub->coverage = parse_coverage(gpos, offset + rd16(p + 2),
                                   &sub->coverage_count);
    if (sub->coverage == NULL) {
        return 0;
    }
    if (format == 1) {
        int i;
        sub->pair_set_count = rd16(p + 8);
        sub->pair_set_offsets = (uint32_t *)malloc(
            (size_t)sub->pair_set_count * sizeof(uint32_t));
        if (sub->pair_set_offsets == NULL) {
            return 0;
        }
        for (i = 0; i < sub->pair_set_count; i++) {
            sub->pair_set_offsets[i] = offset + rd16(p + 10 + 2 * i);
        }
    } else if (format == 2) {
        uint32_t class1_off = offset + rd16(p + 8);
        uint32_t class2_off = offset + rd16(p + 10);
        int c1, c2;
        int record_size;
        const unsigned char *records;
        sub->class1_count = rd16(p + 12);
        sub->class2_count = rd16(p + 14);
        sub->class1 = (uint16_t *)calloc((size_t)num_glyphs,
                                         sizeof(uint16_t));
        sub->class2 = (uint16_t *)calloc((size_t)num_glyphs,
                                         sizeof(uint16_t));
        sub->matrix = (int16_t *)calloc(
            (size_t)sub->class1_count * (size_t)sub->class2_count,
            sizeof(int16_t));
        if (sub->class1 == NULL || sub->class2 == NULL ||
            sub->matrix == NULL) {
            return 0;
        }
        parse_class_def(gpos, class1_off, sub->class1, num_glyphs);
        parse_class_def(gpos, class2_off, sub->class2, num_glyphs);
        record_size = sub->value1_size + sub->value2_size;
        records = p + 16;
        for (c1 = 0; c1 < sub->class1_count; c1++) {
            for (c2 = 0; c2 < sub->class2_count; c2++) {
                const unsigned char *rec =
                    records + (size_t)(c1 * sub->class2_count + c2) *
                                  (size_t)record_size;
                int v = 0;
                if (sub->xadv_offset >= 0) {
                    v = (int16_t)rd16(rec + sub->xadv_offset);
                }
                sub->matrix[c1 * sub->class2_count + c2] = (int16_t)v;
            }
        }
    } else {
        return 0;
    }
    return 1;
}

static int pair_lookup_score(const unsigned char *gpos, uint32_t lookup_off,
                             int type)
{
    int sub_count = rd16(gpos + lookup_off + 4);
    int score = 0;
    int i;
    for (i = 0; i < sub_count; i++) {
        uint32_t sub_off = lookup_off + rd16(gpos + lookup_off + 6 + 2 * i);
        int eff_type = type;
        if (type == 9) {
            eff_type = rd16(gpos + sub_off + 2);
            sub_off += rd32(gpos + sub_off + 4);
        }
        if (eff_type == 2) {
            const unsigned char *p = gpos + sub_off;
            int format = rd16(p);
            if (format == 1) {
                score += rd16(p + 8) * 8;
            } else if (format == 2) {
                score += rd16(p + 12) * rd16(p + 14);
            }
        }
    }
    return score;
}

KernTable *kern_parse(const unsigned char *data, size_t size,
                      int num_glyphs)
{
    uint32_t gpos_off = 0;
    uint32_t gpos_len = 0;
    int num_tables;
    int i;
    uint32_t lookup_list;
    int lookup_count;
    int best_score = 0;
    int best_lookup = -1;
    int best_type = 0;
    KernTable *kern;

    if (size < 12) {
        return NULL;
    }
    num_tables = rd16(data + 4);
    for (i = 0; i < num_tables; i++) {
        const unsigned char *rec = data + 12 + 16 * i;
        if (memcmp(rec, "GPOS", 4) == 0) {
            gpos_off = rd32(rec + 8);
            gpos_len = rd32(rec + 12);
            break;
        }
    }
    if (gpos_off == 0 || gpos_len < 10 || gpos_off + gpos_len > size) {
        return NULL;
    }
    {
        const unsigned char *gpos = data + gpos_off;
        lookup_list = rd16(gpos + 8);
        lookup_count = rd16(gpos + lookup_list);
        for (i = 0; i < lookup_count; i++) {
            uint32_t lookup_off =
                lookup_list + rd16(gpos + lookup_list + 2 + 2 * i);
            int type = rd16(gpos + lookup_off);
            int score;
            if (type != 2 && type != 9) {
                continue;
            }
            score = pair_lookup_score(gpos, lookup_off, type);
            if (score > best_score) {
                best_score = score;
                best_lookup = i;
                best_type = type;
            }
        }
    }
    if (best_lookup < 0 || best_score <= 0) {
        return NULL;
    }
    kern = (KernTable *)calloc(1, sizeof(KernTable));
    if (kern == NULL) {
        return NULL;
    }
    kern->gpos = data + gpos_off;
    {
        const unsigned char *gpos = kern->gpos;
        uint32_t lookup_off =
            lookup_list + rd16(gpos + lookup_list + 2 + 2 * best_lookup);
        int sub_count = rd16(gpos + lookup_off + 4);
        kern->count = 0;
        kern->subs = (KernSub *)calloc((size_t)sub_count, sizeof(KernSub));
        if (kern->subs == NULL) {
            kern_free(kern);
            return NULL;
        }
        kern->count = sub_count;
        for (i = 0; i < sub_count; i++) {
            uint32_t sub_off =
                lookup_off + rd16(gpos + lookup_off + 6 + 2 * i);
            if (best_type == 9) {
                sub_off += rd32(gpos + sub_off + 4);
            }
            if (!parse_pair_subtable(kern, &kern->subs[i], sub_off,
                                     num_glyphs)) {
                kern_free(kern);
                return NULL;
            }
        }
    }
    return kern;
}

int kern_value(const KernTable *kern, int glyph1, int glyph2)
{
    int i;
    if (kern == NULL || glyph1 <= 0 || glyph2 <= 0) {
        return 0;
    }
    for (i = 0; i < kern->count; i++) {
        const KernSub *sub = &kern->subs[i];
        int index;
        if (!coverage_contains(sub, glyph1)) {
            continue;
        }
        if (sub->format == 1) {
            uint32_t pair_set;
            int count;
            int record_size;
            int j;
            index = coverage_index(sub, glyph1);
            if (index < 0 || index >= sub->pair_set_count) {
                continue;
            }
            pair_set = sub->pair_set_offsets[index];
            count = rd16(sub->gpos + pair_set);
            record_size = 2 + sub->value1_size + sub->value2_size;
            for (j = 0; j < count; j++) {
                const unsigned char *rec =
                    sub->gpos + pair_set + 2 + (size_t)j * (size_t)record_size;
                if ((int)rd16(rec) == glyph2) {
                    if (sub->xadv_offset >= 0) {
                        return (int16_t)rd16(rec + 2 + sub->xadv_offset);
                    }
                    return 0;
                }
            }
        } else if (sub->format == 2) {
            int c1;
            int c2;
            if (glyph1 >= sub->num_glyphs || glyph2 >= sub->num_glyphs) {
                continue;
            }
            c1 = sub->class1[glyph1];
            c2 = sub->class2[glyph2];
            if (c1 < sub->class1_count && c2 < sub->class2_count) {
                return sub->matrix[c1 * sub->class2_count + c2];
            }
            return 0;
        }
    }
    return 0;
}

