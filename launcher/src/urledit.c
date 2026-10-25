#include "urledit.h"

#include <limits.h>
#include <string.h>

static int ascii_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
           c == '\f';
}

/* Byte length of the sequence at i.  A malformed or truncated sequence counts
 * as one byte so every walk through the buffer still advances. */
static size_t utf8_len(const char *s, size_t len, size_t i)
{
    unsigned char c;
    size_t n = 1;
    size_t j;

    if (i >= len) {
        return 1;
    }
    c = (unsigned char)s[i];
    if ((c & 0xE0) == 0xC0) {
        n = 2;
    } else if ((c & 0xF0) == 0xE0) {
        n = 3;
    } else if ((c & 0xF8) == 0xF0) {
        n = 4;
    }
    if (i + n > len) {
        return 1;
    }
    for (j = 1; j < n; j++) {
        if (((unsigned char)s[i + j] & 0xC0) != 0x80) {
            return 1;
        }
    }
    return n;
}

/* Rounds i down to a character start when it points inside a sequence. */
static size_t utf8_floor(const char *s, size_t len, size_t i)
{
    size_t start;
    size_t limit;

    if (i == 0 || i >= len || ((unsigned char)s[i] & 0xC0) != 0x80) {
        return i;
    }
    start = i;
    limit = i > 4 ? i - 4 : 0;
    while (start > limit && ((unsigned char)s[start] & 0xC0) == 0x80) {
        start--;
    }
    if (start < i && utf8_len(s, len, start) > i - start) {
        return start;
    }
    return i;
}

/* Start of the character that ends at i. */
static size_t utf8_prev(const char *s, size_t len, size_t i)
{
    size_t start;
    size_t limit;

    if (i == 0) {
        return 0;
    }
    start = i - 1;
    limit = i > 4 ? i - 4 : 0;
    while (start > limit && ((unsigned char)s[start] & 0xC0) == 0x80) {
        start--;
    }
    if (utf8_len(s, len, start) == i - start) {
        return start;
    }
    return i - 1;
}

size_t urledit_char_len(const UrlEdit *edit, size_t i)
{
    if (edit == NULL) {
        return 1;
    }
    return utf8_len(edit->buf, edit->len, i);
}

void urledit_init(UrlEdit *edit, const char *url)
{
    size_t len = 0;

    if (edit == NULL) {
        return;
    }
    memset(edit, 0, sizeof(*edit));
    if (url != NULL) {
        len = strlen(url);
        if (len >= sizeof(edit->buf)) {
            len = sizeof(edit->buf) - 1;
        }
        memcpy(edit->buf, url, len);
    }
    edit->buf[len] = 0;
    edit->len = len;
    edit->cursor = len;
    /* Past the end, so the layout clamps it to the page holding the cursor. */
    edit->first_line = INT_MAX;
}

int urledit_insert(UrlEdit *edit, const char *text)
{
    size_t n;

    if (edit == NULL || text == NULL) {
        return 0;
    }
    n = strlen(text);
    if (n == 0 || edit->len + n >= sizeof(edit->buf)) {
        return 0;
    }
    urledit_set_cursor(edit, edit->cursor);
    memmove(edit->buf + edit->cursor + n, edit->buf + edit->cursor,
            edit->len - edit->cursor + 1);
    memcpy(edit->buf + edit->cursor, text, n);
    edit->len += n;
    edit->cursor += n;
    return 1;
}

int urledit_backspace(UrlEdit *edit)
{
    size_t start;

    if (edit == NULL || edit->cursor == 0) {
        return 0;
    }
    urledit_set_cursor(edit, edit->cursor);
    if (edit->cursor == 0) {
        return 0;
    }
    start = utf8_prev(edit->buf, edit->len, edit->cursor);
    memmove(edit->buf + start, edit->buf + edit->cursor,
            edit->len - edit->cursor + 1);
    edit->len -= edit->cursor - start;
    edit->cursor = start;
    return 1;
}

void urledit_move(UrlEdit *edit, int delta)
{
    if (edit == NULL) {
        return;
    }
    while (delta < 0 && edit->cursor > 0) {
        edit->cursor = utf8_prev(edit->buf, edit->len, edit->cursor);
        delta++;
    }
    while (delta > 0 && edit->cursor < edit->len) {
        edit->cursor += utf8_len(edit->buf, edit->len, edit->cursor);
        delta--;
    }
}

void urledit_set_cursor(UrlEdit *edit, size_t cursor)
{
    if (edit == NULL) {
        return;
    }
    if (cursor > edit->len) {
        cursor = edit->len;
    }
    edit->cursor = utf8_floor(edit->buf, edit->len, cursor);
}

void urledit_clear(UrlEdit *edit)
{
    if (edit == NULL) {
        return;
    }
    edit->len = 0;
    edit->cursor = 0;
    edit->first_line = 0;
    edit->buf[0] = 0;
}

int urledit_normalize(const UrlEdit *edit, char *out, size_t cap)
{
    static const char prefix[] = "https://";
    size_t start = 0;
    size_t end;
    size_t len;
    size_t prefix_len = 0;
    size_t i;
    int has_scheme = 0;

    if (out != NULL && cap > 0) {
        out[0] = 0;
    }
    if (edit == NULL || out == NULL || cap == 0) {
        return -1;
    }
    end = edit->len;
    while (start < end && ascii_space((unsigned char)edit->buf[start])) {
        start++;
    }
    while (end > start && ascii_space((unsigned char)edit->buf[end - 1])) {
        end--;
    }
    len = end - start;
    if (len == 0) {
        return 0;
    }
    for (i = start; i + 2 < end; i++) {
        if (edit->buf[i] == ':' && edit->buf[i + 1] == '/' &&
            edit->buf[i + 2] == '/') {
            has_scheme = 1;
            break;
        }
    }
    if (!has_scheme) {
        prefix_len = sizeof(prefix) - 1;
    }
    if (prefix_len + len + 1 > cap) {
        return -1;
    }
    memcpy(out, prefix, prefix_len);
    memcpy(out + prefix_len, edit->buf + start, len);
    out[prefix_len + len] = 0;
    return (int)(prefix_len + len);
}
