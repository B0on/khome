#include "suites.h"

#include <stdlib.h>
#include <string.h>

#include "test_harness.h"
#include "text.h"

/* These tests only use the public text.h API: fonts_load / fonts_get,
 * text_width and text_ellipsize.  Favorite-name typography is SerifB 36 px
 * with 1 px letter-spacing and a 256 px name box (SPEC 2.5 / 7.3). */

static Fonts g_fonts;
static const Font *g_serif_b;
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
            g_serif_b = fonts_get(&g_fonts, FONT_SERIF_B);
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

static int utf8_cp_count(const char *s)
{
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p != 0) {
        if ((*p & 0xC0) != 0x80) {
            n++;
        }
        p++;
    }
    return n;
}

static int ends_with_ellipsis(const char *s)
{
    size_t n = strlen(s);
    return n >= 3 && memcmp(s + n - 3, "\xE2\x80\xA6", 3) == 0;
}

static size_t utf8_seq_len(unsigned char lead)
{
    if (lead < 0x80) {
        return 1;
    }
    if ((lead & 0xE0) == 0xC0) {
        return 2;
    }
    if ((lead & 0xF0) == 0xE0) {
        return 3;
    }
    if ((lead & 0xF8) == 0xF0) {
        return 4;
    }
    return 1;
}

/* The ellipsizer must be maximal: putting back the first character it dropped
 * (before the ellipsis) must exceed max_width. */
static int ellipsis_is_maximal(const Font *font, float size, float ls,
                               const char *src, const char *out,
                               float max_width)
{
    size_t src_len = strlen(src);
    size_t prefix_len;
    size_t seq;
    char trial[512];
    size_t trial_len;

    if (!ends_with_ellipsis(out)) {
        return 0;
    }
    prefix_len = strlen(out) - 3;
    if (prefix_len >= src_len) {
        return 0;
    }
    if (memcmp(out, src, prefix_len) != 0) {
        return 0;
    }
    seq = utf8_seq_len((unsigned char)src[prefix_len]);
    if (prefix_len + seq > src_len) {
        return 0;
    }
    trial_len = prefix_len + seq;
    if (trial_len + 3 >= sizeof(trial)) {
        return 0;
    }
    memcpy(trial, src, trial_len);
    memcpy(trial + trial_len, "\xE2\x80\xA6", 3);
    trial[trial_len + 3] = 0;
    return text_width(font, size, ls, trial) > max_width;
}

static void check_monotonic(const char *const *steps, int count,
                            const char *label)
{
    float prev = -1.0f;
    int i;
    for (i = 0; i < count; i++) {
        float w = text_width(g_serif_b, 36.0f, 1.0f, steps[i]);
        KH_CHECK_MSG(w > prev, "%s width not increasing at \"%s\": "
                     "%.2f is not > %.2f", label, steps[i], w, prev);
        prev = w;
    }
}

static void test_width_monotonic_japanese(void)
{
    static const char *const steps[] = {
        "は", "はて", "はてな", "はてなブ", "はてなブッ", "はてなブック",
        "はてなブックマ"
    };
    if (!fonts_ready()) {
        return;
    }
    check_monotonic(steps, (int)(sizeof(steps) / sizeof(steps[0])),
                    "Japanese");
}

static void test_width_monotonic_latin(void)
{
    static const char *const steps[] = {
        "W", "Wi", "Wik", "Wiki", "Wikip", "Wikipe", "Wikiped"
    };
    if (!fonts_ready()) {
        return;
    }
    check_monotonic(steps, (int)(sizeof(steps) / sizeof(steps[0])), "Latin");
}

static void test_width_monotonic_mixed(void)
{
    static const char *const steps[] = {
        "A", "Aあ", "AあB", "AあBい", "AあBいC"
    };
    if (!fonts_ready()) {
        return;
    }
    check_monotonic(steps, (int)(sizeof(steps) / sizeof(steps[0])), "Mixed");
}

static void test_width_includes_letter_spacing(void)
{
    static const char *const texts[] = { "abc", "はてな", "AあB" };
    static const float spacings[] = { 1.0f, 2.0f, 4.0f, 8.0f };
    size_t t;
    size_t s;
    if (!fonts_ready()) {
        return;
    }
    for (t = 0; t < sizeof(texts) / sizeof(texts[0]); t++) {
        int gaps = utf8_cp_count(texts[t]) - 1;
        float base = text_width(g_serif_b, 36.0f, 0.0f, texts[t]);
        float prev = base;
        for (s = 0; s < sizeof(spacings) / sizeof(spacings[0]); s++) {
            float w = text_width(g_serif_b, 36.0f, spacings[s], texts[t]);
            KH_CHECK_MSG(w > prev,
                         "\"%s\": width with spacing %.1f (%.2f) is not "
                         "greater than the previous value (%.2f)",
                         texts[t], spacings[s], w, prev);
            /* Letter-spacing applies between characters only, never after the
             * last one (SPEC 7.3). */
            KH_CHECK_NEAR(w - base, (double)spacings[s] * (double)gaps, 0.01,
                          "letter-spacing contribution");
            prev = w;
        }
    }
}

static void test_width_no_trailing_letter_spacing(void)
{
    if (!fonts_ready()) {
        return;
    }
    KH_CHECK_NEAR(text_width(g_serif_b, 36.0f, 8.0f, "は"),
                  text_width(g_serif_b, 36.0f, 0.0f, "は"), 0.001,
                  "single Japanese glyph must ignore letter-spacing");
    KH_CHECK_NEAR(text_width(g_serif_b, 36.0f, 8.0f, "W"),
                  text_width(g_serif_b, 36.0f, 0.0f, "W"), 0.001,
                  "single Latin glyph must ignore letter-spacing");
}

static void test_ellipsize_japanese_spec_example(void)
{
    char out[512];
    const char *src = "はてなブックマーク";
    float max_width = 256.0f;
    if (!fonts_ready()) {
        return;
    }
    text_ellipsize(g_serif_b, 36.0f, 1.0f, src, max_width, out, sizeof(out));
    KH_CHECK_MSG(strcmp(out, "はてなブック…") == 0,
                 "SPEC 2.5 example: expected \"はてなブック…\", got \"%s\"",
                 out);
    KH_CHECK_MSG(text_width(g_serif_b, 36.0f, 1.0f, out) <= max_width,
                 "ellipsized Japanese name is %.2f px, over the 256 px box",
                 text_width(g_serif_b, 36.0f, 1.0f, out));
    KH_CHECK_MSG(ellipsis_is_maximal(g_serif_b, 36.0f, 1.0f, src, out,
                                     max_width),
                 "\"%s\" is not the longest prefix that fits with an ellipsis",
                 out);
}

static void test_ellipsize_latin(void)
{
    char out[512];
    const char *src = "A very long favorite name indeed";
    float max_width = 256.0f;
    if (!fonts_ready()) {
        return;
    }
    text_ellipsize(g_serif_b, 36.0f, 1.0f, src, max_width, out, sizeof(out));
    KH_CHECK_MSG(ends_with_ellipsis(out),
                 "long Latin name does not end in U+2026: \"%s\"", out);
    KH_CHECK_MSG(strlen(out) < strlen(src),
                 "long Latin name was not shortened: \"%s\"", out);
    KH_CHECK_MSG(text_width(g_serif_b, 36.0f, 1.0f, out) <= max_width,
                 "ellipsized Latin name is %.2f px, over the 256 px box",
                 text_width(g_serif_b, 36.0f, 1.0f, out));
    KH_CHECK_MSG(ellipsis_is_maximal(g_serif_b, 36.0f, 1.0f, src, out,
                                     max_width),
                 "\"%s\" is not the longest prefix that fits with an ellipsis",
                 out);
}

static void test_ellipsize_mixed(void)
{
    char out[512];
    const char *src = "Kindle 漫画 ニュース";
    float max_width = 256.0f;
    if (!fonts_ready()) {
        return;
    }
    text_ellipsize(g_serif_b, 36.0f, 1.0f, src, max_width, out, sizeof(out));
    KH_CHECK_MSG(ends_with_ellipsis(out),
                 "long mixed name does not end in U+2026: \"%s\"", out);
    KH_CHECK_MSG(strlen(out) < strlen(src),
                 "long mixed name was not shortened: \"%s\"", out);
    KH_CHECK_MSG(text_width(g_serif_b, 36.0f, 1.0f, out) <= max_width,
                 "ellipsized mixed name is %.2f px, over the 256 px box",
                 text_width(g_serif_b, 36.0f, 1.0f, out));
    KH_CHECK_MSG(ellipsis_is_maximal(g_serif_b, 36.0f, 1.0f, src, out,
                                     max_width),
                 "\"%s\" is not the longest prefix that fits with an ellipsis",
                 out);
}

static void test_ellipsize_short_names_unchanged(void)
{
    static const char *const names[] = {
        "Zenn", "Hacker News", "Wikipedia", "GitHub", "NHK NEWS", "短い名前"
    };
    char out[512];
    size_t i;
    if (!fonts_ready()) {
        return;
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        float w = text_width(g_serif_b, 36.0f, 1.0f, names[i]);
        text_ellipsize(g_serif_b, 36.0f, 1.0f, names[i], 256.0f, out,
                       sizeof(out));
        KH_CHECK_MSG(strcmp(out, names[i]) == 0,
                     "\"%s\" (%.2f px) fits in 256 px but became \"%s\"",
                     names[i], w, out);
    }
}

void kh_run_text_tests(void)
{
    printf("\n[text]\n");
    g_fonts_loaded = load_fonts();
    kh_run_test("text.width_monotonic_japanese",
                test_width_monotonic_japanese);
    kh_run_test("text.width_monotonic_latin", test_width_monotonic_latin);
    kh_run_test("text.width_monotonic_mixed", test_width_monotonic_mixed);
    kh_run_test("text.width_includes_letter_spacing",
                test_width_includes_letter_spacing);
    kh_run_test("text.width_no_trailing_letter_spacing",
                test_width_no_trailing_letter_spacing);
    kh_run_test("text.ellipsize_japanese_spec_example",
                test_ellipsize_japanese_spec_example);
    kh_run_test("text.ellipsize_latin", test_ellipsize_latin);
    kh_run_test("text.ellipsize_mixed", test_ellipsize_mixed);
    kh_run_test("text.ellipsize_short_names_unchanged",
                test_ellipsize_short_names_unchanged);
    if (g_fonts_loaded) {
        fonts_free(&g_fonts);
    }
}
