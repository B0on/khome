#include "tabs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t utf8_char_len(const char *s, size_t left)
{
    unsigned char c = (unsigned char)s[0];
    size_t len;
    if (c < 0x80) {
        return 1;
    }
    if ((c & 0xE0) == 0xC0) {
        len = 2;
    } else if ((c & 0xF0) == 0xE0) {
        len = 3;
    } else if ((c & 0xF8) == 0xF0) {
        len = 4;
    } else {
        return 1;
    }
    if (len > left) {
        return 1;
    }
    if (len >= 2 && ((unsigned char)s[1] & 0xC0) != 0x80) {
        return 1;
    }
    if (len >= 3 && ((unsigned char)s[2] & 0xC0) != 0x80) {
        return 1;
    }
    if (len == 4 && ((unsigned char)s[3] & 0xC0) != 0x80) {
        return 1;
    }
    return len;
}

static size_t prev_char(const char *s, size_t cut)
{
    if (cut == 0) {
        return 0;
    }
    cut--;
    while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) {
        cut--;
    }
    return cut;
}

/* Copies at most max_chars characters and at most cap - 1 bytes, adding an
 * ellipsis when the source is longer. */
static void copy_short(char *dst, size_t cap, const char *src, size_t len,
                       size_t max_chars)
{
    size_t cut = 0;
    size_t chars = 0;
    int truncated;

    if (dst == NULL || cap == 0) {
        return;
    }
    dst[0] = 0;
    if (src == NULL || len == 0) {
        return;
    }
    while (cut < len && chars < max_chars) {
        cut += utf8_char_len(src + cut, len - cut);
        chars++;
    }
    truncated = cut < len;
    while (cut > 0 && cut + (truncated ? 3u : 0u) + 1u > cap) {
        cut = prev_char(src, cut);
    }
    if (cut + (truncated ? 3u : 0u) + 1u > cap) {
        dst[0] = 0;
        return;
    }
    memcpy(dst, src, cut);
    dst[cut] = 0;
    if (truncated) {
        memcpy(dst + cut, "\xE2\x80\xA6", 3);
        dst[cut + 3] = 0;
    }
}

void tabs_init(TabList *tabs)
{
    if (tabs == NULL) {
        return;
    }
    memset(tabs, 0, sizeof(*tabs));
    tabs->active = -1;
}

static int active_index(const TabList *tabs)
{
    int active;
    if (tabs == NULL || tabs->count <= 0) {
        return -1;
    }
    active = tabs->active;
    if (active < 0) {
        active = 0;
    }
    if (active >= tabs->count) {
        active = tabs->count - 1;
    }
    return active;
}

static int parse_header(const char *line, size_t len, int *active)
{
    char buf[32];
    char *end;
    long value;

    if (len < 7 || len >= sizeof(buf) || strncmp(line, "active=", 7) != 0) {
        return 0;
    }
    memcpy(buf, line, len);
    buf[len] = 0;
    value = strtol(buf + 7, &end, 10);
    if (end == buf + 7 || *end != 0) {
        return 0;
    }
    *active = (int)value;
    return 1;
}

static void append_parsed(TabList *out, const char *src, size_t len)
{
    if (out->count >= KHOME_MAX_TABS || len == 0 ||
        len >= KHOME_TAB_URL_MAX) {
        return;
    }
    memcpy(out->tabs[out->count].url, src, len);
    out->tabs[out->count].url[len] = 0;
    out->count++;
}

int tabs_parse(TabList *tabs, const char *text)
{
    TabList out;
    const char *p;
    int header_active = -1;
    int first = 1;

    tabs_init(&out);
    p = text;
    if (p != NULL && (unsigned char)p[0] == 0xEF &&
        (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) {
        p += 3;
    }
    while (p != NULL && *p != 0) {
        const char *nl = strchr(p, '\n');
        size_t len = nl != NULL ? (size_t)(nl - p) : strlen(p);
        while (len > 0 && p[len - 1] == '\r') {
            len--;
        }
        if (len > 0) {
            if (first && parse_header(p, len, &header_active)) {
                first = 0;
            } else {
                append_parsed(&out, p, len);
                if (first) {
                    first = 0;
                }
            }
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    if (out.count > 0) {
        out.active = header_active >= 0 ? header_active : 0;
        if (out.active < 0) {
            out.active = 0;
        }
        if (out.active >= out.count) {
            out.active = out.count - 1;
        }
    }
    if (tabs != NULL) {
        *tabs = out;
    }
    return out.count;
}

int tabs_format(const TabList *tabs, char *out, size_t cap)
{
    char header[32];
    size_t needed;
    size_t used;
    int count;
    int active;
    int i;

    if (out != NULL && cap > 0) {
        out[0] = 0;
    }
    if (tabs == NULL || out == NULL || cap == 0) {
        return -1;
    }
    count = tabs->count;
    if (count < 0) {
        count = 0;
    }
    if (count > KHOME_MAX_TABS) {
        count = KHOME_MAX_TABS;
    }
    active = active_index(tabs);
    needed = (size_t)snprintf(header, sizeof(header), "active=%d\n", active);
    for (i = 0; i < count; i++) {
        needed += strlen(tabs->tabs[i].url) + 1;
    }
    if (needed + 1 > cap) {
        return -1;
    }
    memcpy(out, header, strlen(header));
    used = strlen(header);
    for (i = 0; i < count; i++) {
        size_t len = strlen(tabs->tabs[i].url);
        memcpy(out + used, tabs->tabs[i].url, len);
        used += len;
        out[used++] = '\n';
    }
    out[used] = 0;
    return (int)used;
}

int tabs_open(TabList *tabs, const char *url)
{
    size_t len;

    if (tabs == NULL || url == NULL) {
        return -1;
    }
    len = strlen(url);
    if (len == 0 || len >= KHOME_TAB_URL_MAX) {
        return -1;
    }
    if (tabs->count >= KHOME_MAX_TABS) {
        int drop = -1;
        int i;
        for (i = 0; i < tabs->count; i++) {
            if (i != tabs->active) {
                drop = i;
                break;
            }
        }
        if (drop < 0) {
            drop = 0;
        }
        memmove(&tabs->tabs[drop], &tabs->tabs[drop + 1],
                (size_t)(tabs->count - drop - 1) * sizeof(Tab));
        tabs->count--;
        if (tabs->active > drop) {
            tabs->active--;
        }
    }
    memcpy(tabs->tabs[tabs->count].url, url, len + 1);
    tabs->count++;
    tabs->active = tabs->count - 1;
    return tabs->active;
}

int tabs_close(TabList *tabs, int index)
{
    if (tabs == NULL || index < 0 || index >= tabs->count) {
        return -1;
    }
    memmove(&tabs->tabs[index], &tabs->tabs[index + 1],
            (size_t)(tabs->count - index - 1) * sizeof(Tab));
    tabs->count--;
    if (tabs->count == 0) {
        tabs->active = -1;
        return -1;
    }
    if (index < tabs->active) {
        tabs->active--;
    } else if (index == tabs->active && tabs->active >= tabs->count) {
        tabs->active = tabs->count - 1;
    }
    return tabs->active;
}

int tabs_select(TabList *tabs, int index)
{
    if (tabs == NULL || index < 0 || index >= tabs->count) {
        return -1;
    }
    tabs->active = index;
    return tabs->active;
}

int tabs_set_active_url(TabList *tabs, const char *url)
{
    size_t len;
    int active = active_index(tabs);

    if (active < 0 || url == NULL) {
        return -1;
    }
    len = strlen(url);
    if (len == 0 || len >= KHOME_TAB_URL_MAX) {
        return -1;
    }
    tabs->active = active;
    memcpy(tabs->tabs[active].url, url, len + 1);
    return 0;
}

int tabs_set_url(TabList *tabs, int index, const char *url)
{
    size_t len;

    if (tabs == NULL || index < 0 || index >= tabs->count || url == NULL) {
        return -1;
    }
    len = strlen(url);
    if (len == 0 || len >= KHOME_TAB_URL_MAX) {
        return -1;
    }
    memcpy(tabs->tabs[index].url, url, len + 1);
    return 0;
}

const char *tabs_active_url(const TabList *tabs)
{
    int active = active_index(tabs);
    if (active < 0) {
        return NULL;
    }
    return tabs->tabs[active].url;
}

void tabs_url_parts(const char *url, char *host, size_t host_cap, char *path,
                    size_t path_cap)
{
    const char *p;
    const char *host_begin;
    const char *host_end;
    const char *path_begin;
    const char *path_end;
    const char *scheme;
    const char *at;
    size_t host_len;
    size_t path_len;

    if (host != NULL && host_cap > 0) {
        host[0] = 0;
    }
    if (path != NULL && path_cap > 0) {
        path[0] = path_cap > 1 ? '/' : 0;
        if (path_cap > 1) {
            path[1] = 0;
        }
    }
    if (url == NULL) {
        return;
    }
    p = url;
    scheme = strstr(p, "://");
    if (scheme != NULL) {
        host_begin = scheme + 3;
    } else if (p[0] == '/' && p[1] == '/') {
        host_begin = p + 2;
    } else {
        host_begin = p;
    }
    host_end = host_begin + strcspn(host_begin, "/?#");
    at = memchr(host_begin, '@', (size_t)(host_end - host_begin));
    if (at != NULL) {
        host_begin = at + 1;
    }
    host_len = (size_t)(host_end - host_begin);
    if (host_len > 4 && (host_begin[0] == 'w' || host_begin[0] == 'W') &&
        (host_begin[1] == 'w' || host_begin[1] == 'W') &&
        (host_begin[2] == 'w' || host_begin[2] == 'W') && host_begin[3] == '.') {
        host_begin += 4;
        host_len -= 4;
    }
    copy_short(host, host_cap, host_begin, host_len,
               host_cap > 0 ? host_cap - 1 : 0);

    path_begin = host_end;
    path_len = 0;
    if (*path_begin == '/') {
        path_end = path_begin + strcspn(path_begin, "?#");
        path_len = (size_t)(path_end - path_begin);
    }
    if (path_len > 0) {
        copy_short(path, path_cap, path_begin, path_len, KHOME_TAB_PATH_CHARS);
    }
}

static int is_unreserved(unsigned char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9')) {
        return 1;
    }
    switch (c) {
    case '-':
    case '_':
    case '.':
    case '!':
    case '~':
    case '*':
    case '\'':
    case '(':
    case ')':
        return 1;
    default:
        return 0;
    }
}

int tabs_encode_component(const char *src, char *out, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t needed = 0;
    size_t used = 0;
    size_t i;

    if (out != NULL && cap > 0) {
        out[0] = 0;
    }
    if (src == NULL || out == NULL || cap == 0) {
        return -1;
    }
    for (i = 0; src[i] != 0; i++) {
        needed += is_unreserved((unsigned char)src[i]) ? 1u : 3u;
    }
    if (needed + 1 > cap) {
        return -1;
    }
    for (i = 0; src[i] != 0; i++) {
        unsigned char c = (unsigned char)src[i];
        if (is_unreserved(c)) {
            out[used++] = (char)c;
        } else {
            out[used++] = '%';
            out[used++] = hex[c >> 4];
            out[used++] = hex[c & 0x0F];
        }
    }
    out[used] = 0;
    return (int)used;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* One 2-hex-digit byte, or -1. */
static int hex_byte(const char *s)
{
    int hi = hex_digit(s[0]);
    int lo = hex_digit(s[1]);

    if (hi < 0 || lo < 0) {
        return -1;
    }
    return (hi << 4) | lo;
}

/* The UTF-16LE code unit at hex + at, or -1. */
static long hex_unit(const char *hex, size_t at)
{
    int low = hex_byte(hex + at);
    int high = hex_byte(hex + at + 2);

    if (low < 0 || high < 0) {
        return -1;
    }
    return (long)((unsigned)low | ((unsigned)high << 8));
}

int tabs_utf16_hex_decode(const char *hex, char *out, size_t cap)
{
    size_t len;
    size_t i = 0;
    size_t used = 0;

    if (out != NULL && cap > 0) {
        out[0] = 0;
    }
    if (hex == NULL || out == NULL || cap == 0) {
        return -1;
    }
    len = strlen(hex);
    if (len % 4 != 0) {
        return -1;
    }
    while (i < len) {
        long unit = hex_unit(hex, i);
        unsigned long code;
        unsigned char enc[4];
        size_t n;
        size_t j;

        i += 4;
        if (unit < 0) {
            goto fail;
        }
        code = (unsigned long)unit;
        if (code >= 0xD800 && code <= 0xDBFF) {
            long low;

            if (i >= len) {
                goto fail;
            }
            low = hex_unit(hex, i);
            if (low < 0xDC00 || low > 0xDFFF) {
                goto fail;
            }
            i += 4;
            code = 0x10000ul + ((code - 0xD800ul) << 10) +
                   ((unsigned long)low - 0xDC00ul);
        } else if (code >= 0xDC00 && code <= 0xDFFF) {
            goto fail;
        } else if (code == 0) {
            goto fail;
        }
        if (code < 0x80) {
            enc[0] = (unsigned char)code;
            n = 1;
        } else if (code < 0x800) {
            enc[0] = (unsigned char)(0xC0 | (code >> 6));
            enc[1] = (unsigned char)(0x80 | (code & 0x3F));
            n = 2;
        } else if (code < 0x10000) {
            enc[0] = (unsigned char)(0xE0 | (code >> 12));
            enc[1] = (unsigned char)(0x80 | ((code >> 6) & 0x3F));
            enc[2] = (unsigned char)(0x80 | (code & 0x3F));
            n = 3;
        } else {
            enc[0] = (unsigned char)(0xF0 | (code >> 18));
            enc[1] = (unsigned char)(0x80 | ((code >> 12) & 0x3F));
            enc[2] = (unsigned char)(0x80 | ((code >> 6) & 0x3F));
            enc[3] = (unsigned char)(0x80 | (code & 0x3F));
            n = 4;
        }
        if (used + n + 1 > cap) {
            goto fail;
        }
        for (j = 0; j < n; j++) {
            out[used++] = (char)enc[j];
        }
    }
    out[used] = 0;
    return (int)used;
fail:
    out[0] = 0;
    return -1;
}

static int title_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
           c == '\f';
}

/* Collapses whitespace runs to one space and trims both ends in place. */
static void title_clean(char *s)
{
    size_t in = 0;
    size_t out = 0;
    int gap = 0;

    while (s[in] != 0) {
        if (title_space(s[in])) {
            gap = out > 0;
            in++;
            continue;
        }
        if (gap) {
            s[out++] = ' ';
            gap = 0;
        }
        s[out++] = s[in++];
    }
    s[out] = 0;
}

int tabs_browser_row_parse(const char *row, char *url, size_t url_cap,
                           char *title, size_t title_cap)
{
    char url_hex[KHOME_TAB_URL_MAX * 4];
    char title_hex[KHOME_PAGE_TITLE_MAX * 4];
    const char *sep;
    size_t url_len;
    size_t title_len;

    if (url != NULL && url_cap > 0) {
        url[0] = 0;
    }
    if (title != NULL && title_cap > 0) {
        title[0] = 0;
    }
    if (row == NULL || url == NULL || url_cap == 0) {
        return -1;
    }
    sep = strchr(row, ' ');
    if (sep == NULL) {
        return -1;
    }
    url_len = (size_t)(sep - row);
    if (url_len == 0 || url_len >= sizeof(url_hex)) {
        return -1;
    }
    memcpy(url_hex, row, url_len);
    url_hex[url_len] = 0;
    if (tabs_utf16_hex_decode(url_hex, url, url_cap) < 0) {
        return -1;
    }
    if (title == NULL || title_cap == 0) {
        return 0;
    }
    title_len = strcspn(sep + 1, "\r\n");
    if (title_len >= sizeof(title_hex)) {
        return 0;
    }
    memcpy(title_hex, sep + 1, title_len);
    title_hex[title_len] = 0;
    if (tabs_utf16_hex_decode(title_hex, title, title_cap) < 0) {
        title[0] = 0;
        return 0;
    }
    title_clean(title);
    return 0;
}

static const char *json_skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    return p;
}

static int json_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

/* p points at the opening quote; returns the byte after the closing quote.
 * Escapes and raw control characters are checked too, so a value that merely
 * contains the right word cannot validate a broken object. */
static const char *json_string_end(const char *p)
{
    if (p == NULL || *p != '"') {
        return NULL;
    }
    p++;
    while (*p != 0) {
        unsigned char c = (unsigned char)*p;

        if (c == '"') {
            return p + 1;
        }
        if (c == '\\') {
            p++;
            if (*p == '"' || *p == '\\' || *p == '/' || *p == 'b' ||
                *p == 'f' || *p == 'n' || *p == 'r' || *p == 't') {
                p++;
            } else if (*p == 'u') {
                int i;

                p++;
                for (i = 0; i < 4; i++) {
                    if (!json_hex_digit(p[i])) {
                        return NULL;
                    }
                }
                p += 4;
            } else {
                return NULL;
            }
        } else if (c < 0x20) {
            return NULL;
        } else {
            p++;
        }
    }
    return NULL;
}

static const char *json_number_end(const char *p)
{
    if (*p == '-') {
        p++;
    }
    if (*p == '0') {
        p++;
    } else if (*p >= '1' && *p <= '9') {
        do {
            p++;
        } while (*p >= '0' && *p <= '9');
    } else {
        return NULL;
    }
    if (*p == '.') {
        p++;
        if (!(*p >= '0' && *p <= '9')) {
            return NULL;
        }
        do {
            p++;
        } while (*p >= '0' && *p <= '9');
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') {
            p++;
        }
        if (!(*p >= '0' && *p <= '9')) {
            return NULL;
        }
        do {
            p++;
        } while (*p >= '0' && *p <= '9');
    }
    return p;
}

static const char *json_object_end(const char *p);
static const char *json_array_end(const char *p);

static const char *json_value_end(const char *p)
{
    p = json_skip_ws(p);
    if (*p == '"') {
        return json_string_end(p);
    }
    if (*p == '{') {
        return json_object_end(p);
    }
    if (*p == '[') {
        return json_array_end(p);
    }
    if (strncmp(p, "true", 4) == 0) {
        return p + 4;
    }
    if (strncmp(p, "false", 5) == 0) {
        return p + 5;
    }
    if (strncmp(p, "null", 4) == 0) {
        return p + 4;
    }
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        return json_number_end(p);
    }
    return NULL;
}

static const char *json_object_end(const char *p)
{
    if (*p != '{') {
        return NULL;
    }
    p = json_skip_ws(p + 1);
    if (*p == '}') {
        return p + 1;
    }
    for (;;) {
        const char *key_end;

        key_end = json_string_end(p);
        if (key_end == NULL) {
            return NULL;
        }
        p = json_skip_ws(key_end);
        if (*p != ':') {
            return NULL;
        }
        p = json_value_end(p + 1);
        if (p == NULL) {
            return NULL;
        }
        p = json_skip_ws(p);
        if (*p == ',') {
            p = json_skip_ws(p + 1);
            if (*p == '}') {
                return NULL;
            }
            continue;
        }
        if (*p == '}') {
            return p + 1;
        }
        return NULL;
    }
}

static const char *json_array_end(const char *p)
{
    if (*p != '[') {
        return NULL;
    }
    p = json_skip_ws(p + 1);
    if (*p == ']') {
        return p + 1;
    }
    for (;;) {
        p = json_value_end(p);
        if (p == NULL) {
            return NULL;
        }
        p = json_skip_ws(p);
        if (*p == ',') {
            p = json_skip_ws(p + 1);
            if (*p == ']') {
                return NULL;
            }
            continue;
        }
        if (*p == ']') {
            return p + 1;
        }
        return NULL;
    }
}

BrowserOrientation tabs_browser_orientation(const char *json)
{
    const char *p;
    const char *end;
    int found = 0;
    int landscape = 0;

    if (json == NULL) {
        return BROWSER_ORIENTATION_PORTRAIT;
    }
    p = json_skip_ws(json);
    if (*p != '{') {
        return BROWSER_ORIENTATION_PORTRAIT;
    }
    /* Validate the whole document first.  A truncated object such as
     * {"orientation":"landscapeLeft" has a readable value but is still not a
     * settings object khome may act on. */
    end = json_object_end(p);
    if (end == NULL || *json_skip_ws(end) != 0) {
        return BROWSER_ORIENTATION_PORTRAIT;
    }

    p = json_skip_ws(p + 1);
    if (*p == '}') {
        return BROWSER_ORIENTATION_PORTRAIT;
    }
    for (;;) {
        const char *key_end;
        size_t key_len;
        int is_orientation;

        key_end = json_string_end(p);
        if (key_end == NULL) {
            return BROWSER_ORIENTATION_PORTRAIT;
        }
        key_len = (size_t)(key_end - p - 2);
        is_orientation =
            key_len == sizeof("orientation") - 1 &&
            memcmp(p + 1, "orientation", key_len) == 0;
        p = json_skip_ws(key_end);
        if (*p != ':') {
            return BROWSER_ORIENTATION_PORTRAIT;
        }
        p = json_skip_ws(p + 1);
        if (is_orientation && !found) {
            const char *value_end = json_string_end(p);

            if (value_end != NULL) {
                size_t value_len = (size_t)(value_end - p - 2);

                landscape = value_len == sizeof("landscapeLeft") - 1 &&
                            memcmp(p + 1, "landscapeLeft", value_len) == 0;
            }
            found = 1;
        }
        p = json_value_end(p);
        if (p == NULL) {
            return BROWSER_ORIENTATION_PORTRAIT;
        }
        p = json_skip_ws(p);
        if (*p == ',') {
            p = json_skip_ws(p + 1);
            continue;
        }
        if (*p == '}') {
            break;
        }
        return BROWSER_ORIENTATION_PORTRAIT;
    }
    return found && landscape ? BROWSER_ORIENTATION_LANDSCAPE
                              : BROWSER_ORIENTATION_PORTRAIT;
}

const char *tabs_browser_orientation_value(BrowserOrientation orientation)
{
    return orientation == BROWSER_ORIENTATION_LANDSCAPE ? "landscapeLeft"
                                                        : "auto";
}

static void favorite_name(char *dst, size_t cap, const char *title,
                          size_t title_len, const char *url)
{
    char host[KHOME_TAB_HOST_MAX];
    char path[KHOME_TAB_PATH_MAX];

    if (title != NULL && title_len > 0) {
        copy_short(dst, cap, title, title_len, cap > 0 ? cap - 1 : 0);
        return;
    }
    tabs_url_parts(url, host, sizeof(host), path, sizeof(path));
    if (host[0] != 0) {
        copy_short(dst, cap, host, strlen(host), cap > 0 ? cap - 1 : 0);
    } else {
        copy_short(dst, cap, url, strlen(url), cap > 0 ? cap - 1 : 0);
    }
}

static void favorite_initial(Favorite *fav)
{
    size_t len;

    fav->initial[0] = 0;
    if (fav->name[0] == 0) {
        memcpy(fav->initial, "?", 2);
        return;
    }
    len = utf8_char_len(fav->name, strlen(fav->name));
    if (len >= sizeof(fav->initial)) {
        len = 1;
    }
    memcpy(fav->initial, fav->name, len);
    fav->initial[len] = 0;
    if (len == 1 && fav->initial[0] >= 'a' && fav->initial[0] <= 'z') {
        fav->initial[0] = (char)(fav->initial[0] - 'a' + 'A');
    }
}

int favorites_find(const Favorite *favorites, int count, const char *url)
{
    int i;

    if (favorites == NULL || url == NULL) {
        return -1;
    }
    if (count < 0) {
        return -1;
    }
    if (count > KHOME_MAX_FAVORITES) {
        count = KHOME_MAX_FAVORITES;
    }
    for (i = 0; i < count; i++) {
        if (strcmp(favorites[i].url, url) == 0) {
            return i;
        }
    }
    return -1;
}

int favorites_add(Favorite *favorites, int *count, int max, const char *url,
                  const char *title)
{
    Favorite *fav;
    size_t len;
    int index;

    if (favorites == NULL || count == NULL || url == NULL) {
        return -1;
    }
    len = strlen(url);
    if (len == 0 || len >= KHOME_TAB_URL_MAX) {
        return -1;
    }
    if (max > KHOME_MAX_FAVORITES) {
        max = KHOME_MAX_FAVORITES;
    }
    index = *count;
    if (index < 0 || index >= max) {
        return -1;
    }
    if (favorites_find(favorites, index, url) >= 0) {
        return -1;
    }
    fav = &favorites[index];
    memset(fav, 0, sizeof(*fav));
    memcpy(fav->url, url, len + 1);
    favorite_name(fav->name, sizeof(fav->name), title,
                  title != NULL ? strlen(title) : 0, url);
    favorite_initial(fav);
    *count = index + 1;
    return index;
}

int favorites_remove(Favorite *favorites, int *count, int index)
{
    if (favorites == NULL || count == NULL || index < 0 || index >= *count) {
        return -1;
    }
    memmove(&favorites[index], &favorites[index + 1],
            (size_t)(*count - index - 1) * sizeof(Favorite));
    (*count)--;
    memset(&favorites[*count], 0, sizeof(Favorite));
    return 0;
}

/* A URL field of only whitespace, with or without a tab after it, is no URL. */
static int line_blank(const char *p, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        if (p[i] != ' ' && p[i] != '\t' && p[i] != '\v' && p[i] != '\f') {
            return 0;
        }
    }
    return 1;
}

int favorites_parse(Favorite *out, int max, const char *text)
{
    const char *p;
    int count = 0;

    if (out == NULL || max <= 0) {
        return 0;
    }
    if (max > KHOME_MAX_FAVORITES) {
        max = KHOME_MAX_FAVORITES;
    }
    p = text;
    if (p != NULL && (unsigned char)p[0] == 0xEF &&
        (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) {
        p += 3;
    }
    while (p != NULL && *p != 0 && count < max) {
        const char *nl = strchr(p, '\n');
        const char *tab;
        size_t len = nl != NULL ? (size_t)(nl - p) : strlen(p);
        size_t url_len;

        while (len > 0 && p[len - 1] == '\r') {
            len--;
        }
        tab = memchr(p, '\t', len);
        url_len = tab != NULL ? (size_t)(tab - p) : len;
        if (url_len > 0 && url_len < KHOME_TAB_URL_MAX &&
            !line_blank(p, url_len)) {
            Favorite *fav = &out[count];

            memset(fav, 0, sizeof(*fav));
            memcpy(fav->url, p, url_len);
            fav->url[url_len] = 0;
            if (favorites_find(out, count, fav->url) < 0) {
                favorite_name(fav->name, sizeof(fav->name),
                              tab != NULL ? tab + 1 : "",
                              tab != NULL ? len - url_len - 1 : 0, fav->url);
                favorite_initial(fav);
                count++;
            }
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    return count;
}

int favorites_format(const Favorite *favorites, int count, char *out,
                     size_t cap)
{
    size_t needed = 0;
    size_t used = 0;
    int i;

    if (out != NULL && cap > 0) {
        out[0] = 0;
    }
    if (favorites == NULL || out == NULL || cap == 0) {
        return -1;
    }
    if (count < 0) {
        count = 0;
    }
    if (count > KHOME_MAX_FAVORITES) {
        count = KHOME_MAX_FAVORITES;
    }
    for (i = 0; i < count; i++) {
        needed += strlen(favorites[i].url) + strlen(favorites[i].name) + 2;
    }
    if (needed + 1 > cap) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        size_t url_len = strlen(favorites[i].url);
        size_t name_len = strlen(favorites[i].name);

        memcpy(out + used, favorites[i].url, url_len);
        used += url_len;
        out[used++] = '\t';
        memcpy(out + used, favorites[i].name, name_len);
        used += name_len;
        out[used++] = '\n';
    }
    out[used] = 0;
    return (int)used;
}
