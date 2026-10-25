#include "suites.h"

#include <string.h>

#include "tabs.h"
#include "test_harness.h"

/* sqlite3's hex() hands the browser's LocalStorage text back as UTF-16LE code
 * units.  These tests pin the UTF-8 conversion and the "urlhex titlehex" row
 * split that feeds the tab sheet. */

#define OUT_CAP 64

/* "Hello" in UTF-16LE: 4800 6500 6c00 6c00 6f00. */
static const char ASCII_HELLO_HEX[] = "480065006c006c006f00";

/* U+3042 HIRAGANA LETTER A. */
static const char HIRAGANA_A_HEX[] = "4230";

/* U+1F600 GRINNING FACE, the surrogate pair D83D DE00. */
static const char GRINNING_HEX[] = "3dd800de";

/* "ab" in UTF-16LE: 6100 6200. */
static const char URL_AB_HEX[] = "61006200";

/* "Hi" in UTF-16LE: 4800 6900. */
static const char TITLE_HI_HEX[] = "48006900";

static void expect_decoded(const char *label, const char *hex,
                           const char *expected, size_t cap)
{
    char out[OUT_CAP];
    int got;

    memset(out, 'x', sizeof(out));
    got = tabs_utf16_hex_decode(hex, out, cap);
    KH_CHECK_MSG(got == (int)strlen(expected),
                 "%s: expected length %d, got %d", label,
                 (int)strlen(expected), got);
    KH_CHECK_MSG(strcmp(out, expected) == 0,
                 "%s: expected \"%s\", got \"%s\"", label, expected, out);
}

static void expect_decode_failure(const char *label, const char *hex,
                                  size_t cap)
{
    char out[OUT_CAP];
    int got;

    memset(out, 'x', sizeof(out));
    got = tabs_utf16_hex_decode(hex, out, cap);
    out[OUT_CAP - 1] = 0;
    KH_CHECK_MSG(got == -1, "%s: expected -1, got %d", label, got);
    if (cap > 0) {
        KH_CHECK_MSG(out[0] == 0,
                     "%s: a failure must leave an empty string, got first "
                     "byte %d (\"%.8s\")",
                     label, (int)(unsigned char)out[0], out);
    }
}

static void test_decode_ascii(void)
{
    expect_decoded("ASCII Hello", ASCII_HELLO_HEX, "Hello", OUT_CAP);
    expect_decoded("uppercase hex digits", "4100", "A", OUT_CAP);
    expect_decoded("lowercase hex digits", "6100", "a", OUT_CAP);
}

static void test_decode_hiragana(void)
{
    expect_decoded("U+3042", HIRAGANA_A_HEX, "\xE3\x81\x82", OUT_CAP);
}

static void test_decode_surrogate_pair(void)
{
    expect_decoded("U+1F600", GRINNING_HEX, "\xF0\x9F\x98\x80", OUT_CAP);
    expect_decoded("uppercase surrogate pair", "3DD800DE",
                   "\xF0\x9F\x98\x80", OUT_CAP);
    expect_decoded("mixed ASCII, kana, emoji", "423041003dd800de",
                   "\xE3\x81\x82" "A" "\xF0\x9F\x98\x80", OUT_CAP);
}

static void test_decode_rejects_malformed_input(void)
{
    expect_decode_failure("odd digit count", "410", OUT_CAP);
    expect_decode_failure("odd byte count", "41000", OUT_CAP);
    expect_decode_failure("half a code unit", "41", OUT_CAP);
    expect_decode_failure("not a hex digit", "41G0", OUT_CAP);
    expect_decode_failure("letters outside hex", "zzzz", OUT_CAP);
}

static void test_decode_rejects_unpaired_surrogates(void)
{
    expect_decode_failure("high surrogate alone", "3dd8", OUT_CAP);
    expect_decode_failure("low surrogate alone", "00de", OUT_CAP);
    expect_decode_failure("high surrogate then ASCII", "3dd84100", OUT_CAP);
    expect_decode_failure("high surrogate then high surrogate", "3dd83dd8",
                          OUT_CAP);
}

static void test_decode_rejects_nul_code_unit(void)
{
    expect_decode_failure("NUL only", "0000", OUT_CAP);
    expect_decode_failure("NUL inside text", "410000006100", OUT_CAP);
}

static void test_decode_rejects_short_output_buffer(void)
{
    expect_decode_failure("no room for the NUL", "4100", 1);
    expect_decode_failure("no room for a 3-byte kana", HIRAGANA_A_HEX, 3);
    expect_decode_failure("no room for a 4-byte emoji", GRINNING_HEX, 4);
    expect_decode_failure("zero capacity", "4100", 0);
    KH_CHECK_MSG(tabs_utf16_hex_decode("4100", NULL, OUT_CAP) == -1,
                 "NULL output: expected -1");
    KH_CHECK_MSG(tabs_utf16_hex_decode(NULL, NULL, 0) == -1,
                 "NULL input: expected -1");

    expect_decoded("exactly one byte plus NUL", "4100", "A", 2);
    expect_decoded("exactly three bytes plus NUL", HIRAGANA_A_HEX,
                   "\xE3\x81\x82", 4);
    expect_decoded("exactly four bytes plus NUL", GRINNING_HEX,
                   "\xF0\x9F\x98\x80", 5);
}

static void expect_parse_ok(const char *label, const char *row,
                            size_t url_cap, size_t title_cap,
                            const char *expected_url,
                            const char *expected_title)
{
    char url[OUT_CAP];
    char title[OUT_CAP];
    int got;

    memset(url, 'x', sizeof(url));
    memset(title, 'x', sizeof(title));
    got = tabs_browser_row_parse(row, url, url_cap, title, title_cap);
    KH_CHECK_MSG(got == 0, "%s: expected 0, got %d", label, got);
    KH_CHECK_MSG(strcmp(url, expected_url) == 0,
                 "%s: expected URL \"%s\", got \"%s\"", label, expected_url,
                 url);
    KH_CHECK_MSG(strcmp(title, expected_title) == 0,
                 "%s: expected title \"%s\", got \"%s\"", label,
                 expected_title, title);
}

static void expect_parse_failure(const char *label, const char *row)
{
    char url[OUT_CAP];
    char title[OUT_CAP];
    int got;

    memset(url, 'x', sizeof(url));
    memset(title, 'x', sizeof(title));
    got = tabs_browser_row_parse(row, url, OUT_CAP, title, OUT_CAP);
    KH_CHECK_MSG(got == -1, "%s: expected -1, got %d", label, got);
    KH_CHECK_MSG(url[0] == 0,
                 "%s: a failure must leave the URL empty, got \"%s\"", label,
                 url);
    KH_CHECK_MSG(title[0] == 0,
                 "%s: a failure must leave the title empty, got \"%s\"",
                 label, title);
}

static void test_parse_splits_and_decodes(void)
{
    char row[128];

    snprintf(row, sizeof(row), "%s %s", URL_AB_HEX, TITLE_HI_HEX);
    expect_parse_ok("ASCII URL and title", row, OUT_CAP, OUT_CAP, "ab",
                    "Hi");

    snprintf(row, sizeof(row), "%s %s", URL_AB_HEX, HIRAGANA_A_HEX);
    expect_parse_ok("kana title", row, OUT_CAP, OUT_CAP, "ab",
                    "\xE3\x81\x82");

    snprintf(row, sizeof(row), "%s %s", URL_AB_HEX, GRINNING_HEX);
    expect_parse_ok("emoji title", row, OUT_CAP, OUT_CAP, "ab",
                    "\xF0\x9F\x98\x80");

    snprintf(row, sizeof(row), "%s ", URL_AB_HEX);
    expect_parse_ok("empty title field", row, OUT_CAP, OUT_CAP, "ab", "");
}

static void test_parse_collapses_title_whitespace(void)
{
    char row[128];

    /* "  A \t  B  " in UTF-16LE, with runs of spaces and a tab. */
    snprintf(row, sizeof(row), "%s 2000200041002000090020002000420020002000",
             URL_AB_HEX);
    expect_parse_ok("collapsed runs and trimmed ends", row, OUT_CAP, OUT_CAP,
                    "ab", "A B");

    /* " A " around the title. */
    snprintf(row, sizeof(row), "%s 200041002000", URL_AB_HEX);
    expect_parse_ok("trimmed ends", row, OUT_CAP, OUT_CAP, "ab", "A");
}

static void test_parse_title_ends_at_line_break(void)
{
    char row[128];

    snprintf(row, sizeof(row), "%s %s\r\n", URL_AB_HEX, TITLE_HI_HEX);
    expect_parse_ok("CRLF ends the title", row, OUT_CAP, OUT_CAP, "ab",
                    "Hi");

    snprintf(row, sizeof(row), "%s %s\n", URL_AB_HEX, TITLE_HI_HEX);
    expect_parse_ok("LF ends the title", row, OUT_CAP, OUT_CAP, "ab", "Hi");
}

static void test_parse_rejects_a_row_without_a_separator(void)
{
    expect_parse_failure("no space", "6100620048006900");
    expect_parse_failure("empty row", "");
}

static void test_parse_rejects_a_bad_url(void)
{
    expect_parse_failure("URL is not hex", "zzzz 48006900");
    expect_parse_failure("URL has an odd length", "610 48006900");
    expect_parse_failure("URL has a NUL code unit", "0000 48006900");
    expect_parse_failure("URL has an unpaired surrogate", "3dd8 48006900");
}

static void test_parse_rejects_an_empty_url(void)
{
    expect_parse_failure("row starts with the separator", " 48006900");
}

static void test_parse_keeps_the_url_when_the_title_is_bad(void)
{
    char url[OUT_CAP];
    char title[OUT_CAP];
    int got;

    memset(url, 'x', sizeof(url));
    memset(title, 'x', sizeof(title));
    got = tabs_browser_row_parse("61006200 zz", url, sizeof(url), title,
                                 sizeof(title));
    KH_CHECK_MSG(got == 0, "bad title: expected 0, got %d", got);
    KH_CHECK_MSG(strcmp(url, "ab") == 0,
                 "bad title: expected URL \"ab\", got \"%s\"", url);
    KH_CHECK_MSG(title[0] == 0,
                 "bad title: expected an empty title, got \"%s\"", title);

    memset(url, 'x', sizeof(url));
    memset(title, 'x', sizeof(title));
    got = tabs_browser_row_parse("61006200 4800xx00", url, sizeof(url),
                                 title, sizeof(title));
    KH_CHECK_MSG(got == 0, "partly bad title: expected 0, got %d", got);
    KH_CHECK_MSG(strcmp(url, "ab") == 0,
                 "partly bad title: expected URL \"ab\", got \"%s\"", url);
    KH_CHECK_MSG(title[0] == 0,
                 "partly bad title: expected an empty title, got \"%s\"",
                 title);

    memset(url, 'x', sizeof(url));
    memset(title, 'x', sizeof(title));
    got = tabs_browser_row_parse("61006200 48006900", url, sizeof(url), title,
                                 2);
    KH_CHECK_MSG(got == 0, "title too large for title_cap: expected 0, got %d",
                 got);
    KH_CHECK_MSG(strcmp(url, "ab") == 0,
                 "title too large for title_cap: expected URL \"ab\", got "
                 "\"%s\"", url);
    KH_CHECK_MSG(title[0] == 0,
                 "title too large for title_cap: expected an empty title, "
                 "got \"%s\"", title);
}

static void test_parse_rejects_a_url_that_does_not_fit(void)
{
    char url[OUT_CAP];
    char title[OUT_CAP];
    int got;

    memset(url, 'x', sizeof(url));
    memset(title, 'x', sizeof(title));
    got = tabs_browser_row_parse("61006200 48006900", url, 2, title,
                                 sizeof(title));
    KH_CHECK_MSG(got == -1,
                 "URL too large for url_cap: expected -1, got %d", got);
    KH_CHECK_MSG(url[0] == 0,
                 "URL too large for url_cap: expected an empty URL, got "
                 "\"%s\"", url);
    KH_CHECK_MSG(title[0] == 0,
                 "URL too large for url_cap: expected an empty title, got "
                 "\"%s\"", title);
}

/* The stock browser keeps its display state in the LocalStorage "settings"
 * object.  Only "landscapeLeft" means landscape; "auto", a missing key, an
 * unknown value, or malformed JSON must all fall back to portrait. */

static const char *orientation_name(BrowserOrientation orientation)
{
    return orientation == BROWSER_ORIENTATION_LANDSCAPE ? "landscape"
                                                        : "portrait";
}

static void expect_orientation(const char *label, const char *json,
                               BrowserOrientation expected)
{
    BrowserOrientation got = tabs_browser_orientation(json);

    KH_CHECK_MSG(got == expected, "%s: expected %s, got %s", label,
                 orientation_name(expected), orientation_name(got));
}

static void test_orientation_auto_is_portrait(void)
{
    expect_orientation("canonical settings",
                       "{\"javascriptDisabled\":false,"
                       "\"imagesDisabled\":false,\"orientation\":\"auto\"}",
                       BROWSER_ORIENTATION_PORTRAIT);
}

static void test_orientation_landscape_left_is_landscape(void)
{
    expect_orientation("landscapeLeft",
                       "{\"javascriptDisabled\":false,"
                       "\"imagesDisabled\":false,"
                       "\"orientation\":\"landscapeLeft\"}",
                       BROWSER_ORIENTATION_LANDSCAPE);
}

static void test_orientation_missing_key_is_portrait(void)
{
    expect_orientation("no orientation key",
                       "{\"javascriptDisabled\":false,"
                       "\"imagesDisabled\":false}",
                       BROWSER_ORIENTATION_PORTRAIT);
    expect_orientation("empty object", "{}", BROWSER_ORIENTATION_PORTRAIT);
}

static void test_orientation_unknown_values_are_portrait(void)
{
    expect_orientation("portrait", "{\"orientation\":\"portrait\"}",
                       BROWSER_ORIENTATION_PORTRAIT);
    expect_orientation("landscapeRight",
                       "{\"orientation\":\"landscapeRight\"}",
                       BROWSER_ORIENTATION_PORTRAIT);
    expect_orientation("empty value", "{\"orientation\":\"\"}",
                       BROWSER_ORIENTATION_PORTRAIT);
    expect_orientation("landscape", "{\"orientation\":\"landscape\"}",
                       BROWSER_ORIENTATION_PORTRAIT);
    expect_orientation("different case",
                       "{\"orientation\":\"LANDSCAPELEFT\"}",
                       BROWSER_ORIENTATION_PORTRAIT);
}

static void test_orientation_null_is_portrait(void)
{
    expect_orientation("NULL", NULL, BROWSER_ORIENTATION_PORTRAIT);
}

static void test_orientation_malformed_json_is_portrait(void)
{
    expect_orientation("empty string", "", BROWSER_ORIENTATION_PORTRAIT);
    expect_orientation("not an object", "\"auto\"",
                       BROWSER_ORIENTATION_PORTRAIT);
    expect_orientation("truncated object", "{", BROWSER_ORIENTATION_PORTRAIT);
    expect_orientation("unterminated key", "{\"orient",
                       BROWSER_ORIENTATION_PORTRAIT);
    expect_orientation("missing colon",
                       "{\"orientation\" \"landscapeLeft\"}",
                       BROWSER_ORIENTATION_PORTRAIT);
    expect_orientation("missing value", "{\"orientation\":}",
                       BROWSER_ORIENTATION_PORTRAIT);
    expect_orientation("unterminated value",
                       "{\"orientation\":\"landscapeLeft",
                       BROWSER_ORIENTATION_PORTRAIT);
    expect_orientation("missing closing brace after auto",
                       "{\"javascriptDisabled\":false,"
                       "\"imagesDisabled\":false,\"orientation\":\"auto\"",
                       BROWSER_ORIENTATION_PORTRAIT);
}

static void test_orientation_malformed_landscape_is_portrait(void)
{
    /* The whole object is broken, so it must not count as a landscape
     * setting even though the value reads "landscapeLeft". */
    expect_orientation("missing closing brace after landscapeLeft",
                       "{\"orientation\":\"landscapeLeft\"",
                       BROWSER_ORIENTATION_PORTRAIT);
}

static void test_orientation_accepts_whitespace(void)
{
    expect_orientation("spaces around every token",
                       "{ \"javascriptDisabled\" : false , "
                       "\"imagesDisabled\" : false , "
                       "\"orientation\" : \"landscapeLeft\" }",
                       BROWSER_ORIENTATION_LANDSCAPE);
    expect_orientation("newlines and tabs",
                       "{\n\t\"javascriptDisabled\": false,\n"
                       "\t\"imagesDisabled\": false,\n"
                       "\t\"orientation\": \"auto\"\n}\n",
                       BROWSER_ORIENTATION_PORTRAIT);
    expect_orientation("newline before the value",
                       "{\n  \"orientation\" :\n  \"landscapeLeft\"\n}\n",
                       BROWSER_ORIENTATION_LANDSCAPE);
}

static void test_orientation_key_order_and_decoys(void)
{
    expect_orientation("orientation first",
                       "{\"orientation\":\"landscapeLeft\","
                       "\"javascriptDisabled\":false,"
                       "\"imagesDisabled\":false}",
                       BROWSER_ORIENTATION_LANDSCAPE);
    expect_orientation("the word orientation inside another value",
                       "{\"note\":\"orientation landscapeLeft\","
                       "\"orientation\":\"auto\"}",
                       BROWSER_ORIENTATION_PORTRAIT);
    expect_orientation("the value landscapeLeft inside another value",
                       "{\"note\":\"landscapeLeft\","
                       "\"orientation\":\"auto\"}",
                       BROWSER_ORIENTATION_PORTRAIT);
    expect_orientation("decoy value with a landscape orientation",
                       "{\"note\":\"orientation landscapeLeft\","
                       "\"orientation\":\"landscapeLeft\"}",
                       BROWSER_ORIENTATION_LANDSCAPE);
    expect_orientation("similar key name",
                       "{\"orientationHint\":\"landscapeLeft\","
                       "\"orientation\":\"auto\"}",
                       BROWSER_ORIENTATION_PORTRAIT);
    expect_orientation("nested object before orientation",
                       "{\"extra\":{\"orientation\":\"landscapeLeft\"},"
                       "\"orientation\":\"auto\"}",
                       BROWSER_ORIENTATION_PORTRAIT);
}

static void test_orientation_value_names(void)
{
    const char *portrait = tabs_browser_orientation_value(
        BROWSER_ORIENTATION_PORTRAIT);
    const char *landscape = tabs_browser_orientation_value(
        BROWSER_ORIENTATION_LANDSCAPE);

    KH_CHECK_MSG(strcmp(portrait, "auto") == 0,
                 "portrait: expected \"auto\", got \"%s\"", portrait);
    KH_CHECK_MSG(strcmp(landscape, "landscapeLeft") == 0,
                 "landscape: expected \"landscapeLeft\", got \"%s\"",
                 landscape);
}

void kh_run_browser_orientation_tests(void)
{
    printf("\n[browser-orientation]\n");
    kh_run_test("browser_orientation.auto_is_portrait",
                test_orientation_auto_is_portrait);
    kh_run_test("browser_orientation.landscape_left_is_landscape",
                test_orientation_landscape_left_is_landscape);
    kh_run_test("browser_orientation.missing_key_is_portrait",
                test_orientation_missing_key_is_portrait);
    kh_run_test("browser_orientation.unknown_values_are_portrait",
                test_orientation_unknown_values_are_portrait);
    kh_run_test("browser_orientation.null_is_portrait",
                test_orientation_null_is_portrait);
    kh_run_test("browser_orientation.malformed_json_is_portrait",
                test_orientation_malformed_json_is_portrait);
    kh_run_test("browser_orientation.malformed_landscape_is_portrait",
                test_orientation_malformed_landscape_is_portrait);
    kh_run_test("browser_orientation.accepts_whitespace",
                test_orientation_accepts_whitespace);
    kh_run_test("browser_orientation.key_order_and_decoys",
                test_orientation_key_order_and_decoys);
    kh_run_test("browser_orientation.value_names",
                test_orientation_value_names);
}

void kh_run_browser_row_tests(void)
{
    printf("\n[browser-row]\n");
    kh_run_test("browser_row.decode_ascii", test_decode_ascii);
    kh_run_test("browser_row.decode_hiragana", test_decode_hiragana);
    kh_run_test("browser_row.decode_surrogate_pair",
                test_decode_surrogate_pair);
    kh_run_test("browser_row.decode_rejects_malformed_input",
                test_decode_rejects_malformed_input);
    kh_run_test("browser_row.decode_rejects_unpaired_surrogates",
                test_decode_rejects_unpaired_surrogates);
    kh_run_test("browser_row.decode_rejects_nul_code_unit",
                test_decode_rejects_nul_code_unit);
    kh_run_test("browser_row.decode_rejects_short_output_buffer",
                test_decode_rejects_short_output_buffer);
    kh_run_test("browser_row.parse_splits_and_decodes",
                test_parse_splits_and_decodes);
    kh_run_test("browser_row.parse_collapses_title_whitespace",
                test_parse_collapses_title_whitespace);
    kh_run_test("browser_row.parse_title_ends_at_line_break",
                test_parse_title_ends_at_line_break);
    kh_run_test("browser_row.parse_rejects_a_row_without_a_separator",
                test_parse_rejects_a_row_without_a_separator);
    kh_run_test("browser_row.parse_rejects_a_bad_url",
                test_parse_rejects_a_bad_url);
    kh_run_test("browser_row.parse_rejects_an_empty_url",
                test_parse_rejects_an_empty_url);
    kh_run_test("browser_row.parse_keeps_the_url_when_the_title_is_bad",
                test_parse_keeps_the_url_when_the_title_is_bad);
    kh_run_test("browser_row.parse_rejects_a_url_that_does_not_fit",
                test_parse_rejects_a_url_that_does_not_fit);
}
