#include "suites.h"

#include <string.h>

#include "test_harness.h"
#include "urledit.h"

/* UrlEdit is the value behind the full-screen URL editor.  These tests drive
 * the public urledit.h API and assert the text, length and cursor a caller can
 * read back, plus the normalized string that the browser receives. */

static void expect_edit(const char *label, const UrlEdit *edit,
                        const char *text, size_t cursor)
{
    size_t len = strlen(text);

    KH_CHECK_MSG(edit->len == len, "%s: expected len %d, got %d", label,
                 (int)len, (int)edit->len);
    KH_CHECK_MSG(edit->cursor == cursor, "%s: expected cursor %d, got %d",
                 label, (int)cursor, (int)edit->cursor);
    KH_CHECK_MSG(strcmp(edit->buf, text) == 0,
                 "%s: expected \"%s\", got \"%s\"", label, text, edit->buf);
    KH_CHECK_MSG(edit->buf[edit->len] == 0,
                 "%s: buffer is not NUL-terminated at len %d", label,
                 (int)edit->len);
    KH_CHECK_MSG(edit->cursor <= edit->len,
                 "%s: cursor %d past len %d", label, (int)edit->cursor,
                 (int)edit->len);
}

static void fill_chars(char *out, size_t count, char c)
{
    memset(out, c, count);
    out[count] = 0;
}

/* The editor shows each non-ASCII character as one '?', but the buffer and
 * the cursor must keep the original bytes so a round trip through the editor
 * does not mangle a Japanese or accented URL. */
static void expect_cursor_on_a_character(const char *label, const UrlEdit *edit)
{
    KH_CHECK_MSG(edit->cursor == 0 ||
                     ((unsigned char)edit->buf[edit->cursor] & 0xC0) != 0x80,
                 "%s: cursor %d points inside a UTF-8 sequence", label,
                 (int)edit->cursor);
}

static void expect_whole_characters(const char *label, const UrlEdit *edit)
{
    size_t i = 0;

    while (i < edit->len) {
        size_t n = urledit_char_len(edit, i);
        size_t j;

        KH_CHECK_MSG(n >= 1 && i + n <= edit->len,
                     "%s: character at %d runs past len %d", label, (int)i,
                     (int)edit->len);
        if (n < 1 || i + n > edit->len) {
            return;
        }
        for (j = 1; j < n; j++) {
            KH_CHECK_MSG(((unsigned char)edit->buf[i + j] & 0xC0) == 0x80,
                         "%s: byte %d is not a continuation byte", label,
                         (int)(i + j));
        }
        i += n;
    }
    KH_CHECK_MSG(i == edit->len, "%s: the walk ends at %d, len %d", label,
                 (int)i, (int)edit->len);
}

static void test_init_copies_url_and_puts_cursor_at_end(void)
{
    UrlEdit edit;
    char big[KHOME_TAB_URL_MAX + 64];

    urledit_init(&edit, "https://example.com");
    expect_edit("init", &edit, "https://example.com", 19);

    edit.shift = 1;
    edit.symbols = 1;
    urledit_init(&edit, "abc");
    expect_edit("re-init", &edit, "abc", 3);
    KH_CHECK_MSG(edit.shift == 0 && edit.symbols == 0,
                 "re-init: key modes should start cleared, got shift %d and "
                 "symbols %d", edit.shift, edit.symbols);

    urledit_init(&edit, NULL);
    expect_edit("init NULL", &edit, "", 0);

    fill_chars(big, sizeof(big) - 1, 'x');
    urledit_init(&edit, big);
    KH_CHECK_MSG(edit.len == KHOME_TAB_URL_MAX - 1,
                 "init over capacity: expected len %d, got %d",
                 KHOME_TAB_URL_MAX - 1, (int)edit.len);
    KH_CHECK_MSG(edit.cursor == edit.len,
                 "init over capacity: expected cursor %d, got %d",
                 (int)edit.len, (int)edit.cursor);
    KH_CHECK_MSG(edit.buf[edit.len] == 0,
                 "init over capacity: buffer is not NUL-terminated");
    KH_CHECK_MSG(memcmp(edit.buf, big, edit.len) == 0,
                 "init over capacity: copied text differs from the URL prefix");
}

static void test_insert_pushes_text_at_cursor(void)
{
    UrlEdit edit;

    urledit_init(&edit, "ac");
    urledit_set_cursor(&edit, 1);
    KH_CHECK_MSG(urledit_insert(&edit, "b") == 1, "insert: expected success");
    expect_edit("insert between", &edit, "abc", 2);

    KH_CHECK_MSG(urledit_insert(&edit, "XY") == 1,
                 "insert: expected success for two bytes");
    expect_edit("insert two bytes", &edit, "abXYc", 4);

    urledit_set_cursor(&edit, 0);
    KH_CHECK_MSG(urledit_insert(&edit, ">>") == 1,
                 "insert: expected success at the start");
    expect_edit("insert at start", &edit, ">>abXYc", 2);

    urledit_set_cursor(&edit, edit.len);
    KH_CHECK_MSG(urledit_insert(&edit, "!") == 1,
                 "insert: expected success at the end");
    expect_edit("insert at end", &edit, ">>abXYc!", 8);
}

static void test_insert_uses_the_whole_buffer_and_rejects_overflow(void)
{
    UrlEdit edit;
    char full[KHOME_TAB_URL_MAX];
    char before[KHOME_TAB_URL_MAX];
    char nearly[KHOME_TAB_URL_MAX];

    fill_chars(full, KHOME_TAB_URL_MAX - 1, 'a');
    urledit_init(&edit, "");
    KH_CHECK_MSG(urledit_insert(&edit, full) == 1,
                 "insert to capacity: expected success");
    expect_edit("insert to capacity", &edit, full, KHOME_TAB_URL_MAX - 1);
    KH_CHECK_MSG(edit.len == KHOME_TAB_URL_MAX - 1,
                 "insert to capacity: expected len %d, got %d",
                 KHOME_TAB_URL_MAX - 1, (int)edit.len);

    memcpy(before, edit.buf, sizeof(before));
    KH_CHECK_MSG(urledit_insert(&edit, "b") == 0,
                 "insert past capacity: expected rejection");
    KH_CHECK_MSG(edit.len == KHOME_TAB_URL_MAX - 1,
                 "insert past capacity: len changed to %d", (int)edit.len);
    KH_CHECK_MSG(edit.cursor == KHOME_TAB_URL_MAX - 1,
                 "insert past capacity: cursor changed to %d",
                 (int)edit.cursor);
    KH_CHECK_MSG(strcmp(edit.buf, before) == 0,
                 "insert past capacity: text changed");

    fill_chars(nearly, KHOME_TAB_URL_MAX - 2, 'a');
    urledit_init(&edit, "");
    KH_CHECK_MSG(urledit_insert(&edit, nearly) == 1,
                 "insert near capacity: expected success");
    KH_CHECK_MSG(urledit_insert(&edit, "bc") == 0,
                 "insert two bytes near capacity: expected rejection");
    expect_edit("insert two bytes near capacity", &edit, nearly,
                KHOME_TAB_URL_MAX - 2);
    KH_CHECK_MSG(urledit_insert(&edit, "b") == 1,
                 "insert the last byte: expected success");
    KH_CHECK_MSG(edit.len == KHOME_TAB_URL_MAX - 1,
                 "insert the last byte: expected len %d, got %d",
                 KHOME_TAB_URL_MAX - 1, (int)edit.len);
    KH_CHECK_MSG(edit.buf[edit.len] == 0,
                 "insert the last byte: buffer is not NUL-terminated");
}

static void test_backspace_deletes_the_byte_before_the_cursor(void)
{
    UrlEdit edit;

    urledit_init(&edit, "abcd");
    urledit_set_cursor(&edit, 2);
    KH_CHECK_MSG(urledit_backspace(&edit) == 1,
                 "backspace: expected success in the middle");
    expect_edit("backspace middle", &edit, "acd", 1);

    urledit_set_cursor(&edit, 0);
    KH_CHECK_MSG(urledit_backspace(&edit) == 0,
                 "backspace at start: expected no-op");
    expect_edit("backspace at start", &edit, "acd", 0);

    urledit_set_cursor(&edit, edit.len);
    KH_CHECK_MSG(urledit_backspace(&edit) == 1,
                 "backspace at end: expected success");
    expect_edit("backspace at end", &edit, "ac", 2);
}

static void test_move_saturates_at_both_ends(void)
{
    UrlEdit edit;

    urledit_init(&edit, "abcd");
    urledit_move(&edit, 1);
    KH_CHECK_MSG(edit.cursor == 4, "move right at end: expected 4, got %d",
                 (int)edit.cursor);
    urledit_move(&edit, -2);
    KH_CHECK_MSG(edit.cursor == 2, "move left two: expected 2, got %d",
                 (int)edit.cursor);
    urledit_move(&edit, -10);
    KH_CHECK_MSG(edit.cursor == 0, "move past start: expected 0, got %d",
                 (int)edit.cursor);
    urledit_move(&edit, 3);
    KH_CHECK_MSG(edit.cursor == 3, "move right three: expected 3, got %d",
                 (int)edit.cursor);
    urledit_move(&edit, 0);
    KH_CHECK_MSG(edit.cursor == 3, "move zero: expected 3, got %d",
                 (int)edit.cursor);
    urledit_move(&edit, 99);
    KH_CHECK_MSG(edit.cursor == 4, "move past end: expected 4, got %d",
                 (int)edit.cursor);
}

static void test_set_cursor_clamps_to_the_text(void)
{
    UrlEdit edit;

    urledit_init(&edit, "abcd");
    urledit_set_cursor(&edit, 2);
    KH_CHECK_MSG(edit.cursor == 2, "set cursor 2: got %d", (int)edit.cursor);
    urledit_set_cursor(&edit, 99);
    KH_CHECK_MSG(edit.cursor == 4, "set cursor past end: expected 4, got %d",
                 (int)edit.cursor);
    urledit_set_cursor(&edit, 0);
    KH_CHECK_MSG(edit.cursor == 0, "set cursor 0: got %d", (int)edit.cursor);
}

static void test_clear_empties_the_editor(void)
{
    UrlEdit edit;

    urledit_init(&edit, "abc");
    urledit_clear(&edit);
    expect_edit("clear", &edit, "", 0);
    KH_CHECK_MSG(edit.buf[0] == 0, "clear: buffer does not start with NUL");

    KH_CHECK_MSG(urledit_insert(&edit, "z") == 1,
                 "insert after clear: expected success");
    expect_edit("insert after clear", &edit, "z", 1);
}

/* "aé b": a=1 byte, é=2 bytes, space and b=1 byte each. */
#define UTF8_SAMPLE "a\xC3\xA9 b"

static void test_utf8_backspace_deletes_whole_characters(void)
{
    UrlEdit edit;

    urledit_init(&edit, UTF8_SAMPLE);
    expect_edit("utf8 backspace start", &edit, UTF8_SAMPLE, 5);

    KH_CHECK_MSG(urledit_backspace(&edit) == 1,
                 "utf8 backspace of b: expected success");
    expect_edit("utf8 backspace b", &edit, "a\xC3\xA9 ", 4);
    KH_CHECK_MSG(urledit_backspace(&edit) == 1,
                 "utf8 backspace of space: expected success");
    expect_edit("utf8 backspace space", &edit, "a\xC3\xA9", 3);
    KH_CHECK_MSG(urledit_backspace(&edit) == 1,
                 "utf8 backspace of e-acute: expected success");
    expect_edit("utf8 backspace e-acute", &edit, "a", 1);

    expect_cursor_on_a_character("utf8 backspace", &edit);
    expect_whole_characters("utf8 backspace", &edit);
}

static void test_utf8_move_never_stops_inside_a_character(void)
{
    UrlEdit edit;

    urledit_init(&edit, UTF8_SAMPLE);
    KH_CHECK_MSG(edit.cursor == 5, "utf8 move: expected the cursor at 5, got %d",
                 (int)edit.cursor);

    urledit_move(&edit, -1);
    KH_CHECK_MSG(edit.cursor == 4, "utf8 move -1: expected 4, got %d",
                 (int)edit.cursor);
    urledit_move(&edit, -1);
    KH_CHECK_MSG(edit.cursor == 3, "utf8 move -2: expected 3, got %d",
                 (int)edit.cursor);
    urledit_move(&edit, -1);
    KH_CHECK_MSG(edit.cursor == 1,
                 "utf8 move -3: the two-byte character must be skipped as a "
                 "whole, expected 1, got %d", (int)edit.cursor);
    expect_cursor_on_a_character("utf8 move left", &edit);
    urledit_move(&edit, -1);
    KH_CHECK_MSG(edit.cursor == 0, "utf8 move -4: expected 0, got %d",
                 (int)edit.cursor);

    urledit_move(&edit, 1);
    KH_CHECK_MSG(edit.cursor == 1, "utf8 move +1: expected 1, got %d",
                 (int)edit.cursor);
    urledit_move(&edit, 1);
    KH_CHECK_MSG(edit.cursor == 3, "utf8 move +2: expected 3, got %d",
                 (int)edit.cursor);
    expect_cursor_on_a_character("utf8 move right", &edit);
    urledit_move(&edit, 1);
    urledit_move(&edit, 1);
    KH_CHECK_MSG(edit.cursor == 5, "utf8 move +4: expected 5, got %d",
                 (int)edit.cursor);
}

static void test_utf8_set_cursor_rounds_down_to_a_character(void)
{
    UrlEdit edit;

    urledit_init(&edit, UTF8_SAMPLE);
    urledit_set_cursor(&edit, 2);
    KH_CHECK_MSG(edit.cursor == 1,
                 "set cursor inside e-acute: expected the character start 1, "
                 "got %d", (int)edit.cursor);
    expect_cursor_on_a_character("set cursor inside e-acute", &edit);

    urledit_set_cursor(&edit, 3);
    KH_CHECK_MSG(edit.cursor == 3,
                 "set cursor on the space: expected 3, got %d",
                 (int)edit.cursor);
    urledit_set_cursor(&edit, 99);
    KH_CHECK_MSG(edit.cursor == 5,
                 "set cursor past the text: expected 5, got %d",
                 (int)edit.cursor);
    expect_whole_characters("set cursor", &edit);
}

static void test_utf8_insert_keeps_characters_whole(void)
{
    UrlEdit edit;

    urledit_init(&edit, "ab");
    urledit_set_cursor(&edit, 1);
    KH_CHECK_MSG(urledit_insert(&edit, "\xC3\xA9") == 1,
                 "utf8 insert: expected success");
    expect_edit("utf8 insert e-acute", &edit, "a\xC3\xA9" "b", 3);
    expect_cursor_on_a_character("utf8 insert e-acute", &edit);
    expect_whole_characters("utf8 insert e-acute", &edit);

    KH_CHECK_MSG(urledit_insert(&edit, "x") == 1,
                 "utf8 insert ascii: expected success");
    expect_edit("utf8 insert ascii", &edit, "a\xC3\xA9xb", 4);

    KH_CHECK_MSG(urledit_backspace(&edit) == 1,
                 "utf8 backspace x: expected success");
    expect_edit("utf8 backspace x", &edit, "a\xC3\xA9" "b", 3);
    KH_CHECK_MSG(urledit_backspace(&edit) == 1,
                 "utf8 backspace the inserted e-acute: expected success");
    expect_edit("utf8 backspace e-acute", &edit, "ab", 1);
    expect_whole_characters("utf8 insert round trip", &edit);

    urledit_init(&edit, "");
    KH_CHECK_MSG(urledit_insert(&edit, "\xC3\xA9") == 1,
                 "utf8 insert into an empty editor: expected success");
    expect_edit("utf8 insert into empty", &edit, "\xC3\xA9", 2);
    KH_CHECK_MSG(urledit_backspace(&edit) == 1,
                 "utf8 backspace the only character: expected success");
    expect_edit("utf8 backspace the only character", &edit, "", 0);
}

static void expect_normalize(const char *label, UrlEdit *edit, size_t cap,
                             int expected_len, const char *expected)
{
    char out[KHOME_TAB_URL_MAX + 16];
    int len;

    memset(out, 0x7f, sizeof(out));
    len = urledit_normalize(edit, out, cap);
    KH_CHECK_MSG(len == expected_len, "%s: expected length %d, got %d", label,
                 expected_len, len);
    if (expected_len < 0) {
        return;
    }
    KH_CHECK_MSG(strcmp(out, expected) == 0,
                 "%s: expected \"%s\", got \"%s\"", label, expected, out);
    KH_CHECK_MSG((size_t)len == strlen(out),
                 "%s: returned %d for \"%s\" (%d bytes)", label, len, out,
                 (int)strlen(out));
}

static void test_normalize_adds_missing_scheme(void)
{
    UrlEdit edit;

    urledit_init(&edit, "example.com");
    expect_normalize("no scheme", &edit, 64, 19, "https://example.com");

    urledit_init(&edit, "https://example.com");
    expect_normalize("https", &edit, 64, 19, "https://example.com");

    urledit_init(&edit, "http://example.com");
    expect_normalize("http", &edit, 64, 18, "http://example.com");

    urledit_init(&edit, "ftp://example.com");
    expect_normalize("other scheme", &edit, 64, 17, "ftp://example.com");

    urledit_init(&edit, "example.com/path://segment");
    expect_normalize("scheme marker inside the address", &edit, 64, 26,
                     "example.com/path://segment");
}

static void test_normalize_trims_surrounding_whitespace(void)
{
    UrlEdit edit;

    urledit_init(&edit, "  example.com \t\r\n");
    expect_normalize("trim both ends", &edit, 64, 19,
                     "https://example.com");

    urledit_init(&edit, "\t https://example.com \r");
    expect_normalize("trim around a scheme", &edit, 64, 19,
                     "https://example.com");

    urledit_init(&edit, " \t\r\n\v\f ");
    expect_normalize("blank", &edit, 64, 0, "");

    urledit_init(&edit, "");
    expect_normalize("empty", &edit, 64, 0, "");
}

static void test_normalize_rejects_a_short_buffer(void)
{
    UrlEdit edit;
    char out[8];

    urledit_init(&edit, "example.com");
    expect_normalize("one byte short", &edit, 19, -1, "");
    expect_normalize("exact fit", &edit, 20, 19, "https://example.com");

    urledit_init(&edit, "abcdefghij");
    expect_normalize("short for a ten byte address", &edit, 18, -1, "");
    expect_normalize("exact fit for a ten byte address", &edit, 19, 18,
                     "https://abcdefghij");

    urledit_init(&edit, "abc");
    KH_CHECK_MSG(urledit_normalize(&edit, out, 0) == -1,
                 "cap 0: expected -1");
}

void kh_run_urledit_tests(void)
{
    printf("\n[urledit]\n");
    kh_run_test("urledit.init_copies_url_and_puts_cursor_at_end",
                test_init_copies_url_and_puts_cursor_at_end);
    kh_run_test("urledit.insert_pushes_text_at_cursor",
                test_insert_pushes_text_at_cursor);
    kh_run_test("urledit.insert_uses_the_whole_buffer_and_rejects_overflow",
                test_insert_uses_the_whole_buffer_and_rejects_overflow);
    kh_run_test("urledit.backspace_deletes_the_byte_before_the_cursor",
                test_backspace_deletes_the_byte_before_the_cursor);
    kh_run_test("urledit.move_saturates_at_both_ends",
                test_move_saturates_at_both_ends);
    kh_run_test("urledit.set_cursor_clamps_to_the_text",
                test_set_cursor_clamps_to_the_text);
    kh_run_test("urledit.clear_empties_the_editor",
                test_clear_empties_the_editor);
    kh_run_test("urledit.utf8_backspace_deletes_whole_characters",
                test_utf8_backspace_deletes_whole_characters);
    kh_run_test("urledit.utf8_move_never_stops_inside_a_character",
                test_utf8_move_never_stops_inside_a_character);
    kh_run_test("urledit.utf8_set_cursor_rounds_down_to_a_character",
                test_utf8_set_cursor_rounds_down_to_a_character);
    kh_run_test("urledit.utf8_insert_keeps_characters_whole",
                test_utf8_insert_keeps_characters_whole);
    kh_run_test("urledit.normalize_adds_missing_scheme",
                test_normalize_adds_missing_scheme);
    kh_run_test("urledit.normalize_trims_surrounding_whitespace",
                test_normalize_trims_surrounding_whitespace);
    kh_run_test("urledit.normalize_rejects_a_short_buffer",
                test_normalize_rejects_a_short_buffer);
}
