#include "suites.h"

#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canvas.h"
#include "test_harness.h"
#include "text.h"

/* text_wrap / text_draw_lines behavior: hanging punctuation, head-of-line
 * prohibition, justification, word breaking, and character coverage.  Body
 * type is Serif Regular at 30 px with no letter-spacing (SPEC 7.3). */

#define WRAP_SIZE 30.0f
#define WRAP_SPACING 0.0f
#define WRAP_WIDTH 315.0f

static Fonts g_fonts;
static const Font *g_font;
static int g_fonts_loaded;

static int load_fonts(void)
{
    const char *env = getenv("KHOME_FONTS");
    const char *candidates[4];
    int count = 0;
    int i;

    if (env != NULL && env[0] != 0) {
        candidates[count++] = env;
    }
    candidates[count++] = "../design/system/fonts";
    candidates[count++] = "design/system/fonts";
    candidates[count] = NULL;
    for (i = 0; i < count; i++) {
        if (fonts_load(&g_fonts, candidates[i])) {
            g_font = fonts_get(&g_fonts, FONT_SERIF_R);
            return 1;
        }
    }
    return 0;
}

static int fonts_ready(void)
{
    KH_CHECK_MSG(g_fonts_loaded,
                 "fonts_load failed (run from launcher/ or set KHOME_FONTS)");
    return g_fonts_loaded;
}

static int cp_decode(const char *s, size_t *len)
{
    const unsigned char *u = (const unsigned char *)s;
    if (u[0] == 0) {
        *len = 0;
        return 0;
    }
    if (u[0] < 0x80) {
        *len = 1;
        return u[0];
    }
    if ((u[0] & 0xE0) == 0xC0 && u[1] != 0) {
        *len = 2;
        return ((u[0] & 0x1F) << 6) | (u[1] & 0x3F);
    }
    if ((u[0] & 0xF0) == 0xE0 && u[1] != 0 && u[2] != 0) {
        *len = 3;
        return ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
    }
    if ((u[0] & 0xF8) == 0xF0 && u[1] != 0 && u[2] != 0 && u[3] != 0) {
        *len = 4;
        return ((u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) |
               ((u[2] & 0x3F) << 6) | (u[3] & 0x3F);
    }
    *len = 1;
    return 0xFFFD;
}

static size_t prev_off(const char *s, size_t off)
{
    if (off == 0) {
        return 0;
    }
    off--;
    while (off > 0 && (((unsigned char)s[off]) & 0xC0) == 0x80) {
        off--;
    }
    return off;
}

static int is_hang_mark(int cp)
{
    return cp == 0x3001 || cp == 0x3002 || cp == 0xFF0C || cp == 0xFF0E ||
           cp == 0xFF61 || cp == 0xFF64;
}

/* Characters the brief names as forbidden at the head of a line, plus the
 * hanging marks, which may not open one either. */
static int is_head_forbidden(int cp)
{
    if (is_hang_mark(cp)) {
        return 1;
    }
    switch (cp) {
    case 0x2019: case 0x201D:
    case 0x3005: case 0x3009: case 0x300B: case 0x300D: case 0x300F:
    case 0x3011: case 0x3015: case 0x3017: case 0x3019: case 0x301B:
    case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049:
    case 0x3063: case 0x3083: case 0x3085: case 0x3087: case 0x308E:
    case 0x309B: case 0x309C:
    case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9:
    case 0x30C3: case 0x30E3: case 0x30E5: case 0x30E7: case 0x30EE:
    case 0x30F5: case 0x30F6: case 0x30FB: case 0x30FC:
    case 0xFF01: case 0xFF09: case 0xFF1A: case 0xFF1B: case 0xFF1F:
    case 0xFF3D: case 0xFF5D: case 0xFF63:
        return 1;
    default:
        return 0;
    }
}

static int is_word_char(int cp)
{
    if (cp >= 0x21 && cp <= 0x7E) {
        return 1;
    }
    if (cp >= 0xC0 && cp <= 0x24F) {
        return 1;
    }
    return (cp >= 0xFF10 && cp <= 0xFF19) ||
           (cp >= 0xFF21 && cp <= 0xFF3A) ||
           (cp >= 0xFF41 && cp <= 0xFF5A);
}

static int cp_at(const char *s, size_t off, size_t *len)
{
    return cp_decode(s + off, len);
}

static int last_cp_at(const char *s, size_t end, size_t *len)
{
    return cp_at(s, prev_off(s, end), len);
}

typedef struct {
    const char *text;
    TextLine *lines;
    int count;
    int truncated;
} Wrapped;

static int wrap_text(const char *text, float width, Wrapped *out)
{
    int capacity = (int)strlen(text) + 2;

    out->text = text;
    out->lines = (TextLine *)calloc((size_t)capacity, sizeof(TextLine));
    out->count = 0;
    out->truncated = 0;
    if (out->lines == NULL) {
        return 0;
    }
    out->count = text_wrap(g_font, WRAP_SIZE, WRAP_SPACING, text, width,
                           out->lines, capacity, &out->truncated);
    return 1;
}

static void wrap_free(Wrapped *w)
{
    free(w->lines);
    w->lines = NULL;
    w->count = 0;
}

static void line_copy(const Wrapped *w, int i, char *buf, size_t capacity)
{
    size_t len = w->lines[i].length;
    if (len > capacity - 1) {
        len = capacity - 1;
    }
    memcpy(buf, w->text + w->lines[i].start, len);
    buf[len] = 0;
}

static double line_natural(const Wrapped *w, int i)
{
    char buf[4096];
    line_copy(w, i, buf, sizeof(buf));
    return (double)text_width(g_font, WRAP_SIZE, WRAP_SPACING, buf);
}

static double line_avail(const Wrapped *w, int i, float width)
{
    return (double)(width - w->lines[i].indent);
}

static int line_cp_count(const Wrapped *w, int i)
{
    size_t off = 0;
    int count = 0;
    while (off < w->lines[i].length) {
        size_t len = 0;
        cp_at(w->text + w->lines[i].start, off, &len);
        if (len == 0) {
            break;
        }
        off += len;
        count++;
    }
    return count;
}

static int paragraph_last(const Wrapped *w, int i)
{
    size_t end = w->lines[i].start + w->lines[i].length;
    return end >= strlen(w->text) || w->text[end] == '\n';
}

static int paragraph_first(const Wrapped *w, int i)
{
    return w->lines[i].start == 0 || w->text[w->lines[i].start - 1] == '\n';
}

static int paragraph_is_japanese(const Wrapped *w, int i)
{
    const char *text = w->text;
    size_t off = w->lines[i].start;

    while (off < w->lines[i].start + w->lines[i].length) {
        size_t len = 0;
        int cp = cp_at(text, off, &len);
        if ((cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x9FFF)) {
            return 1;
        }
        if (len == 0) {
            break;
        }
        off += len;
    }
    return 0;
}

/* A break sits inside a word when the character on each side belongs to a
 * word.  The whole word is then [*start, *end). */
static void word_run(const char *text, size_t at, size_t *start, size_t *end)
{
    size_t len = strlen(text);
    size_t ws = at;
    size_t we = at;

    while (ws > 0) {
        size_t clen = 0;
        size_t off = prev_off(text, ws);
        if (!is_word_char(cp_at(text, off, &clen))) {
            break;
        }
        ws = off;
    }
    while (we < len) {
        size_t clen = 0;
        int cp = cp_at(text, we, &clen);
        if (!is_word_char(cp)) {
            break;
        }
        we += clen;
    }
    *start = ws;
    *end = we;
}

static void word_at(const char *text, size_t start, size_t end, char *buf,
                    size_t capacity)
{
    size_t len = end - start;
    if (len > capacity - 1) {
        len = capacity - 1;
    }
    memcpy(buf, text + start, len);
    buf[len] = 0;
}

/* Renders one wrapped line at a time and returns the rightmost inked column,
 * or -1 when the line drew nothing. */
static int ink_right(Canvas *canvas, const Font *font, float size,
                     const char *text, const TextLine *lines, int line_count,
                     int index, float x, float y, float w, float h,
                     float line_height)
{
    int px;
    int py;
    int right = -1;

    canvas_clear(canvas, 255);
    text_draw_lines(canvas, font, size, WRAP_SPACING, text, lines, line_count,
                    index, 1, x, y, w, h, line_height, 0);
    for (py = 0; py < canvas->h; py++) {
        for (px = 0; px < canvas->w; px++) {
            if (canvas->pixels[py * canvas->stride + px] != 255 && px > right) {
                right = px;
            }
        }
    }
    return right;
}

static void test_overhang_only_hanging_punctuation(void)
{
    static const char *const texts[] = {
        "あいうえおかきくけ、さしすせそたちつてとなにぬねのはひふへほまみ"
        "むめもやゆよらりるれろわをん、アイウエオカキクケコサシスセソ。",
        "編集部は「今回の決定に納得していない」と述べた。関係者によると、"
        "２０２６年度の予算案には計上されていないという。",
        "ああああああああああああああああああああああああああああああああ"
        "ああああああああああああああああああああああああああああああああ。"
    };
    size_t t;
    int saw_overhang = 0;

    if (!fonts_ready()) {
        return;
    }
    for (t = 0; t < sizeof(texts) / sizeof(texts[0]); t++) {
        Wrapped w;
        int i;
        if (!wrap_text(texts[t], WRAP_WIDTH, &w)) {
            KH_CHECK_MSG(0, "text %zu: out of memory", t);
            continue;
        }
        KH_CHECK_MSG(w.truncated == 0, "text %zu: wrap truncated", t);
        for (i = 0; i < w.count; i++) {
            size_t last_len = 0;
            int last;
            char one[8];
            double natural = line_natural(&w, i);
            double avail = line_avail(&w, i, WRAP_WIDTH);
            double last_width;

            if (natural <= avail + 0.01) {
                continue;
            }
            saw_overhang = 1;
            last = last_cp_at(w.text, w.lines[i].start + w.lines[i].length,
                              &last_len);
            memcpy(one, w.text + w.lines[i].start + w.lines[i].length -
                            (last_len > 0 ? last_len : 1),
                   last_len);
            one[last_len] = 0;
            last_width = (double)text_width(g_font, WRAP_SIZE, WRAP_SPACING,
                                            one);
            KH_CHECK_MSG(is_hang_mark(last),
                         "text %zu line %d overflows by %.2f px but its last "
                         "character U+%04X is not one of 、。，．｡､",
                         t, i, natural - avail, last);
            KH_CHECK_MSG(natural - last_width <= avail + 0.75,
                         "text %zu line %d: more than the last character is "
                         "past the column edge (all but the last char needs "
                         "%.2f px, the column has %.2f)",
                         t, i, natural - last_width, avail);
        }
        wrap_free(&w);
    }
    KH_CHECK_MSG(saw_overhang, "no line hung a punctuation mark, so the "
                 "overhang rule was never exercised");
}

static void test_hang_punctuation_stays_with_previous_line(void)
{
    static const char *const text =
        "あああああああああ、いいいいいいいいいあいうえおかきくけこさしすせそ";
    Wrapped w;
    char first[64];
    char second[64];

    if (!fonts_ready()) {
        return;
    }
    if (!wrap_text(text, WRAP_WIDTH, &w)) {
        KH_CHECK(0);
        return;
    }
    KH_CHECK_MSG(w.count >= 2, "expected at least 2 lines, got %d", w.count);
    if (w.count >= 2) {
        line_copy(&w, 0, first, sizeof(first));
        line_copy(&w, 1, second, sizeof(second));
        KH_CHECK_MSG(strcmp(first, "あああああああああ、") == 0,
                     "line 0 should keep the full-width comma it hungs: "
                     "expected \"あああああああああ、\", got \"%s\"", first);
        KH_CHECK_MSG(strncmp(second, "い", strlen("い")) == 0,
                     "line 1 should start with the next character: expected "
                     "\"いい...\", got \"%s\"", second);
        KH_CHECK_MSG(w.lines[0].justify == 0.0f,
                     "a hanging line must stay ragged: justify %.2f",
                     (double)w.lines[0].justify);
    }
    wrap_free(&w);
}

static void test_head_forbidden_never_starts_a_line(void)
{
    static const char *const text =
        "ああああああああああああああああ「引用」あああああああああああああ"
        "ああああ（注釈）あああああああああああああああああああああーああ"
        "ああああああああああああああああああ・ああああああああああああ?"
        "あああああああああああああああああああああああああああああああ";
    Wrapped w;
    size_t off = 0;
    int present = 0;
    int i;

    if (!fonts_ready()) {
        return;
    }
    if (!wrap_text(text, WRAP_WIDTH, &w)) {
        KH_CHECK(0);
        return;
    }
    KH_CHECK_MSG(w.count >= 4, "expected several lines, got %d", w.count);
    while (off < strlen(text)) {
        size_t len = 0;
        int cp = cp_at(text, off, &len);
        if (is_head_forbidden(cp)) {
            present++;
        }
        if (len == 0) {
            break;
        }
        off += len;
    }
    KH_CHECK_MSG(present > 0, "test text has no head-forbidden characters");
    for (i = 0; i < w.count; i++) {
        size_t len = 0;
        int cp;
        if (w.lines[i].length == 0) {
            continue;
        }
        cp = cp_at(w.text, w.lines[i].start, &len);
        KH_CHECK_MSG(!paragraph_first(&w, i) || !is_head_forbidden(cp),
                     "line %d starts with the head-forbidden character "
                     "U+%04X", i, cp);
    }
    wrap_free(&w);
}

static void test_head_forbidden_pulls_previous_character(void)
{
    static const char *const text =
        "あああああああああいいいいいいいいいあ」なにぬねの";
    Wrapped w;
    char before[64];
    char after[64];

    if (!fonts_ready()) {
        return;
    }
    if (!wrap_text(text, WRAP_WIDTH, &w)) {
        KH_CHECK(0);
        return;
    }
    KH_CHECK_MSG(w.count == 3, "expected 3 lines, got %d", w.count);
    if (w.count == 3) {
        line_copy(&w, 1, before, sizeof(before));
        line_copy(&w, 2, after, sizeof(after));
        KH_CHECK_MSG(strcmp(before, "いいいいいいいいい") == 0,
                     "line 1 should stop before the character that pairs "
                     "with 」: expected \"いいいいいいいいい\", got \"%s\"",
                     before);
        KH_CHECK_MSG(strcmp(after, "あ」なにぬねの") == 0,
                     "line 2 should carry あ down with 」: expected "
                     "\"あ」なにぬねの\", got \"%s\"", after);
    }
    wrap_free(&w);
}

static void test_head_forbidden_run_pulls_together(void)
{
    static const char *const text =
        "あああああああああいいいいいいいいいあ」」なにぬねの";
    Wrapped w;
    char after[64];

    if (!fonts_ready()) {
        return;
    }
    if (!wrap_text(text, WRAP_WIDTH, &w)) {
        KH_CHECK(0);
        return;
    }
    KH_CHECK_MSG(w.count == 3, "expected 3 lines, got %d", w.count);
    if (w.count == 3) {
        line_copy(&w, 2, after, sizeof(after));
        KH_CHECK_MSG(strcmp(after, "あ」」なにぬねの") == 0,
                     "a run of closing marks must travel with the character "
                     "before it: expected \"あ」」なにぬねの\", got \"%s\"",
                     after);
    }
    wrap_free(&w);
}

static void test_head_forbidden_keeps_word_whole(void)
{
    static const char *const texts[] = {
        "ああああああああAB」なにぬねの",
        "ああああああ（８６）なにぬねの"
    };
    static const char *const words[] = { "AB", "８６" };
    size_t t;

    if (!fonts_ready()) {
        return;
    }
    for (t = 0; t < sizeof(texts) / sizeof(texts[0]); t++) {
        Wrapped w;
        int i;
        int seen = 0;
        char first[64] = "";
        char second[64] = "";

        if (!wrap_text(texts[t], WRAP_WIDTH, &w)) {
            KH_CHECK(0);
            continue;
        }
        if (w.count > 0) {
            line_copy(&w, 0, first, sizeof(first));
        }
        if (w.count > 1) {
            line_copy(&w, 1, second, sizeof(second));
        }
        for (i = 0; i < w.count; i++) {
            char buf[256];
            line_copy(&w, i, buf, sizeof(buf));
            if (strstr(buf, words[t]) != NULL) {
                seen++;
            }
        }
        KH_CHECK_MSG(seen == 1,
                     "text %zu: word %s (%.2f px in a %.2f px column) is not "
                     "on a single line; line 0 \"%s\", line 1 \"%s\"", t,
                     words[t],
                     (double)text_width(g_font, WRAP_SIZE, WRAP_SPACING,
                                        words[t]),
                     (double)WRAP_WIDTH, first, second);
        wrap_free(&w);
    }
}

static const char *const k_justified_text =
    "あああああああああいいいいいいいいいあいうえおかきくけこさしすせそたち"
    "つてなにぬねの";

static void test_justify_fills_nonfinal_lines(void)
{
    Wrapped w;
    int i;

    if (!fonts_ready()) {
        return;
    }
    if (!wrap_text(k_justified_text, WRAP_WIDTH, &w)) {
        KH_CHECK(0);
        return;
    }
    KH_CHECK_MSG(w.count >= 3, "expected several lines, got %d", w.count);
    for (i = 0; i < w.count; i++) {
        int cps = line_cp_count(&w, i);
        double natural = line_natural(&w, i);
        double avail = line_avail(&w, i, WRAP_WIDTH);
        double filled = natural + (double)w.lines[i].justify *
                                   (double)(cps > 1 ? cps - 1 : 1);

        if (paragraph_last(&w, i)) {
            KH_CHECK_MSG(w.lines[i].justify == 0.0f,
                         "last line %d is justified by %.2f px", i,
                         (double)w.lines[i].justify);
            continue;
        }
        KH_CHECK_MSG(w.lines[i].justify > 0.0f,
                     "line %d of a Japanese paragraph is ragged: natural "
                     "%.2f px in a %.2f px column, justify %.2f", i, natural,
                     avail, (double)w.lines[i].justify);
        KH_CHECK_MSG(fabs(filled - avail) <= 0.75,
                     "line %d: natural %.2f + justify %.2f over %d gaps = "
                     "%.2f, expected the column width %.2f", i, natural,
                     (double)w.lines[i].justify, cps - 1, filled, avail);
    }
    wrap_free(&w);
}

static void test_justify_zero_on_paragraph_last_line(void)
{
    static const char *const text =
        "ああああああ\nいいいいいいいいいああああああああ\n\n"
        "うううううううううううううううううううう";
    Wrapped w;
    int i;
    int seen = 0;

    if (!fonts_ready()) {
        return;
    }
    if (!wrap_text(text, WRAP_WIDTH, &w)) {
        KH_CHECK(0);
        return;
    }
    for (i = 0; i < w.count; i++) {
        if (!paragraph_last(&w, i)) {
            continue;
        }
        seen++;
        KH_CHECK_MSG(w.lines[i].justify == 0.0f,
                     "line %d ends its paragraph (or sits before \\n) but is "
                     "justified by %.2f px", i, (double)w.lines[i].justify);
    }
    KH_CHECK_MSG(seen >= 3, "expected a last line for each of the 3 "
                 "paragraphs, saw %d", seen);
    wrap_free(&w);
}

static void test_justify_zero_when_gaps_would_open_too_wide(void)
{
    static const char *const text =
        "ああああああああKindleああああああああああ";
    Wrapped w;

    if (!fonts_ready()) {
        return;
    }
    if (!wrap_text(text, WRAP_WIDTH, &w)) {
        KH_CHECK(0);
        return;
    }
    KH_CHECK_MSG(w.count >= 2, "expected at least 2 lines, got %d", w.count);
    if (w.count >= 2) {
        double natural = line_natural(&w, 0);
        double avail = line_avail(&w, 0, WRAP_WIDTH);
        KH_CHECK_MSG(avail - natural > (double)WRAP_SIZE,
                     "line 0: slack %.2f px is not over one em (%.2f)", avail -
                     natural, (double)WRAP_SIZE);
        KH_CHECK_MSG(w.lines[0].justify == 0.0f,
                     "line 0 has %.2f px of slack (over one em) but justify "
                     "is %.2f", avail - natural,
                     (double)w.lines[0].justify);
        KH_CHECK_MSG(w.lines[1].justify > 0.0f,
                     "line 1 should still be justified, got %.2f",
                     (double)w.lines[1].justify);
    }
    wrap_free(&w);
}

static void test_latin_word_breaks_before_the_word(void)
{
    static const char *const text =
        "ああああああああKindleああああああああああ";
    Wrapped w;
    char first[64];
    char second[64];

    if (!fonts_ready()) {
        return;
    }
    if (!wrap_text(text, WRAP_WIDTH, &w)) {
        KH_CHECK(0);
        return;
    }
    KH_CHECK_MSG(w.count >= 2, "expected at least 2 lines, got %d", w.count);
    if (w.count >= 2) {
        line_copy(&w, 0, first, sizeof(first));
        line_copy(&w, 1, second, sizeof(second));
        KH_CHECK_MSG(strcmp(first, "ああああああああ") == 0,
                     "line 0 should end before the Latin word: expected "
                     "\"ああああああああ\", got \"%s\"", first);
        KH_CHECK_MSG(strncmp(second, "Kindle", strlen("Kindle")) == 0,
                     "line 1 should start with the whole word: expected "
                     "\"Kindle...\", got \"%s\"", second);
    }
    wrap_free(&w);
}

static void test_latin_word_is_not_split(void)
{
    static const char *const texts[] = {
        "ああああああああああああああああああああKindleああああああああああ"
        "ああああああああああああ",
        "Kindleストアで購入した本を読む。対応機種はWindows PCとMacです。"
    };
    size_t t;

    if (!fonts_ready()) {
        return;
    }
    for (t = 0; t < sizeof(texts) / sizeof(texts[0]); t++) {
        Wrapped w;
        int i;
        if (!wrap_text(texts[t], WRAP_WIDTH, &w)) {
            KH_CHECK(0);
            continue;
        }
        for (i = 0; i + 1 < w.count; i++) {
            size_t end = w.lines[i].start + w.lines[i].length;
            size_t next_len = 0;
            size_t prev_len = 0;
            int prev;
            int next;
            if (end >= strlen(w.text) || w.text[end] == '\n') {
                continue;
            }
            prev = last_cp_at(w.text, end, &prev_len);
            next = cp_at(w.text, end, &next_len);
            if (is_word_char(prev) && is_word_char(next)) {
                size_t ws = 0;
                size_t we = 0;
                char word[256];
                word_run(w.text, end, &ws, &we);
                word_at(w.text, ws, we, word, sizeof(word));
                KH_CHECK_MSG(text_width(g_font, WRAP_SIZE, WRAP_SPACING,
                                        word) > WRAP_WIDTH,
                             "text %zu: line %d breaks the word \"%s\" "
                             "(%.2f px) although the column is %.2f px wide",
                             t, i, word,
                             (double)text_width(g_font, WRAP_SIZE,
                                                WRAP_SPACING, word),
                             (double)WRAP_WIDTH);
            }
        }
        wrap_free(&w);
    }
}

static void test_long_word_is_split(void)
{
    static const char *const text = "Supercalifragilisticexpialidocious";
    Wrapped w;
    int i;
    size_t total = 0;

    if (!fonts_ready()) {
        return;
    }
    if (!wrap_text(text, WRAP_WIDTH, &w)) {
        KH_CHECK(0);
        return;
    }
    KH_CHECK_MSG(w.count >= 2, "a word wider than the column must be split, "
                 "got %d line(s)", w.count);
    for (i = 0; i < w.count; i++) {
        KH_CHECK_MSG(line_natural(&w, i) <=
                         line_avail(&w, i, WRAP_WIDTH) + 0.01,
                     "split word line %d is %.2f px, over the %.2f px column",
                     i, line_natural(&w, i),
                     line_avail(&w, i, WRAP_WIDTH));
        total += w.lines[i].length;
    }
    KH_CHECK_MSG(total == strlen(text), "split word lost characters: %zu of "
                 "%zu bytes kept", total, strlen(text));
    wrap_free(&w);
}

static void test_fullwidth_alnum_is_a_word(void)
{
    static const char *const text =
        "ああああああああＡＢＣＤＥＦＧＨＩＪああああああああ";
    Wrapped w;
    int i;
    int seen = 0;

    if (!fonts_ready()) {
        return;
    }
    if (!wrap_text(text, WRAP_WIDTH, &w)) {
        KH_CHECK(0);
        return;
    }
    for (i = 0; i < w.count; i++) {
        char buf[256];
        line_copy(&w, i, buf, sizeof(buf));
        if (strstr(buf, "ＡＢＣＤＥＦＧＨＩＪ") != NULL) {
            seen = 1;
        }
    }
    KH_CHECK_MSG(seen, "the full-width word ＡＢＣＤＥＦＧＨＩＪ (%.2f px) was "
                 "split although the column is %.2f px wide",
                 (double)text_width(g_font, WRAP_SIZE, WRAP_SPACING,
                                    "ＡＢＣＤＥＦＧＨＩＪ"),
                 (double)WRAP_WIDTH);
    for (i = 0; i + 1 < w.count; i++) {
        size_t end = w.lines[i].start + w.lines[i].length;
        size_t prev_len = 0;
        size_t next_len = 0;
        int prev;
        int next;
        if (end >= strlen(w.text)) {
            continue;
        }
        prev = last_cp_at(w.text, end, &prev_len);
        next = cp_at(w.text, end, &next_len);
        KH_CHECK_MSG(!(prev >= 0xFF10 && prev <= 0xFF5A && next >= 0xFF10 &&
                       next <= 0xFF5A),
                     "line %d breaks inside a full-width alphanumeric run "
                     "(U+%04X | U+%04X)", i, prev, next);
    }
    wrap_free(&w);
}

static void test_every_character_appears_once(void)
{
    static const char *const texts[] = {
        "日本語の文章です。ABCdef二行目の文章です。\n次の段落の文章です。",
        k_justified_text,
        "ああああああああああああああああああああああああああああああああ"
        "ああああああああああああああああああああああああああああああああ"
    };
    size_t t;

    if (!fonts_ready()) {
        return;
    }
    for (t = 0; t < sizeof(texts) / sizeof(texts[0]); t++) {
        size_t len = strlen(texts[t]);
        size_t source_len = 0;
        size_t wrapped_len = 0;
        size_t i;
        char *source = (char *)malloc(len + 1);
        char *joined = (char *)malloc(len + 1);
        Wrapped w;

        if (!wrap_text(texts[t], WRAP_WIDTH, &w) || source == NULL ||
            joined == NULL) {
            KH_CHECK(0);
            free(source);
            free(joined);
            wrap_free(&w);
            continue;
        }
        for (i = 0; i < len; i++) {
            if (texts[t][i] != '\n') {
                source[source_len++] = texts[t][i];
            }
        }
        for (i = 0; i < (size_t)w.count; i++) {
            memcpy(joined + wrapped_len, w.text + w.lines[i].start,
                   w.lines[i].length);
            wrapped_len += w.lines[i].length;
        }
        joined[wrapped_len] = 0;
        source[source_len] = 0;
        KH_CHECK_MSG(wrapped_len == source_len &&
                         memcmp(joined, source, source_len) == 0,
                     "text %zu: joining the line ranges gives %zu bytes, "
                     "the source without newlines has %zu", t, wrapped_len,
                     source_len);
        free(source);
        free(joined);
        wrap_free(&w);
    }
}

static void test_draw_lines_justify_reaches_the_column_edge(void)
{
    const float size = WRAP_SIZE;
    const float line_height = size * 1.7f;
    const float x = 30.0f;
    const float y = 30.0f;
    const float w = WRAP_WIDTH;
    Canvas canvas;
    Wrapped lines;
    int i;
    int justified = 0;

    if (!fonts_ready()) {
        return;
    }
    if (!wrap_text(k_justified_text, WRAP_WIDTH, &lines)) {
        KH_CHECK(0);
        return;
    }
    if (!canvas_init(&canvas, (int)(x + w + 2.0f * size),
                     (int)(y + line_height + 2.0f * size))) {
        KH_CHECK_MSG(0, "canvas_init failed");
        wrap_free(&lines);
        return;
    }
    for (i = 0; i < lines.count; i++) {
        int right = ink_right(&canvas, g_font, size, lines.text, lines.lines,
                              lines.count, i, x, y, w,
                              line_height + 2.0f * size, line_height);
        KH_CHECK_MSG(right >= 0, "line %d drew no ink", i);
        if (lines.lines[i].justify > 0.0f) {
            justified++;
            KH_CHECK_MSG((double)right >= (double)(x + w) - 0.5 * (double)size,
                         "justified line %d: ink ends at x=%d, expected near "
                         "the column edge x=%.0f", i, right,
                         (double)(x + w));
        } else if (paragraph_last(&lines, i)) {
            KH_CHECK_MSG((double)right < (double)(x + w) - 0.5 * (double)size,
                         "final line %d reaches x=%d, within half an em of "
                         "the column edge x=%.0f", i, right,
                         (double)(x + w));
        }
    }
    KH_CHECK_MSG(justified >= 2, "expected several justified lines to draw, "
                 "saw %d", justified);
    canvas_free(&canvas);
    wrap_free(&lines);
}

static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    long size;
    char *buf;

    if (fp == NULL) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size <= 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    buf = (char *)malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    buf[size] = 0;
    return buf;
}

static void corpus_note(char *slot, size_t capacity, const char *file, int line,
                        const char *fmt, ...)
{
    va_list ap;
    size_t used;

    if (slot[0] != 0) {
        return;
    }
    snprintf(slot, capacity, "%s:%d ", file, line);
    used = strlen(slot);
    va_start(ap, fmt);
    vsnprintf(slot + used, capacity - used, fmt, ap);
    va_end(ap);
}

static void test_article_corpus(void)
{
    DIR *dir;
    struct dirent *entry;
    const char *dir_path = "../out/probe-v2/articles";
    int files = 0;
    int lines_total = 0;
    int bad_overhang = 0;
    int bad_head = 0;
    int bad_fill = 0;
    int missing_justify = 0;
    int bad_word = 0;
    int truncated = 0;
    char overhang_ex[256] = "";
    char head_ex[256] = "";
    char fill_ex[256] = "";
    char justify_ex[256] = "";
    char word_ex[256] = "";
    char trunc_ex[256] = "";

    if (!fonts_ready()) {
        return;
    }
    dir = opendir(dir_path);
    KH_CHECK_MSG(dir != NULL, "cannot open %s (run from launcher/)", dir_path);
    if (dir == NULL) {
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        size_t name_len = strlen(entry->d_name);
        char path[4096];
        char *text;
        Wrapped w;
        int i;

        if (name_len < 5 || strcmp(entry->d_name + name_len - 4, ".txt") != 0) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        text = read_file(path);
        if (text == NULL) {
            continue;
        }
        files++;
        if (!wrap_text(text, WRAP_WIDTH, &w)) {
            free(text);
            continue;
        }
        lines_total += w.count;
        if (w.truncated) {
            truncated++;
            snprintf(trunc_ex, sizeof(trunc_ex), "%s", entry->d_name);
        }
        for (i = 0; i < w.count; i++) {
            size_t end = w.lines[i].start + w.lines[i].length;
            double natural = line_natural(&w, i);
            double avail = line_avail(&w, i, WRAP_WIDTH);
            size_t last_len = 0;
            int last;
            int cps = line_cp_count(&w, i);

            if (natural > avail + 0.01) {
                char one[8];
                double last_width;
                last = last_cp_at(text, end, &last_len);
                memcpy(one, text + end - (last_len > 0 ? last_len : 1),
                       last_len);
                one[last_len] = 0;
                last_width = (double)text_width(g_font, WRAP_SIZE,
                                                WRAP_SPACING, one);
                if (!is_hang_mark(last) ||
                    natural - last_width > avail + 0.75) {
                    bad_overhang++;
                    corpus_note(overhang_ex, sizeof(overhang_ex),
                                entry->d_name, i,
                                "overhang %.2f px in column %.2f px",
                                natural - avail, avail);
                }
            }
            if (w.lines[i].length > 0 && !paragraph_first(&w, i)) {
                size_t first_len = 0;
                int first = cp_at(text, w.lines[i].start, &first_len);
                if (is_head_forbidden(first)) {
                    bad_head++;
                    corpus_note(head_ex, sizeof(head_ex), entry->d_name, i,
                                "starts with U+%04X", first);
                }
            }
            if (w.lines[i].justify != 0.0f) {
                double filled = natural + (double)w.lines[i].justify *
                                           (double)(cps > 1 ? cps - 1 : 1);
                if (fabs(filled - avail) > 0.75) {
                    bad_fill++;
                    corpus_note(fill_ex, sizeof(fill_ex), entry->d_name, i,
                                "filled %.2f px in column %.2f px", filled,
                                avail);
                }
                if (paragraph_last(&w, i)) {
                    bad_fill++;
                    corpus_note(fill_ex, sizeof(fill_ex), entry->d_name, i,
                                "last line justified by %.2f px in column "
                                "%.2f px", (double)w.lines[i].justify, avail);
                }
            } else if (!paragraph_last(&w, i) && natural <= avail - 0.5 &&
                       avail - natural <= (double)WRAP_SIZE &&
                       cps >= 2 && paragraph_is_japanese(&w, i)) {
                last = last_cp_at(text, end, &last_len);
                if (!is_hang_mark(last)) {
                    missing_justify++;
                    corpus_note(justify_ex, sizeof(justify_ex), entry->d_name,
                                i, "natural %.2f px in column %.2f px", natural,
                                avail);
                }
            }
            if (i + 1 < w.count && end < strlen(text) &&
                text[end] != '\n') {
                size_t prev_len = 0;
                size_t next_len = 0;
                int prev = last_cp_at(text, end, &prev_len);
                int next = cp_at(text, end, &next_len);
                if (is_word_char(prev) && is_word_char(next)) {
                    size_t ws = 0;
                    size_t we = 0;
                    char word[256];
                    double word_width;
                    word_run(text, end, &ws, &we);
                    word_at(text, ws, we, word, sizeof(word));
                    word_width = (double)text_width(g_font, WRAP_SIZE,
                                                    WRAP_SPACING, word);
                    if (word_width <= (double)WRAP_WIDTH) {
                        bad_word++;
                        corpus_note(word_ex, sizeof(word_ex), entry->d_name, i,
                                "breaks \"%.60s\" (%.2f px), column %.2f px",
                                word, word_width, (double)WRAP_WIDTH);
                    }
                }
            }
        }
        wrap_free(&w);
        free(text);
    }
    closedir(dir);

    KH_CHECK_MSG(files >= 50, "expected at least 50 articles in %s, read %d",
                 dir_path, files);
    KH_CHECK_MSG(lines_total > 1000, "expected over 1000 wrapped lines, got %d",
                 lines_total);
    KH_CHECK_MSG(truncated == 0, "%d article(s) truncated: %s", truncated,
                 trunc_ex);
    KH_CHECK_MSG(bad_overhang == 0,
                 "%d line(s) overflow the column by more than one hanging "
                 "mark; first %s", bad_overhang, overhang_ex);
    KH_CHECK_MSG(bad_head == 0,
                 "%d line(s) start with a head-forbidden character; first %s",
                 bad_head, head_ex);
    KH_CHECK_MSG(bad_fill == 0,
                 "%d justified line(s) do not fill the column or should be "
                 "ragged; first %s", bad_fill, fill_ex);
    KH_CHECK_MSG(missing_justify == 0,
                 "%d Japanese line(s) that should be justified are ragged; "
                 "first %s", missing_justify, justify_ex);
    KH_CHECK_MSG(bad_word == 0,
                 "%d line break(s) cut a word that fits the column; first %s",
                 bad_word, word_ex);
}

/* Whitespace the wrapper may drop at a line break, per the spec correction. */
static int is_break_space(int cp)
{
    return cp == 0x20 || cp == 0x09 || cp == 0x3000;
}

/* True when [off, off + n) sits in the gap between two wrapped lines of the
 * same paragraph, with no paragraph break in between. */
static int at_line_break(const Wrapped *w, size_t off, size_t n)
{
    int i;
    for (i = 0; i + 1 < w->count; i++) {
        size_t end = w->lines[i].start + w->lines[i].length;
        size_t next = w->lines[i + 1].start;
        size_t k;
        int newline = 0;
        if (off < end || off + n > next) {
            continue;
        }
        for (k = end; k < next; k++) {
            if (w->text[k] == '\n') {
                newline = 1;
                break;
            }
        }
        if (!newline) {
            return 1;
        }
    }
    return 0;
}

static void test_article_corpus_keeps_every_character(void)
{
    DIR *dir;
    struct dirent *entry;
    const char *dir_path = "../out/probe-v2/articles";
    int files = 0;
    int dropped_at_break = 0;
    int lost_non_whitespace = 0;
    int lost_space_elsewhere = 0;
    int bad_sequence = 0;
    char non_ws_ex[256] = "";
    char place_ex[256] = "";
    char sequence_ex[256] = "";

    if (!fonts_ready()) {
        return;
    }
    dir = opendir(dir_path);
    KH_CHECK_MSG(dir != NULL, "cannot open %s (run from launcher/)", dir_path);
    if (dir == NULL) {
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        size_t name_len = strlen(entry->d_name);
        char path[4096];
        char *text;
        Wrapped w;
        size_t len;
        size_t i;
        unsigned char *covered;
        char *expected;
        char *joined;
        size_t off = 0;
        size_t expected_len = 0;
        size_t joined_len = 0;

        if (name_len < 5 || strcmp(entry->d_name + name_len - 4, ".txt") != 0) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        text = read_file(path);
        if (text == NULL) {
            continue;
        }
        files++;
        if (!wrap_text(text, WRAP_WIDTH, &w)) {
            free(text);
            continue;
        }
        len = strlen(text);
        covered = (unsigned char *)calloc(len, 1);
        expected = (char *)malloc(len + 1);
        joined = (char *)malloc(len + 1);
        if (covered == NULL || expected == NULL || joined == NULL) {
            free(covered);
            free(expected);
            free(joined);
            wrap_free(&w);
            free(text);
            continue;
        }
        for (i = 0; i < (size_t)w.count; i++) {
            size_t k;
            for (k = 0; k < w.lines[i].length; k++) {
                covered[w.lines[i].start + k] = 1;
            }
            memcpy(joined + joined_len, text + w.lines[i].start,
                   w.lines[i].length);
            joined_len += w.lines[i].length;
        }
        joined[joined_len] = 0;

        while (off < len) {
            size_t n = 1;
            int cp;
            size_t k;
            if (text[off] == '\n') {
                off++;
                continue;
            }
            if (covered[off]) {
                expected[expected_len++] = text[off];
                off++;
                continue;
            }
            cp = cp_at(text, off, &n);
            if (n == 0) {
                n = 1;
            }
            if (is_break_space(cp) && at_line_break(&w, off, n)) {
                dropped_at_break++;
                off += n;
                continue;
            }
            if (is_break_space(cp)) {
                lost_space_elsewhere++;
                if (place_ex[0] == 0) {
                    snprintf(place_ex, sizeof(place_ex),
                             "%s: U+%04X at source offset %zu is in no line "
                             "range and is not at a line break",
                             entry->d_name, cp, off);
                }
            } else {
                lost_non_whitespace++;
                if (non_ws_ex[0] == 0) {
                    snprintf(non_ws_ex, sizeof(non_ws_ex),
                             "%s: U+%04X at source offset %zu is in no line "
                             "range", entry->d_name, cp, off);
                }
            }
            for (k = 0; k < n; k++) {
                expected[expected_len++] = text[off + k];
            }
            off += n;
        }
        expected[expected_len] = 0;

        if (joined_len != expected_len ||
            memcmp(joined, expected, expected_len) != 0) {
            size_t at = 0;
            while (at < joined_len && at < expected_len &&
                   joined[at] == expected[at]) {
                at++;
            }
            bad_sequence++;
            if (sequence_ex[0] == 0) {
                snprintf(sequence_ex, sizeof(sequence_ex),
                         "%s: joined line ranges differ at byte %zu "
                         "(expected 0x%02X, got 0x%02X; %zu bytes joined, "
                         "%zu expected)", entry->d_name, at,
                         at < expected_len ? (unsigned char)expected[at] : 0,
                         at < joined_len ? (unsigned char)joined[at] : 0,
                         joined_len, expected_len);
            }
        }
        free(covered);
        free(expected);
        free(joined);
        wrap_free(&w);
        free(text);
    }
    closedir(dir);

    KH_CHECK_MSG(files >= 50, "expected at least 50 articles in %s, read %d",
                 dir_path, files);
    KH_CHECK_MSG(lost_non_whitespace == 0,
                 "%d non-whitespace character(s) are in no line range; first "
                 "%s", lost_non_whitespace, non_ws_ex);
    KH_CHECK_MSG(lost_space_elsewhere == 0,
                 "%d whitespace character(s) were dropped somewhere other "
                 "than a line break; first %s", lost_space_elsewhere,
                 place_ex);
    KH_CHECK_MSG(bad_sequence == 0,
                 "%d of %d article(s): the joined line ranges are not the "
                 "source minus newlines and minus the %d whitespace "
                 "character(s) dropped at line breaks; first %s", bad_sequence,
                 files, dropped_at_break, sequence_ex);
}

/* Every Japanese paragraph opens with a one-em indent, whatever its first
 * character looks like.  A numeric or Latin lead is still Japanese when the
 * paragraph holds kana or an ideograph. */

static void test_indent_numeric_led_japanese_paragraph(void)
{
    static const char *const text =
        "2026年10月24日、大型の台風が列島を横断しています。";
    Wrapped w;
    int i;

    if (!fonts_ready()) {
        return;
    }
    if (!wrap_text(text, WRAP_WIDTH, &w)) {
        KH_CHECK(0);
        return;
    }
    KH_CHECK_MSG(w.count >= 2,
                 "expected the paragraph to wrap, got %d line(s)", w.count);
    if (w.count >= 1) {
        KH_CHECK_NEAR(w.lines[0].indent, (double)WRAP_SIZE, 0.001,
                      "numeric-led Japanese paragraph indent");
    }
    for (i = 1; i < w.count; i++) {
        KH_CHECK_MSG(w.lines[i].indent == 0.0f,
                     "continuation line %d has indent %.2f, expected 0",
                     i, (double)w.lines[i].indent);
    }
    wrap_free(&w);
}

static void test_indent_latin_led_japanese_paragraph(void)
{
    static const char *const text = "AIが書いた日本語の記事です。";
    Wrapped w;

    if (!fonts_ready()) {
        return;
    }
    if (!wrap_text(text, WRAP_WIDTH, &w)) {
        KH_CHECK(0);
        return;
    }
    KH_CHECK_MSG(w.count >= 1, "expected at least one line, got %d", w.count);
    if (w.count >= 1) {
        KH_CHECK_NEAR(w.lines[0].indent, (double)WRAP_SIZE, 0.001,
                      "Latin-led Japanese paragraph indent");
    }
    wrap_free(&w);
}

static void test_indent_latin_only_paragraph_stays_flush(void)
{
    static const char *const text = "123 Main Street";
    Wrapped w;

    if (!fonts_ready()) {
        return;
    }
    if (!wrap_text(text, WRAP_WIDTH, &w)) {
        KH_CHECK(0);
        return;
    }
    KH_CHECK_MSG(w.count >= 1, "expected at least one line, got %d", w.count);
    if (w.count >= 1) {
        KH_CHECK_MSG(w.lines[0].indent == 0.0f,
                     "Latin-only paragraph indent is %.2f, expected 0",
                     (double)w.lines[0].indent);
    }
    wrap_free(&w);
}

static void test_indent_every_paragraph(void)
{
    static const char *const text =
        "1つ目の段落です。\n2つ目の段落です。\nAIが書いた段落です。\n"
        "Latin text";
    static const int japanese[] = { 1, 1, 1, 0 };
    Wrapped w;
    int paragraph = 0;
    int i;

    if (!fonts_ready()) {
        return;
    }
    if (!wrap_text(text, WRAP_WIDTH, &w)) {
        KH_CHECK(0);
        return;
    }
    for (i = 0; i < w.count; i++) {
        if (!paragraph_first(&w, i)) {
            continue;
        }
        KH_CHECK_MSG(paragraph < 4, "more than 4 paragraphs in the fixture");
        if (paragraph >= 4) {
            break;
        }
        if (japanese[paragraph]) {
            KH_CHECK_NEAR(w.lines[i].indent, (double)WRAP_SIZE, 0.001,
                          "Japanese paragraph indent");
        } else {
            KH_CHECK_MSG(w.lines[i].indent == 0.0f,
                         "Latin paragraph indent is %.2f, expected 0",
                         (double)w.lines[i].indent);
        }
        paragraph++;
    }
    KH_CHECK_MSG(paragraph == 4,
                 "expected 4 paragraphs, saw %d", paragraph);
    wrap_free(&w);
}

void kh_run_text_wrap_tests(void)
{
    printf("\n[text-wrap]\n");
    g_fonts_loaded = load_fonts();
    kh_run_test("text.wrap_overhang_only_hanging_punctuation",
                test_overhang_only_hanging_punctuation);
    kh_run_test("text.wrap_hang_punctuation_stays_with_previous_line",
                test_hang_punctuation_stays_with_previous_line);
    kh_run_test("text.wrap_head_forbidden_never_starts_a_line",
                test_head_forbidden_never_starts_a_line);
    kh_run_test("text.wrap_head_forbidden_pulls_previous_character",
                test_head_forbidden_pulls_previous_character);
    kh_run_test("text.wrap_head_forbidden_run_pulls_together",
                test_head_forbidden_run_pulls_together);
    kh_run_test("text.wrap_head_forbidden_keeps_word_whole",
                test_head_forbidden_keeps_word_whole);
    kh_run_test("text.wrap_justify_fills_nonfinal_lines",
                test_justify_fills_nonfinal_lines);
    kh_run_test("text.wrap_justify_zero_on_paragraph_last_line",
                test_justify_zero_on_paragraph_last_line);
    kh_run_test("text.wrap_justify_zero_when_gaps_would_open_too_wide",
                test_justify_zero_when_gaps_would_open_too_wide);
    kh_run_test("text.wrap_latin_word_breaks_before_the_word",
                test_latin_word_breaks_before_the_word);
    kh_run_test("text.wrap_latin_word_is_not_split",
                test_latin_word_is_not_split);
    kh_run_test("text.wrap_long_word_is_split", test_long_word_is_split);
    kh_run_test("text.wrap_fullwidth_alnum_is_a_word",
                test_fullwidth_alnum_is_a_word);
    kh_run_test("text.wrap_every_character_appears_once",
                test_every_character_appears_once);
    kh_run_test("text.wrap_indent_numeric_led_japanese_paragraph",
                test_indent_numeric_led_japanese_paragraph);
    kh_run_test("text.wrap_indent_latin_led_japanese_paragraph",
                test_indent_latin_led_japanese_paragraph);
    kh_run_test("text.wrap_indent_latin_only_paragraph_stays_flush",
                test_indent_latin_only_paragraph_stays_flush);
    kh_run_test("text.wrap_indent_every_paragraph",
                test_indent_every_paragraph);
    kh_run_test("text.draw_lines_justify_reaches_the_column_edge",
                test_draw_lines_justify_reaches_the_column_edge);
    kh_run_test("text.wrap_article_corpus", test_article_corpus);
    kh_run_test("text.wrap_article_corpus_keeps_every_character",
                test_article_corpus_keeps_every_character);
    if (g_fonts_loaded) {
        fonts_free(&g_fonts);
    }
}
