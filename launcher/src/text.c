#define STB_TRUETYPE_IMPLEMENTATION
#include "text.h"

#include "gpos.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int glyph;
    float size;
    int fx_q; /* subpixel x offset * 4 */
    unsigned char *bitmap;
    int w;
    int h;
    int xoff;
    int yoff;
} GlyphEntry;

struct GlyphCache {
    GlyphEntry *items;
    int count;
    int cap;
};

static int utf8_decode(const char **cursor)
{
    const unsigned char *s = (const unsigned char *)*cursor;
    int cp;
    if (*s == 0) {
        return 0;
    }
    if (s[0] < 0x80) {
        *cursor += 1;
        return s[0];
    }
    if ((s[0] & 0xE0) == 0xC0 && s[1] != 0) {
        cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *cursor += 2;
        return cp;
    }
    if ((s[0] & 0xF0) == 0xE0 && s[1] != 0 && s[2] != 0) {
        cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *cursor += 3;
        return cp;
    }
    if ((s[0] & 0xF8) == 0xF0 && s[1] != 0 && s[2] != 0 && s[3] != 0) {
        cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
             ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        *cursor += 4;
        return cp;
    }
    *cursor += 1;
    return 0xFFFD;
}

static size_t utf8_prev_boundary(const char *s, size_t len)
{
    if (len == 0) {
        return 0;
    }
    len--;
    while (len > 0 && (((unsigned char)s[len]) & 0xC0) == 0x80) {
        len--;
    }
    return len;
}

static int load_file(const char *path, unsigned char **out, size_t *out_size)
{
    FILE *fp;
    long size;
    unsigned char *data;
    size_t got;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    size = ftell(fp);
    if (size <= 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }
    data = (unsigned char *)malloc((size_t)size);
    if (data == NULL) {
        fclose(fp);
        return 0;
    }
    got = fread(data, 1, (size_t)size, fp);
    fclose(fp);
    if (got != (size_t)size) {
        free(data);
        return 0;
    }
    *out = data;
    *out_size = (size_t)size;
    return 1;
}

static int font_load(Font *font, const char *dir, const char *filename)
{
    char path[1024];
    int offset;

    snprintf(path, sizeof(path), "%s/%s", dir, filename);
    if (!load_file(path, &font->data, &font->size)) {
        fprintf(stderr, "khome: cannot read font %s\n", path);
        return 0;
    }
    offset = stbtt_GetFontOffsetForIndex(font->data, 0);
    if (offset < 0 || !stbtt_InitFont(&font->info, font->data, offset)) {
        fprintf(stderr, "khome: cannot parse font %s\n", path);
        free(font->data);
        font->data = NULL;
        font->size = 0;
        return 0;
    }
    font->cache = (GlyphCache *)calloc(1, sizeof(GlyphCache));
    if (font->cache == NULL) {
        free(font->data);
        font->data = NULL;
        font->size = 0;
        return 0;
    }
    font->kern = kern_parse(font->data, font->size,
                            font->info.numGlyphs);
    return 1;
}

int fonts_load(Fonts *fonts, const char *dir)
{
    if (fonts == NULL || dir == NULL) {
        return 0;
    }
    memset(fonts, 0, sizeof(*fonts));
    if (!font_load(&fonts->font[FONT_SERIF_B], dir, "NotoSerifJP-Bold.ttf")) {
        fonts_free(fonts);
        return 0;
    }
    if (!font_load(&fonts->font[FONT_SERIF_R], dir,
                   "NotoSerifJP-Regular.ttf")) {
        fonts_free(fonts);
        return 0;
    }
    if (!font_load(&fonts->font[FONT_SANS_M], dir, "NotoSansJP-Medium.ttf")) {
        fonts_free(fonts);
        return 0;
    }
    return 1;
}

static void cache_free(GlyphCache *cache)
{
    int i;
    if (cache == NULL) {
        return;
    }
    for (i = 0; i < cache->count; i++) {
        free(cache->items[i].bitmap);
    }
    free(cache->items);
    free(cache);
}

void fonts_free(Fonts *fonts)
{
    int i;
    if (fonts == NULL) {
        return;
    }
    for (i = 0; i < FONT_COUNT; i++) {
        Font *font = &fonts->font[i];
        cache_free(font->cache);
        font->cache = NULL;
        kern_free(font->kern);
        font->kern = NULL;
        free(font->data);
        font->data = NULL;
        font->size = 0;
    }
}

const Font *fonts_get(const Fonts *fonts, FontRole role)
{
    if (fonts == NULL || role < 0 || role >= FONT_COUNT) {
        return NULL;
    }
    return &fonts->font[role];
}

static float font_scale(const Font *font, float size)
{
    return stbtt_ScaleForMappingEmToPixels(&font->info, size);
}

#define KHOME_MAX_GLYPHS 512

typedef struct {
    int cps[KHOME_MAX_GLYPHS];
    int glyphs[KHOME_MAX_GLYPHS];
    int count;
} ShapedText;

static void shape_text(const Font *font, const char *utf8, ShapedText *out)
{
    const char *cursor = utf8;
    out->count = 0;
    while (cursor != NULL && *cursor != 0 &&
           out->count < KHOME_MAX_GLYPHS) {
        int cp = utf8_decode(&cursor);
        out->cps[out->count] = cp;
        out->glyphs[out->count] = stbtt_FindGlyphIndex(&font->info, cp);
        out->count++;
    }
}

float text_width(const Font *font, float size, float letter_spacing,
                 const char *utf8)
{
    ShapedText shaped;
    float width = 0.0f;
    float scale;
    int i;

    if (font == NULL || utf8 == NULL || size <= 0.0f) {
        return 0.0f;
    }
    shape_text(font, utf8, &shaped);
    scale = font_scale(font, size);
    for (i = 0; i < shaped.count; i++) {
        int advance = 0;
        int lsb = 0;
        stbtt_GetCodepointHMetrics(&font->info, shaped.cps[i], &advance, &lsb);
        width += (float)advance * scale;
        if (i + 1 < shaped.count) {
            width += (float)kern_value(font->kern, shaped.glyphs[i],
                                       shaped.glyphs[i + 1]) *
                     scale;
            width += letter_spacing;
        }
    }
    return width;
}

void text_ellipsize(const Font *font, float size, float letter_spacing,
                    const char *src, float max_width, char *dst,
                    size_t dst_size)
{
    size_t len;
    size_t capacity;
    char *candidate;
    size_t i;

    if (dst == NULL || dst_size == 0) {
        return;
    }
    dst[0] = 0;
    if (src == NULL || font == NULL) {
        return;
    }
    if (text_width(font, size, letter_spacing, src) <= max_width) {
        snprintf(dst, dst_size, "%s", src);
        return;
    }
    capacity = strlen(src) + 4;
    candidate = (char *)malloc(capacity);
    if (candidate == NULL) {
        return;
    }
    /* Reserve room for U+2026, as text-overflow:ellipsis does. */
    len = strlen(src);
    while (len > 0) {
        len = utf8_prev_boundary(src, len);
        for (i = 0; i < len; i++) {
            candidate[i] = src[i];
        }
        candidate[len] = (char)0xE2;
        candidate[len + 1] = (char)0x80;
        candidate[len + 2] = (char)0xA6;
        candidate[len + 3] = 0;
        if (text_width(font, size, letter_spacing, candidate) <= max_width) {
            snprintf(dst, dst_size, "%s", candidate);
            free(candidate);
            return;
        }
    }
    snprintf(dst, dst_size, "\xE2\x80\xA6");
    free(candidate);
}

/* Line breaking.  The classes live in one sorted table so the rules stay data
 * and every branch below asks the same lookup. */
enum {
    CH_NORMAL = 0, /* break allowed on either side */
    CH_WORD,       /* Latin word character: breaks only at spaces */
    CH_SPACE,
    CH_HEAD_NG,    /* may not begin a line */
    CH_END_NG,     /* may not end a line */
    CH_HANG        /* may not begin a line, but may overhang the right edge */
};

typedef struct {
    int lo;
    int hi;
    unsigned char cls;
} CharClassRange;

static const CharClassRange k_char_classes[] = {
    { 0x0009, 0x000D, CH_SPACE },
    { 0x0020, 0x0020, CH_SPACE },
    { 0x0021, 0x007E, CH_WORD },
    { 0x00A0, 0x00A0, CH_SPACE },
    { 0x00C0, 0x024F, CH_WORD },
    { 0x2018, 0x2018, CH_END_NG },  /* ‘ */
    { 0x2019, 0x2019, CH_HEAD_NG }, /* ’ */
    { 0x201C, 0x201C, CH_END_NG },  /* “ */
    { 0x201D, 0x201D, CH_HEAD_NG }, /* ” */
    { 0x3000, 0x3000, CH_SPACE },
    { 0x3001, 0x3002, CH_HANG },    /* 、。 */
    { 0x3005, 0x3005, CH_HEAD_NG }, /* 々 */
    { 0x3008, 0x3008, CH_END_NG },  /* 〈 */
    { 0x3009, 0x3009, CH_HEAD_NG }, /* 〉 */
    { 0x300A, 0x300A, CH_END_NG },  /* 《 */
    { 0x300B, 0x300B, CH_HEAD_NG }, /* 》 */
    { 0x300C, 0x300C, CH_END_NG },  /* 「 */
    { 0x300D, 0x300D, CH_HEAD_NG }, /* 」 */
    { 0x300E, 0x300E, CH_END_NG },  /* 『 */
    { 0x300F, 0x300F, CH_HEAD_NG }, /* 』 */
    { 0x3010, 0x3010, CH_END_NG },  /* 【 */
    { 0x3011, 0x3011, CH_HEAD_NG }, /* 】 */
    { 0x3014, 0x3014, CH_END_NG },  /* 〔 */
    { 0x3015, 0x3015, CH_HEAD_NG }, /* 〕 */
    { 0x3016, 0x3016, CH_END_NG },  /* 〖 */
    { 0x3017, 0x3017, CH_HEAD_NG }, /* 〗 */
    { 0x3018, 0x3018, CH_END_NG },  /* 〘 */
    { 0x3019, 0x3019, CH_HEAD_NG }, /* 〙 */
    { 0x301A, 0x301A, CH_END_NG },  /* 〚 */
    { 0x301B, 0x301B, CH_HEAD_NG }, /* 〛 */
    { 0x3041, 0x3041, CH_HEAD_NG }, /* ぁ */
    { 0x3043, 0x3043, CH_HEAD_NG }, /* ぃ */
    { 0x3045, 0x3045, CH_HEAD_NG }, /* ぅ */
    { 0x3047, 0x3047, CH_HEAD_NG }, /* ぇ */
    { 0x3049, 0x3049, CH_HEAD_NG }, /* ぉ */
    { 0x3063, 0x3063, CH_HEAD_NG }, /* っ */
    { 0x3083, 0x3083, CH_HEAD_NG }, /* ゃ */
    { 0x3085, 0x3085, CH_HEAD_NG }, /* ゅ */
    { 0x3087, 0x3087, CH_HEAD_NG }, /* ょ */
    { 0x308E, 0x308E, CH_HEAD_NG }, /* ゎ */
    { 0x309B, 0x309C, CH_HEAD_NG }, /* ゛゜ */
    { 0x30A1, 0x30A1, CH_HEAD_NG }, /* ァ */
    { 0x30A3, 0x30A3, CH_HEAD_NG }, /* ィ */
    { 0x30A5, 0x30A5, CH_HEAD_NG }, /* ゥ */
    { 0x30A7, 0x30A7, CH_HEAD_NG }, /* ェ */
    { 0x30A9, 0x30A9, CH_HEAD_NG }, /* ォ */
    { 0x30C3, 0x30C3, CH_HEAD_NG }, /* ッ */
    { 0x30E3, 0x30E3, CH_HEAD_NG }, /* ャ */
    { 0x30E5, 0x30E5, CH_HEAD_NG }, /* ュ */
    { 0x30E7, 0x30E7, CH_HEAD_NG }, /* ョ */
    { 0x30EE, 0x30EE, CH_HEAD_NG }, /* ヮ */
    { 0x30F5, 0x30F6, CH_HEAD_NG }, /* ヵヶ */
    { 0x30FB, 0x30FB, CH_HEAD_NG }, /* ・ */
    { 0x30FC, 0x30FC, CH_HEAD_NG }, /* ー */
    { 0xFF01, 0xFF01, CH_HEAD_NG }, /* ！ */
    { 0xFF08, 0xFF08, CH_END_NG },  /* （ */
    { 0xFF09, 0xFF09, CH_HEAD_NG }, /* ） */
    { 0xFF0C, 0xFF0C, CH_HANG },    /* ， */
    { 0xFF0E, 0xFF0E, CH_HANG },    /* ． */
    { 0xFF10, 0xFF19, CH_WORD },    /* ０-９ */
    { 0xFF1A, 0xFF1B, CH_HEAD_NG }, /* ：； */
    { 0xFF1F, 0xFF1F, CH_HEAD_NG }, /* ？ */
    { 0xFF21, 0xFF3A, CH_WORD },    /* Ａ-Ｚ */
    { 0xFF3B, 0xFF3B, CH_END_NG },  /* ［ */
    { 0xFF3D, 0xFF3D, CH_HEAD_NG }, /* ］ */
    { 0xFF41, 0xFF5A, CH_WORD },    /* ａ-ｚ */
    { 0xFF5B, 0xFF5B, CH_END_NG },  /* ｛ */
    { 0xFF5D, 0xFF5D, CH_HEAD_NG }, /* ｝ */
    { 0xFF61, 0xFF61, CH_HANG },    /* ｡ */
    { 0xFF62, 0xFF62, CH_END_NG },  /* ｢ */
    { 0xFF63, 0xFF63, CH_HEAD_NG }, /* ｣ */
    { 0xFF64, 0xFF64, CH_HANG }     /* ､ */
};

static unsigned char char_class(int cp)
{
    int lo = 0;
    int hi = (int)(sizeof(k_char_classes) / sizeof(k_char_classes[0])) - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (cp < k_char_classes[mid].lo) {
            hi = mid - 1;
        } else if (cp > k_char_classes[mid].hi) {
            lo = mid + 1;
        } else {
            return k_char_classes[mid].cls;
        }
    }
    return CH_NORMAL;
}

/* Hanging marks are forbidden at the head of a line too; they only differ in
 * that an overflowing one may stay on the line that is already full. */
static int cannot_start_line(unsigned char cls)
{
    return cls == CH_HEAD_NG || cls == CH_HANG;
}

typedef struct {
    int cp;
    size_t next;
    int glyph;
    unsigned char cls;
} CharInfo;

static void char_info(const Font *font, const char *s, size_t off,
                      CharInfo *out)
{
    const char *cursor = s + off;
    out->cp = utf8_decode(&cursor);
    out->next = (size_t)(cursor - s);
    if (out->next <= off) {
        out->next = off + 1;
    }
    out->glyph = stbtt_FindGlyphIndex(&font->info, out->cp);
    out->cls = char_class(out->cp);
}

static float char_step(const Font *font, float scale, float letter_spacing,
                       int prev_glyph, const CharInfo *ci)
{
    int advance = 0;
    int lsb = 0;
    float step;
    stbtt_GetCodepointHMetrics(&font->info, ci->cp, &advance, &lsb);
    step = (float)advance * scale;
    if (prev_glyph >= 0) {
        step += letter_spacing;
        step += (float)kern_value(font->kern, prev_glyph, ci->glyph) * scale;
    }
    return step;
}

/* Start of the block that must travel with a closing mark at `end`.  It is
 * the single character at prev, or the whole word that contains it when that
 * word fits a column on its own.  A word that fills the line from its start
 * cannot move without emptying the line, so it keeps the single carry. */
static size_t carry_start(const Font *font, float scale, float letter_spacing,
                          const char *para, size_t start, size_t end,
                          size_t prev, float line_width)
{
    size_t word_start = prev;
    size_t q;
    float width = 0.0f;
    int prev_glyph = -1;
    CharInfo ci;

    char_info(font, para, prev, &ci);
    if (ci.cls != CH_WORD) {
        return prev;
    }
    while (word_start > start) {
        size_t before = utf8_prev_boundary(para, word_start);
        char_info(font, para, before, &ci);
        if (ci.cls != CH_WORD) {
            break;
        }
        word_start = before;
    }
    if (word_start == start) {
        return prev;
    }
    q = word_start;
    while (q < end) {
        char_info(font, para, q, &ci);
        width += char_step(font, scale, letter_spacing, prev_glyph, &ci);
        prev_glyph = ci.glyph;
        q = ci.next;
    }
    if (width > line_width + 0.001f) {
        return prev;
    }
    return word_start;
}

/* End byte offset of one line, starting at start.  The caller has already
 * stripped leading spaces. */
static size_t break_line(const Font *font, float scale, float letter_spacing,
                         const char *para, size_t para_len, size_t start,
                         float avail, float line_width)
{
    float width = 0.0f;
    size_t last_space = start;
    int have_space = 0;
    size_t word_start = start;
    int in_word = 0;
    int prev_glyph = -1;
    size_t q = start;
    size_t end;
    CharInfo ci;

    while (q < para_len) {
        float step;
        char_info(font, para, q, &ci);
        step = char_step(font, scale, letter_spacing, prev_glyph, &ci);
        if (width + step <= avail + 0.001f) {
            width += step;
            prev_glyph = ci.glyph;
            if (ci.cls == CH_SPACE && q > start) {
                last_space = q;
                have_space = 1;
                in_word = 0;
            } else if (ci.cls == CH_WORD) {
                if (!in_word) {
                    word_start = q;
                    in_word = 1;
                }
            } else {
                in_word = 0;
            }
            q = ci.next;
            continue;
        }
        if (width <= 0.0f) {
            q = ci.next; /* even one character is wider than the line */
            break;
        }
        if (ci.cls == CH_HANG) {
            q = ci.next; /* one punctuation mark may hang past the edge */
            break;
        }
        if (ci.cls == CH_WORD) {
            if (in_word && word_start > start) {
                /* Break before the word, never inside it.  When a space sits
                 * right before the word, break there so the space is dropped
                 * as leading whitespace instead of trailing the line. */
                if (have_space &&
                    last_space == utf8_prev_boundary(para, word_start)) {
                    q = last_space;
                } else {
                    q = word_start;
                }
                break;
            }
            if (!in_word && have_space) {
                q = last_space;
                break;
            }
        }
        break;
    }
    end = q;

    /* A closing mark may not open the next line: push the character before it
     * down as well.  A run of them, including hanging marks that lost their
     * anchor, comes down as one block. */
    while (end < para_len && end > start) {
        size_t prev;
        char_info(font, para, end, &ci);
        if (!cannot_start_line(ci.cls)) {
            break;
        }
        prev = utf8_prev_boundary(para, end);
        if (prev <= start) {
            break;
        }
        end = carry_start(font, scale, letter_spacing, para, start, end, prev,
                          line_width);
    }
    /* An opening bracket may not close this line. */
    while (end > start) {
        size_t prev = utf8_prev_boundary(para, end);
        char_info(font, para, prev, &ci);
        if (ci.cls != CH_END_NG || prev <= start) {
            break;
        }
        end = prev;
    }
    if (end <= start) {
        char_info(font, para, start, &ci);
        end = ci.next;
    }
    return end;
}

/* A paragraph counts as Japanese when it holds any kana or ideograph, even if
 * it opens with a Latin letter or a digit. */
static int paragraph_is_japanese(const char *para, size_t para_len)
{
    size_t off = 0;
    while (off < para_len) {
        const char *cursor = para + off;
        int cp = utf8_decode(&cursor);
        if ((cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x9FFF)) {
            return 1;
        }
        if (cursor <= para + off) {
            break;
        }
        off = (size_t)(cursor - para);
    }
    return 0;
}

/* Extra pixels for every gap in [start, end) so the line fills avail.  Zero
 * keeps the line ragged: too few characters, a hanging mark or opening
 * bracket at the end, no room to fill, more than two ems of slack, or gaps
 * that would open by more than 0.15 em each. */
static float line_justify(const Font *font, float scale, float letter_spacing,
                          const char *para, size_t start, size_t end,
                          float avail, float em)
{
    float natural = 0.0f;
    int count = 0;
    int prev_glyph = -1;
    int last_cls = CH_NORMAL;
    size_t q = start;
    float extra;
    float gap;
    CharInfo ci;

    while (q < end) {
        char_info(font, para, q, &ci);
        natural += char_step(font, scale, letter_spacing, prev_glyph, &ci);
        prev_glyph = ci.glyph;
        last_cls = ci.cls;
        count++;
        q = ci.next;
    }
    if (count < 2 || last_cls == CH_HANG || last_cls == CH_END_NG) {
        return 0.0f;
    }
    extra = avail - natural;
    if (extra <= 0.5f || extra > 2.0f * em) {
        return 0.0f;
    }
    gap = extra / (float)(count - 1);
    return gap > 0.15f * em ? 0.0f : gap;
}

int text_wrap(const Font *font, float size, float letter_spacing,
              const char *utf8, float line_width, TextLine *lines,
              int max_lines, int *truncated)
{
    const char *para;
    float scale;
    int count = 0;

    if (truncated != NULL) {
        *truncated = 0;
    }
    if (utf8 == NULL || utf8[0] == 0) {
        return 0;
    }
    if (font == NULL || size <= 0.0f || line_width <= 0.0f || lines == NULL ||
        max_lines <= 0) {
        if (truncated != NULL) {
            *truncated = 1;
        }
        return 0;
    }
    scale = font_scale(font, size);
    para = utf8;
    for (;;) {
        const char *nl = strchr(para, '\n');
        size_t para_len = nl != NULL ? (size_t)(nl - para) : strlen(para);
        size_t base = (size_t)(para - utf8);
        int japanese = paragraph_is_japanese(para, para_len);
        int justify_para = paragraph_is_japanese(para, para_len);
        size_t off = 0;

        if (para_len == 0 && *para == 0) {
            break; /* a trailing newline adds no line */
        }
        for (;;) {
            size_t start = off;
            size_t end;
            float indent;
            CharInfo ci;

            while (start < para_len) {
                char_info(font, para, start, &ci);
                if (ci.cls != CH_SPACE) {
                    break;
                }
                start = ci.next;
            }
            indent = off == 0 && japanese ? size : 0.0f;
            if (count >= max_lines) {
                if (truncated != NULL) {
                    *truncated = 1;
                }
                return count;
            }
            if (start >= para_len) {
                lines[count].start = base + start;
                lines[count].length = 0;
                lines[count].indent = indent;
                lines[count].justify = 0.0f;
                count++;
                break;
            }
            end = break_line(font, scale, letter_spacing, para, para_len,
                             start, line_width - indent, line_width);
            lines[count].start = base + start;
            lines[count].length = end - start;
            lines[count].indent = indent;
            lines[count].justify = 0.0f;
            if (justify_para && end < para_len) {
                lines[count].justify =
                    line_justify(font, scale, letter_spacing, para, start, end,
                                 line_width - indent, size);
            }
            count++;
            off = end;
            if (off >= para_len) {
                break;
            }
        }
        if (nl == NULL) {
            break;
        }
        para = nl + 1;
    }
    return count;
}

static const GlyphEntry *glyph_get(const Font *font, int glyph, float size,
                                   int fx_q)
{
    GlyphCache *cache;
    int i;
    GlyphEntry *entry;
    float scale;
    unsigned char *bitmap;

    if (font == NULL || font->cache == NULL || glyph <= 0) {
        return NULL;
    }
    cache = font->cache;
    for (i = 0; i < cache->count; i++) {
        if (cache->items[i].glyph == glyph && cache->items[i].size == size &&
            cache->items[i].fx_q == fx_q) {
            return &cache->items[i];
        }
    }
    if (cache->count == cache->cap) {
        int new_cap = cache->cap == 0 ? 64 : cache->cap * 2;
        GlyphEntry *grown = (GlyphEntry *)realloc(
            cache->items, (size_t)new_cap * sizeof(GlyphEntry));
        if (grown == NULL) {
            return NULL;
        }
        cache->items = grown;
        cache->cap = new_cap;
    }
    entry = &cache->items[cache->count];
    memset(entry, 0, sizeof(*entry));
    entry->glyph = glyph;
    entry->size = size;
    entry->fx_q = fx_q;
    scale = font_scale(font, size);
    bitmap = stbtt_GetGlyphBitmapSubpixel(&font->info, scale, scale,
                                          (float)fx_q / 4.0f, 0.0f, glyph,
                                          &entry->w, &entry->h, &entry->xoff,
                                          &entry->yoff);
    if (bitmap != NULL && entry->w > 0 && entry->h > 0) {
        entry->bitmap = bitmap;
    } else {
        if (bitmap != NULL) {
            free(bitmap);
        }
        entry->w = entry->h = 0;
    }
    cache->count++;
    return entry;
}

static void draw_alpha_bitmap(Canvas *canvas, int x, int y,
                              const unsigned char *pixels, int w, int h,
                              uint8_t color, int cx0, int cy0, int cx1,
                              int cy1)
{
    int row, col;
    if (pixels == NULL || w <= 0 || h <= 0) {
        return;
    }
    for (row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || py >= canvas->h || py < cy0 || py >= cy1) {
            continue;
        }
        for (col = 0; col < w; col++) {
            int px = x + col;
            int alpha = pixels[(size_t)row * (size_t)w + col];
            uint8_t *dst;
            int out;
            if (alpha == 0 || px < 0 || px >= canvas->w || px < cx0 ||
                px >= cx1) {
                continue;
            }
            dst = canvas->pixels + (size_t)py * (size_t)canvas->stride + px;
            out = ((int)color * alpha + (int)(*dst) * (255 - alpha) + 127) / 255;
            *dst = (uint8_t)out;
        }
    }
}

static float text_draw_impl(Canvas *canvas, const Font *font, float size,
                            float letter_spacing, float x, float baseline,
                            const char *utf8, uint8_t color, TextAlign align,
                            float box_width, int clip_x0, int clip_y0,
                            int clip_x1, int clip_y1)
{
    ShapedText shaped;
    float scale;
    float start;
    float pen;
    int i;
    if (canvas == NULL || font == NULL || utf8 == NULL || size <= 0.0f) {
        return x;
    }
    if (align != TEXT_LEFT) {
        float width = text_width(font, size, letter_spacing, utf8);
        if (utf8[0] != 0) {
            width += letter_spacing;
        }
        if (align == TEXT_CENTER) {
            start = x + (box_width - width) * 0.5f;
        } else {
            start = x + box_width - width;
        }
    } else {
        start = x;
    }
    scale = font_scale(font, size);
    shape_text(font, utf8, &shaped);
    pen = start;
    for (i = 0; i < shaped.count; i++) {
        int advance = 0;
        int lsb = 0;
        int glyph = shaped.glyphs[i];
        stbtt_GetCodepointHMetrics(&font->info, shaped.cps[i], &advance, &lsb);
        if (glyph > 0) {
            double floor_pos = floor((double)pen);
            double frac = (double)pen - floor_pos;
            int fx_q = (int)floor(frac * 4.0 + 0.5);
            const GlyphEntry *entry;
            if (fx_q >= 4) {
                fx_q = 0;
                floor_pos += 1.0;
            }
            entry = glyph_get(font, glyph, size, fx_q);
            if (entry != NULL && entry->w > 0) {
                draw_alpha_bitmap(canvas, (int)floor_pos + entry->xoff,
                                  (int)lroundf(baseline) + entry->yoff,
                                  entry->bitmap, entry->w, entry->h, color,
                                  clip_x0, clip_y0, clip_x1, clip_y1);
            }
        }
        pen += (float)advance * scale + letter_spacing;
        if (i + 1 < shaped.count) {
            pen += (float)kern_value(font->kern, shaped.glyphs[i],
                                     shaped.glyphs[i + 1]) *
                   scale;
        }
    }
    return pen;
}

float text_draw(Canvas *canvas, const Font *font, float size,
                float letter_spacing, float x, float baseline,
                const char *utf8, uint8_t color, TextAlign align,
                float box_width)
{
    if (canvas == NULL) {
        return x;
    }
    return text_draw_impl(canvas, font, size, letter_spacing, x, baseline, utf8,
                          color, align, box_width, 0, 0, canvas->w,
                          canvas->h);
}

float text_draw_clip(Canvas *canvas, const Font *font, float size,
                     float letter_spacing, float x, float baseline,
                     const char *utf8, uint8_t color, TextAlign align,
                     float box_width, int clip_x0, int clip_y0, int clip_x1,
                     int clip_y1)
{
    return text_draw_impl(canvas, font, size, letter_spacing, x, baseline, utf8,
                          color, align, box_width, clip_x0, clip_y0, clip_x1,
                          clip_y1);
}

/* Streams one line without the 512-glyph array text_draw_impl uses, so a
 * wrapped line has no length ceiling. */
static float draw_run(Canvas *canvas, const Font *font, float size,
                      float letter_spacing, float justify, float x,
                      float baseline, const char *utf8, size_t length,
                      uint8_t color, int clip_x0, int clip_y0, int clip_x1,
                      int clip_y1)
{
    float scale = font_scale(font, size);
    float pen = x;
    int prev_glyph = -1;
    const char *cursor = utf8;
    const char *limit = utf8 + length;

    while (cursor < limit && *cursor != 0) {
        const char *next = cursor;
        int cp = utf8_decode(&next);
        int glyph;
        int advance = 0;
        int lsb = 0;
        if (next <= cursor) {
            break;
        }
        cursor = next;
        glyph = stbtt_FindGlyphIndex(&font->info, cp);
        if (prev_glyph >= 0) {
            pen += (float)kern_value(font->kern, prev_glyph, glyph) * scale;
        }
        stbtt_GetCodepointHMetrics(&font->info, cp, &advance, &lsb);
        if (glyph > 0) {
            double floor_pos = floor((double)pen);
            double frac = (double)pen - floor_pos;
            int fx_q = (int)floor(frac * 4.0 + 0.5);
            const GlyphEntry *entry;
            if (fx_q >= 4) {
                fx_q = 0;
                floor_pos += 1.0;
            }
            entry = glyph_get(font, glyph, size, fx_q);
            if (entry != NULL && entry->w > 0) {
                draw_alpha_bitmap(canvas, (int)floor_pos + entry->xoff,
                                  (int)lroundf(baseline) + entry->yoff,
                                  entry->bitmap, entry->w, entry->h, color,
                                  clip_x0, clip_y0, clip_x1, clip_y1);
            }
        }
        pen += (float)advance * scale + letter_spacing;
        if (justify != 0.0f && cursor < limit) {
            pen += justify;
        }
        prev_glyph = glyph;
    }
    return pen;
}

int text_draw_lines(Canvas *canvas, const Font *font, float size,
                    float letter_spacing, const char *utf8,
                    const TextLine *lines, int line_count, int first_line,
                    int count, float x, float y, float w, float h,
                    float line_height, uint8_t color)
{
    float scale;
    float baseline;
    size_t total;
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    int first;
    int last;
    int i;
    int cx0, cy0, cx1, cy1;

    if (canvas == NULL || font == NULL || utf8 == NULL || lines == NULL ||
        size <= 0.0f || line_height <= 0.0f || count <= 0) {
        return 0;
    }
    first = first_line < 0 ? 0 : first_line;
    if (first >= line_count) {
        return 0;
    }
    last = first + count;
    if (last > line_count) {
        last = line_count;
    }
    total = strlen(utf8);
    scale = font_scale(font, size);
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &line_gap);
    baseline = y + (float)ascent * scale;
    cx0 = (int)floor((double)x);
    cy0 = (int)floor((double)y);
    /* One em past the column edge stays drawable for a hanging mark. */
    cx1 = (int)ceil((double)(x + w + size));
    cy1 = (int)ceil((double)(y + h));
    for (i = first; i < last; i++) {
        const TextLine *line = &lines[i];
        if (line->start > total || line->length > total - line->start) {
            continue;
        }
        draw_run(canvas, font, size, letter_spacing, line->justify,
                 x + line->indent,
                 baseline + (float)(i - first) * line_height,
                 utf8 + line->start, line->length, color, cx0, cy0, cx1, cy1);
    }
    return last - first;
}
