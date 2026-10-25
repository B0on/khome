#ifndef KHOME_URLEDIT_H
#define KHOME_URLEDIT_H

#include <stddef.h>

#include "tabs.h"

/* The URL edit buffer behind the full-screen editor.  It is a pure value like
 * TabList: no allocation and no I/O, and every function leaves len, cursor and
 * the NUL terminator consistent.  The cursor and the editing keys work on whole
 * UTF-8 characters, so a multi-byte character survives a round trip even though
 * the editor shows each non-ASCII character as one '?'. */

typedef struct {
    char buf[KHOME_TAB_URL_MAX];
    size_t len;
    size_t cursor;
    int shift;
    int symbols;
    /* First wrapped line on screen.  The layout clamps it to a real line; a
     * value past the end shows the last page. */
    int first_line;
} UrlEdit;

/* Copies url (truncated to the buffer) and puts the cursor at the end. */
void urledit_init(UrlEdit *edit, const char *url);

/* Inserts text at the cursor.  Returns 1, or 0 when the result would not fit,
 * in which case nothing changes. */
int urledit_insert(UrlEdit *edit, const char *text);

/* Deletes the UTF-8 character before the cursor.  Returns 1, or 0 at the
 * start. */
int urledit_backspace(UrlEdit *edit);

/* Moves the cursor by delta UTF-8 characters, clamped to 0..len. */
void urledit_move(UrlEdit *edit, int delta);

/* Moves the cursor to cursor, clamped to 0..len and rounded down to the start
 * of a UTF-8 character. */
void urledit_set_cursor(UrlEdit *edit, size_t cursor);

/* Length in bytes of the UTF-8 character starting at i, always at least 1.
 * A malformed or truncated sequence counts as one byte. */
size_t urledit_char_len(const UrlEdit *edit, size_t i);

void urledit_clear(UrlEdit *edit);

/* Trims surrounding ASCII whitespace.  When the result has no "://", it
 * prepends "https://".  Writes a NUL-terminated string to out and returns its
 * length; returns 0 when nothing is left after trimming, and -1 when out is
 * too small. */
int urledit_normalize(const UrlEdit *edit, char *out, size_t cap);

#endif
