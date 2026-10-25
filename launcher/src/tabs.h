#ifndef KHOME_TABS_H
#define KHOME_TABS_H

#include <stddef.h>

/* Pseudo tabs for the stock browser, which has no tabs of its own.  The model
 * is pure: no file I/O, no allocation, and every function leaves a valid list
 * (count 0..KHOME_MAX_TABS, active -1 exactly when count is 0). */

#define KHOME_MAX_TABS 8
#define KHOME_TAB_URL_MAX 1024

typedef struct {
    char url[KHOME_TAB_URL_MAX];
} Tab;

typedef struct {
    Tab tabs[KHOME_MAX_TABS];
    int count;
    int active; /* index into tabs, or -1 when count == 0 */
} TabList;

/* Text form, one item per line:
 *
 *   active=<index>
 *   <url>
 *   <url>
 *
 * The header line is optional.  Without it the first URL is active and every
 * non-empty line is a URL.  Blank lines are ignored, a trailing CR is
 * stripped, and URLs longer than KHOME_TAB_URL_MAX - 1 bytes are skipped.
 * URLs past KHOME_MAX_TABS are ignored. */
int tabs_parse(TabList *tabs, const char *text);
/* Writes the text form above.  Returns the length excluding the NUL, or -1
 * and an empty string when cap cannot hold the whole text. */
int tabs_format(const TabList *tabs, char *out, size_t cap);

void tabs_init(TabList *tabs);

/* Appends url and makes it active.  Tabs are stored in open order, and a full
 * list drops its lowest-index tab other than the active one (the oldest
 * background tab).  Returns the new active index, or -1 when url is NULL,
 * empty, or too long. */
int tabs_open(TabList *tabs, const char *url);

/* Closes index.  Closing the active tab activates the tab that slides into
 * its place, or the previous tab when it was last.  Returns the new active
 * index, or -1 when index is out of range. */
int tabs_close(TabList *tabs, int index);

int tabs_select(TabList *tabs, int index);
/* Replaces the active tab's URL.  Returns 0, or -1 when there is no active
 * tab or the URL is too long. */
int tabs_set_active_url(TabList *tabs, const char *url);
/* Replaces index's URL.  Returns 0, or -1 when index is out of range or the
 * URL is empty or too long. */
int tabs_set_url(TabList *tabs, int index, const char *url);
const char *tabs_active_url(const TabList *tabs);

#define KHOME_TAB_HOST_MAX 256
#define KHOME_TAB_PATH_MAX 128
#define KHOME_TAB_PATH_CHARS 24

/* Splits a URL for a tab row: host without scheme, userinfo, or a leading
 * "www.", and the path without query or fragment, shortened to at most
 * KHOME_TAB_PATH_CHARS characters plus an ellipsis.  Both outputs are always
 * NUL-terminated; host is "" and path is "/" when the URL has no such part. */
void tabs_url_parts(const char *url, char *host, size_t host_cap, char *path,
                    size_t path_cap);

/* encodeURIComponent: A-Z a-z 0-9 - _ . ! ~ * ' ( ) stay literal; every other
 * UTF-8 byte becomes %XX in uppercase.  Returns the written length excluding
 * the NUL, or -1 and an empty string when cap cannot hold the result. */
int tabs_encode_component(const char *src, char *out, size_t cap);

/* Decodes a hex string of UTF-16LE code units, the form sqlite3's hex() gives
 * a text value, into UTF-8.  A surrogate pair becomes one 4-byte sequence.
 * Returns the written length excluding the NUL, or -1 and an empty string for
 * a non-hex digit, a partial code unit, an unpaired surrogate, a NUL code
 * unit, or a result that does not fit cap. */
int tabs_utf16_hex_decode(const char *hex, char *out, size_t cap);

#define KHOME_PAGE_TITLE_MAX 512

/* Splits one line of "urlhex titlehex", the row the browser LocalStorage
 * query prints, and decodes both fields.  The two fields are separated by one
 * space; the title ends at the first CR, LF, or NUL.  Runs of ASCII
 * whitespace in the title collapse to one space and the ends are trimmed.
 * Returns 0, or -1 and empty outputs when row has no separator or the URL
 * field does not decode.  A title that does not decode or does not fit
 * title_cap is left empty, so the caller still gets the URL. */
int tabs_browser_row_parse(const char *row, char *url, size_t url_cap,
                           char *title, size_t title_cap);

/* The browser keeps its display state in the LocalStorage "settings" object:
 * {"javascriptDisabled":false,"imagesDisabled":false,"orientation":"auto"}.
 * The stock browser only distinguishes "auto" (the panel's portrait layout)
 * from "landscapeLeft"; any other value, a missing key, or malformed JSON is
 * portrait.  khome stores no orientation of its own. */
typedef enum {
    BROWSER_ORIENTATION_PORTRAIT = 0,
    BROWSER_ORIENTATION_LANDSCAPE
} BrowserOrientation;

BrowserOrientation tabs_browser_orientation(const char *settings_json);
/* The value to put back in settings.orientation: "auto" for portrait and
 * "landscapeLeft" for landscape. */
const char *tabs_browser_orientation_value(BrowserOrientation orientation);

/* Favorites are the browser bookmarks khome owns.  They persist as
 * favorites.tsv next to tabs.txt, one "url<TAB>title" line per entry, and the
 * home tiles draw the name and its one-character initial. */
#define KHOME_MAX_FAVORITES 64
#define KHOME_FAVORITE_NAME_MAX 128

typedef struct {
    char url[KHOME_TAB_URL_MAX];
    char name[KHOME_FAVORITE_NAME_MAX];
    char initial[8]; /* first character of name, upper-cased */
} Favorite;

/* Text form, one item per line:
 *
 *   <url><TAB><title>
 *
 * A leading UTF-8 BOM is ignored, blank and whitespace-only lines are skipped,
 * and a trailing CR is stripped.  A line without a tab is a bare URL with an
 * empty title.  An empty or too-long URL, and a URL that already appeared
 * earlier, are skipped.  An empty title falls back to the URL's host.  At most
 * max entries are read.  Returns the count. */
int favorites_parse(Favorite *out, int max, const char *text);
/* Writes the text form above.  Returns the length excluding the NUL, or -1
 * and an empty string when cap cannot hold the whole text. */
int favorites_format(const Favorite *favorites, int count, char *out,
                     size_t cap);

/* Index of url in favorites, or -1. */
int favorites_find(const Favorite *favorites, int count, const char *url);

/* Appends url and title; a NULL or empty title falls back to the URL's host.
 * Returns the new index, or -1 when the list is full, url is empty or too
 * long, or url is already in the list. */
int favorites_add(Favorite *favorites, int *count, int max, const char *url,
                  const char *title);

/* Removes index and slides the rest down.  Returns 0, or -1 when index is out
 * of range. */
int favorites_remove(Favorite *favorites, int *count, int index);

#endif
