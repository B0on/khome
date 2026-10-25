#include "kindletimes.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Panel-pixel geometry, taken from the N1 and AB mocks.  The drawing scale is
 * 1.0 on the 758x1024 framebuffer and 1072/758 for the design renderer. */
#define KT_W 758
#define KT_M 32
#define KT_COL_W (KT_W - 2 * KT_M)

#define KT_FOOTER_RULE 946
#define KT_PAPER_FOOTER_RULE 958

#define KT_FRONT_TOP 172
#define KT_FRONT_LIMIT 728
#define KT_LIST_TOP 148
#define KT_LIST_LIMIT 942
#define KT_INDEX_TOP 740
#define KT_INDEX_BOTTOM 932

#define KT_RULE_GAP 18
#define KT_RULE_H 1

#define KT_TOP_GAP 22
#define KT_TOP_HEAD 36
#define KT_TOP_HEAD_LH 45
#define KT_TOP_LEAD 21
#define KT_TOP_LEAD_LH 31
#define KT_BRIEF_META 19
#define KT_BRIEF_HEAD 24
#define KT_BRIEF_HEAD_LH 31
#define KT_BRIEF_LEAD 16
#define KT_BRIEF_LEAD_LH 23
#define KT_BRIEF_GAP 7

#define KT_BODY_TOP 80
#define KT_ARTICLE_TEXT_W KT_COL_W
#define KT_ARTICLE_PAGE_LINES 32

#define KT_EDITION_EPOCH_YEAR 2026
#define KT_EDITION_EPOCH_MONTH 10
#define KT_EDITION_EPOCH_DAY 1

static const int kt_text_sizes[KT_TEXT_SIZE_COUNT] = { 26, 30, 36 };

static int kt_scale(float v, float s)
{
    return (int)lroundf((double)v * (double)s);
}

static int kt_scale_len(float v, float s)
{
    int n = kt_scale(v, s);
    return n < 1 ? 1 : n;
}

static void kt_fill(Canvas *c, float x, float y, float w, float h, float s,
                    uint8_t value)
{
    int x0 = kt_scale(x, s);
    int y0 = kt_scale(y, s);
    int x1 = kt_scale(x + w, s);
    int y1 = kt_scale(y + h, s);
    canvas_fill_rect(c, x0, y0, x1 - x0, y1 - y0, value);
}

static void kt_hline(Canvas *c, float x, float y, float w, float t, float s)
{
    kt_fill(c, x, y, w, t, s, 0);
}

static void kt_outline(Canvas *c, float x, float y, float w, float h, float s)
{
    int x0 = kt_scale(x, s);
    int y0 = kt_scale(y, s);
    int x1 = kt_scale(x + w, s);
    int y1 = kt_scale(y + h, s);
    canvas_outline_rect(c, x0, y0, x1 - x0, y1 - y0, kt_scale_len(1.0f, s), 0);
}

static void kt_scale_lines(const TextLine *src, int count, float s,
                           TextLine *dst)
{
    int i;
    for (i = 0; i < count; i++) {
        dst[i] = src[i];
        dst[i].indent *= s;
        dst[i].justify *= s;
    }
}

/* One line, positioned by its ascender top like the PIL mocks. */
static void kt_text_top(Canvas *c, const Fonts *fonts, FontRole role,
                        float size, float x, float top, const char *text,
                        uint8_t color, TextAlign align, float box_w, float s)
{
    const Font *font = fonts_get(fonts, role);
    TextLine line;
    float width;
    float start;
    float px = size * s;
    float left = x * s;

    if (font == NULL || text == NULL || text[0] == 0) {
        return;
    }
    width = text_width(font, px, 0.0f, text);
    if (align == TEXT_CENTER) {
        start = left + (box_w * s - width) * 0.5f;
    } else if (align == TEXT_RIGHT) {
        start = left + box_w * s - width;
    } else {
        start = left;
    }
    line.start = 0;
    line.length = strlen(text);
    line.indent = 0.0f;
    line.justify = 0.0f;
    text_draw_lines(c, font, px, 0.0f, text, &line, 1, 0, 1, start, top * s,
                    width + px, px * 2.0f, px * 2.0f, color);
}

static void kt_draw_lines(Canvas *c, const Fonts *fonts, FontRole role,
                          float size, float leading, const char *text,
                          const TextLine *lines, int count, float x, float y,
                          float w, float s)
{
    const Font *font = fonts_get(fonts, role);
    TextLine scaled[KT_ARTICLE_PAGE_LINES];

    if (font == NULL || text == NULL || lines == NULL || count <= 0) {
        return;
    }
    if (count > KT_ARTICLE_PAGE_LINES) {
        count = KT_ARTICLE_PAGE_LINES;
    }
    kt_scale_lines(lines, count, s, scaled);
    text_draw_lines(c, font, size * s, 0.0f, text, scaled, count, 0, count,
                    x * s, y * s, w * s, (leading * (float)count + size) * s,
                    leading * s, 0);
}

static int kt_line_advance(int count, int leading, int size)
{
    return count > 0 ? (count - 1) * leading + size : 0;
}

/* UTF-8 aware step back; the payload bytes of a character all have the top
 * bits 10, so skipping them stops on the next character's first byte. */
static size_t kt_prev_char(const char *text, size_t len)
{
    if (len == 0) {
        return 0;
    }
    len--;
    while (len > 0 && ((unsigned char)text[len] & 0xC0) == 0x80) {
        len--;
    }
    return len;
}

/* text_wrap with a hard line cap.  When the cap cuts the text, the first
 * max_lines lines stay as they broke and the last one shrinks until it plus
 * an ellipsis reaches the column edge, so the tail reads like the mocks. */
static int kt_wrap_limited(const Font *font, float size, const char *src,
                           float width, int max_lines, TextLine *lines,
                           int out_cap, char *out, size_t out_size)
{
    char candidate[KT_LEAD_MAX + 4];
    size_t end;
    size_t start;
    int truncated = 0;
    int count;
    int i;

    if (out == NULL || out_size == 0) {
        return 0;
    }
    out[0] = 0;
    if (font == NULL || src == NULL || src[0] == 0 || max_lines <= 0) {
        return 0;
    }
    if (max_lines > out_cap - 1) {
        max_lines = out_cap - 1;
    }
    count = text_wrap(font, size, 0.0f, src, width, lines, max_lines,
                      &truncated);
    for (i = 0; i < count; i++) {
        lines[i].indent = 0.0f;
    }
    if (!truncated || count <= 0) {
        snprintf(out, out_size, "%s", src);
        return count;
    }
    i = count - 1;
    start = lines[i].start;
    end = start + lines[i].length;
    if (end + 4 > out_size) {
        end = out_size - 4;
        while (end > start && ((unsigned char)src[end] & 0xC0) == 0x80) {
            end--;
        }
    }
    for (;;) {
        size_t len = end - start;
        memcpy(candidate, src, end);
        candidate[end] = (char)0xE2;
        candidate[end + 1] = (char)0x80;
        candidate[end + 2] = (char)0xA6;
        candidate[end + 3] = 0;
        if (text_width(font, size, 0.0f, candidate + start) <= width ||
            len == 0) {
            break;
        }
        end = kt_prev_char(src, end);
        if (end < start) {
            end = start;
        }
    }
    snprintf(out, out_size, "%s", candidate);
    lines[i].length = end - start + 3;
    lines[i].justify = 0.0f;
    return count;
}

static void kt_clear_justify(TextLine *lines, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        lines[i].justify = 0.0f;
    }
}

static int kt_safe_id(const char *id)
{
    size_t i;
    size_t len;

    if (id == NULL || id[0] == 0) {
        return 0;
    }
    len = strlen(id);
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) {
            return 0;
        }
    }
    return strcmp(id, ".") != 0 && strcmp(id, "..") != 0;
}

static int kt_join(char *out, size_t cap, const char *dir, const char *name)
{
    int n = snprintf(out, cap, "%s/%s", dir, name);
    return n > 0 && (size_t)n < cap;
}

static int kt_read_file(const char *path, size_t cap, char **out)
{
    FILE *f;
    char *buf;
    long size;
    size_t got;

    *out = NULL;
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 ||
        (size_t)size >= cap || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    buf = (char *)malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(f);
        return 0;
    }
    got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) {
        free(buf);
        return 0;
    }
    buf[got] = 0;
    *out = buf;
    return 1;
}

/* Cuts the next line off *cursor, trims CR and advances. */
static char *kt_next_line(char **cursor)
{
    char *line;
    char *nl;
    size_t len;

    if (cursor == NULL || *cursor == NULL) {
        return NULL;
    }
    line = *cursor;
    nl = strchr(line, '\n');
    if (nl != NULL) {
        *nl = 0;
        *cursor = nl + 1;
    } else {
        *cursor = NULL;
    }
    len = strlen(line);
    while (len > 0 && line[len - 1] == '\r') {
        line[--len] = 0;
    }
    return line;
}

static int days_from_civil(int y, int m, int d)
{
    int era;
    unsigned yoe;
    unsigned doy;
    unsigned doe;

    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

int kt_issue_number(int year, int month, int day)
{
    int days = days_from_civil(year, month, day) -
               days_from_civil(KT_EDITION_EPOCH_YEAR, KT_EDITION_EPOCH_MONTH,
                               KT_EDITION_EPOCH_DAY) +
               1;
    return days < 1 ? 1 : days;
}

int kt_edition_id_parse(const char *id, KtEdition *out)
{
    int digits[12];
    int i;

    if (id == NULL || out == NULL || strlen(id) != 13 || id[8] != '-') {
        return 0;
    }
    for (i = 0; i < 12; i++) {
        int pos = i < 8 ? i : i + 1;
        if (id[pos] < '0' || id[pos] > '9') {
            return 0;
        }
        digits[i] = id[pos] - '0';
    }
    {
        int year = digits[0] * 1000 + digits[1] * 100 + digits[2] * 10 +
                   digits[3];
        int month = digits[4] * 10 + digits[5];
        int day = digits[6] * 10 + digits[7];
        int hour = digits[8] * 10 + digits[9];
        int minute = digits[10] * 10 + digits[11];

        if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 ||
            minute > 59) {
            return 0;
        }
        memset(out, 0, sizeof(*out));
        snprintf(out->id, sizeof(out->id), "%s", id);
        out->year = year;
        out->month = month;
        out->day = day;
        out->hour = hour;
        out->minute = minute;
        out->morning = hour < 12;
        out->issue = kt_issue_number(year, month, day);
    }
    return 1;
}

int kt_article_paginate(int line_count, int first_fill, int first_last,
                        int fill, int last_fill, KtArticlePage *pages,
                        int max_pages)
{
    int page = 0;
    int first = 0;
    int remaining = line_count;

    if (pages == NULL || max_pages <= 0) {
        return 0;
    }
    if (first_fill < 1) {
        first_fill = 1;
    }
    if (first_last < 1) {
        first_last = 1;
    }
    if (fill < 1) {
        fill = 1;
    }
    if (last_fill < 1) {
        last_fill = 1;
    }
    for (;;) {
        int last_cap = page == 0 ? first_last : last_fill;
        int full = page == 0 ? first_fill : fill;
        int take;

        if (remaining <= last_cap) {
            pages[page].first = first;
            pages[page].count = remaining;
            page++;
            break;
        }
        /* Leave at least one line for the last page: an empty final page
         * would show the next-article box with no body above it. */
        if (remaining > full) {
            take = full;
        } else {
            take = remaining - 1;
        }
        if (take < 1) {
            take = 1;
        }
        pages[page].first = first;
        pages[page].count = take;
        first += take;
        remaining -= take;
        page++;
        if (page >= max_pages) {
            break;
        }
    }
    return page;
}

int kt_article_page_count(int line_count, int first_capacity, int capacity)
{
    KtArticlePage pages[KT_ARTICLE_PAGE_MAX];

    return kt_article_paginate(line_count, first_capacity, first_capacity,
                               capacity, capacity, pages, KT_ARTICLE_PAGE_MAX);
}

int kt_paper_plan(const KtFit *fit, int count, KtPaperLayout *out)
{
    int page;
    int cursor;
    int i;
    int last;

    if (fit == NULL || out == NULL || count <= 0) {
        return 0;
    }
    if (count > KT_STORY_MAX) {
        count = KT_STORY_MAX;
    }
    memset(out, 0, sizeof(*out));
    for (i = 0; i < KT_STORY_MAX; i++) {
        out->story_page[i] = -1;
    }
    out->pages[0].first = 0;
    out->pages[0].count = 0;
    page = 0;

    out->items[0].story = 0;
    out->items[0].role = KT_ROLE_TOP;
    out->items[0].content_y = KT_FRONT_TOP;
    out->items[0].content_h = fit[0].top_height;
    out->items[0].head_count = fit[0].head_count;
    out->items[0].lead_count = fit[0].lead_count;
    out->items[0].rule_y = KT_FRONT_TOP + fit[0].top_height + KT_RULE_GAP;
    out->item_count = 1;
    out->pages[0].count = 1;
    out->story_page[0] = 0;
    cursor = out->items[0].rule_y + KT_RULE_H + KT_RULE_GAP;

    for (i = 1; i < count; i++) {
        int h = fit[i].brief_height;
        int compact = 0;

        for (;;) {
            int limit = page == 0 ? KT_FRONT_LIMIT : KT_LIST_LIMIT;

            if (cursor + h + KT_RULE_GAP <= limit) {
                break;
            }
            /* The full brief does not fit.  A one-line summary is the next
             * size down; use it rather than leave a column of white space. */
            if (fit[i].compact_height > 0 &&
                fit[i].compact_height < h &&
                cursor + fit[i].compact_height + KT_RULE_GAP <= limit) {
                h = fit[i].compact_height;
                compact = 1;
                break;
            }
            if (page + 1 >= KT_PAGE_MAX) {
                page = -1;
                break;
            }
            page++;
            out->pages[page].first = out->item_count;
            out->pages[page].count = 0;
            cursor = KT_LIST_TOP;
            h = fit[i].brief_height;
            compact = 0;
        }
        if (page < 0) {
            break;
        }
        {
            KtPlanItem *item = &out->items[out->item_count];
            item->story = i;
            item->role = KT_ROLE_BRIEF;
            item->content_y = cursor;
            item->content_h = h;
            item->head_count = fit[i].head_count;
            item->lead_count = compact ? fit[i].compact_lead_count
                                       : fit[i].lead_count;
            item->compact = compact;
            item->rule_y = cursor + h + KT_RULE_GAP;
            out->story_page[i] = page;
            out->pages[page].count++;
            out->item_count++;
            cursor = item->rule_y + KT_RULE_H + KT_RULE_GAP;
        }
    }
    out->page_count = page < 0 ? KT_PAGE_MAX : page + 1;

    last = out->pages[0].first + out->pages[0].count - 1;
    out->index_top = out->items[last].rule_y + KT_RULE_H + KT_RULE_GAP + 12;
    if (out->index_top < KT_INDEX_TOP) {
        out->index_top = KT_INDEX_TOP;
    }
    {
        int room = KT_INDEX_BOTTOM - 22 - 24 - (out->index_top + 40);
        int max_rows = room < 0 ? 0 : room / 24 + 1;
        if (max_rows > KT_INDEX_MAX) {
            max_rows = KT_INDEX_MAX;
        }
        for (i = 0; i < count && out->index_count < max_rows; i++) {
            if (out->story_page[i] > 0) {
                out->index_story[out->index_count++] = i;
            }
        }
    }
    return 1;
}

int kt_paper_load(KtPaper *paper, const char *dir)
{
    char path[KT_PATH_MAX];
    FILE *f;
    char line[8192];
    KtEdition edition;
    char generated[48] = "";
    int have_edition = 0;
    int count = 0;

    if (paper == NULL || dir == NULL) {
        return 0;
    }
    memset(paper, 0, sizeof(*paper));
    snprintf(paper->dir, sizeof(paper->dir), "%s", dir);

    if (kt_join(path, sizeof(path), dir, "edition.tsv") &&
        (f = fopen(path, "r")) != NULL) {
        while (fgets(line, sizeof(line), f) != NULL) {
            char *tab = strchr(line, '\t');
            char *value;
            size_t len;

            if (tab == NULL) {
                continue;
            }
            *tab = 0;
            value = tab + 1;
            len = strlen(value);
            while (len > 0 && (value[len - 1] == '\n' || value[len - 1] == '\r')) {
                value[--len] = 0;
            }
            if (strcmp(line, "id") == 0 &&
                kt_edition_id_parse(value, &edition)) {
                have_edition = 1;
            } else if (strcmp(line, "generated") == 0) {
                snprintf(generated, sizeof(generated), "%s", value);
            }
        }
        fclose(f);
    }
    if (have_edition) {
        paper->edition = edition;
        snprintf(paper->edition.generated, sizeof(paper->edition.generated),
                 "%s", generated);
    }

    if (!kt_join(path, sizeof(path), dir, "index.tsv") ||
        (f = fopen(path, "r")) == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), f) != NULL && count < KT_STORY_MAX) {
        char *field[8];
        int n = 0;
        char *p = line;
        size_t len = strlen(line);

        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = 0;
        }
        if (len == 0) {
            continue;
        }
        field[n++] = p;
        while (n < 8 && (p = strchr(field[n - 1], '\t')) != NULL) {
            *p = 0;
            field[n++] = p + 1;
        }
        if (n != 7 || !kt_safe_id(field[0])) {
            continue;
        }
        memset(&paper->stories[count], 0, sizeof(paper->stories[count]));
        snprintf(paper->stories[count].id, sizeof(paper->stories[count].id),
                 "%s", field[0]);
        snprintf(paper->stories[count].section,
                 sizeof(paper->stories[count].section), "%s", field[1]);
        snprintf(paper->stories[count].source,
                 sizeof(paper->stories[count].source), "%s", field[2]);
        snprintf(paper->stories[count].published,
                 sizeof(paper->stories[count].published), "%s", field[3]);
        paper->stories[count].chars = atoi(field[4]);
        snprintf(paper->stories[count].title,
                 sizeof(paper->stories[count].title), "%s", field[5]);
        snprintf(paper->stories[count].lead,
                 sizeof(paper->stories[count].lead), "%s", field[6]);
        count++;
    }
    fclose(f);
    paper->story_count = count;
    return count > 0;
}

void kt_paper_build(KtPaper *paper, const Fonts *fonts)
{
    KtFit fit[KT_STORY_MAX];
    int i;

    if (paper == NULL || fonts == NULL || paper->story_count <= 0) {
        return;
    }
    memset(fit, 0, sizeof(fit));
    for (i = 0; i < paper->story_count; i++) {
        KtStoryText *t = &paper->text[i];
        int top = i == 0;
        int head_size = top ? KT_TOP_HEAD : KT_BRIEF_HEAD;
        int head_lh = top ? KT_TOP_HEAD_LH : KT_BRIEF_HEAD_LH;
        int lead_size = top ? KT_TOP_LEAD : KT_BRIEF_LEAD;
        int lead_lh = top ? KT_TOP_LEAD_LH : KT_BRIEF_LEAD_LH;
        int max_head = top ? 3 : 2;
        int max_lead = top ? 3 : 2;
        int head_adv;
        int lead_adv;

        memset(t, 0, sizeof(*t));
        t->head_count = kt_wrap_limited(
            fonts_get(fonts, FONT_SERIF_B), (float)head_size,
            paper->stories[i].title, (float)KT_COL_W, max_head, t->head_lines,
            4, t->head, sizeof(t->head));
        t->summary_count = kt_wrap_limited(
            fonts_get(fonts, FONT_SERIF_R), (float)lead_size,
            paper->stories[i].lead, (float)KT_COL_W, max_lead,
            t->summary_lines, 4, t->summary, sizeof(t->summary));
        kt_clear_justify(t->head_lines, t->head_count);
        head_adv = kt_line_advance(t->head_count, head_lh, head_size);
        lead_adv = kt_line_advance(t->summary_count, lead_lh, lead_size);
        fit[i].head_count = t->head_count;
        fit[i].lead_count = t->summary_count;
        if (top) {
            fit[i].top_height =
                KT_TOP_GAP + head_adv + 12 + lead_adv + 10 + 19;
            fit[i].brief_height = fit[i].top_height;
        } else {
            int compact_adv;

            t->compact_count = kt_wrap_limited(
                fonts_get(fonts, FONT_SERIF_R), (float)lead_size,
                paper->stories[i].lead, (float)KT_COL_W, 1, t->compact_lines,
                2, t->compact, sizeof(t->compact));
            compact_adv = kt_line_advance(t->compact_count, lead_lh, lead_size);
            fit[i].brief_height =
                KT_BRIEF_META + head_adv + KT_BRIEF_GAP + lead_adv;
            fit[i].compact_lead_count = t->compact_count;
            fit[i].compact_height =
                KT_BRIEF_META + head_adv + KT_BRIEF_GAP + compact_adv;
            fit[i].top_height = fit[i].brief_height;
        }
    }
    kt_paper_plan(fit, paper->story_count, &paper->layout);
}

int kt_paper_page_of(const KtPaper *paper, int story)
{
    if (paper == NULL || story < 0 || story >= paper->story_count) {
        return -1;
    }
    return paper->layout.story_page[story];
}

int kt_paper_item_at(const KtPaper *paper, int page, int x, int y)
{
    int i;
    int first;
    int end;

    if (paper == NULL || page < 0 || page >= paper->layout.page_count) {
        return -1;
    }
    if (x < KT_M || x >= KT_W - KT_M) {
        return -1;
    }
    first = paper->layout.pages[page].first;
    end = first + paper->layout.pages[page].count;
    for (i = first; i < end; i++) {
        const KtPlanItem *item = &paper->layout.items[i];
        if (y >= item->content_y && y < item->content_y + item->content_h) {
            return item->story;
        }
    }
    return -1;
}

int kt_paper_index_at(const KtPaper *paper, int page, int x, int y)
{
    int i;

    if (paper == NULL || page != 0 || paper->layout.index_count <= 0) {
        return -1;
    }
    if (x < KT_M + 14 || x >= KT_W - KT_M - 14) {
        return -1;
    }
    for (i = 0; i < paper->layout.index_count; i++) {
        int top = paper->layout.index_top + 40 + i * 24;
        if (y >= top && y < top + 24) {
            return paper->layout.index_story[i];
        }
    }
    return -1;
}

static int kt_size_index(int size)
{
    int i;
    for (i = 0; i < KT_TEXT_SIZE_COUNT; i++) {
        if (kt_text_sizes[i] == size) {
            return i;
        }
    }
    return 1;
}

static int kt_article_reflow(KtArticle *a, const Fonts *fonts)
{
    int alloc;
    int head_alloc;
    int truncated = 0;
    int head_adv;
    int first_fill;
    int first_last;
    int fill;
    int last_fill;
    int i;

    if (a == NULL || fonts == NULL || a->body == NULL) {
        return 0;
    }
    a->size_index = kt_size_index(a->size);
    a->size = kt_text_sizes[a->size_index];
    a->line_height = (a->size * 17 + 5) / 10;
    a->head_size = a->size + 6;
    a->head_leading = (a->head_size * 13 + 5) / 10;

    free(a->lines);
    a->lines = NULL;
    a->line_count = 0;
    alloc = (int)strlen(a->body) + 8;
    a->lines = (TextLine *)malloc((size_t)alloc * sizeof(TextLine));
    if (a->lines == NULL) {
        return 0;
    }
    a->line_count = text_wrap(fonts_get(fonts, FONT_SERIF_R), (float)a->size,
                              0.0f, a->body, (float)KT_ARTICLE_TEXT_W,
                              a->lines, alloc, &truncated);

    free(a->head_lines);
    a->head_lines = NULL;
    a->head_count = 0;
    head_alloc = (int)strlen(a->head) + 8;
    a->head_lines = (TextLine *)malloc((size_t)head_alloc * sizeof(TextLine));
    if (a->head_lines == NULL) {
        return 0;
    }
    a->head_count = text_wrap(fonts_get(fonts, FONT_SERIF_B),
                              (float)a->head_size, 0.0f, a->head,
                              (float)KT_ARTICLE_TEXT_W, a->head_lines,
                              head_alloc, &truncated);
    for (i = 0; i < a->head_count; i++) {
        a->head_lines[i].indent = 0.0f;
        a->head_lines[i].justify = 0.0f;
    }
    head_adv = kt_line_advance(a->head_count, a->head_leading, a->head_size);
    a->body_top = 84 + head_adv + 54;

    /* A page that is not the last may use the full body height; only the
     * last page reserves the 70 px next-article box. */
    first_fill = (930 - a->body_top - a->size) / a->line_height + 1;
    fill = (850 - a->size) / a->line_height + 1;
    if (a->has_next) {
        first_last = (840 - a->body_top - a->size) / a->line_height + 1;
        last_fill = (760 - a->size) / a->line_height + 1;
    } else {
        first_last = first_fill;
        last_fill = fill;
    }
    a->page_count = kt_article_paginate(a->line_count, first_fill, first_last,
                                       fill, last_fill, a->pages,
                                       KT_ARTICLE_PAGE_MAX);
    if (a->page < 0) {
        a->page = 0;
    }
    if (a->page >= a->page_count) {
        a->page = a->page_count - 1;
    }
    return 1;
}

int kt_article_open(KtArticle *article, const KtPaper *paper,
                    const Fonts *fonts, int story, int size)
{
    char path[KT_PATH_MAX];
    char *raw = NULL;
    char *cursor;
    char *headline;
    char *meta;
    char *blank;
    char *body;
    KtArticle opened;

    if (article == NULL || paper == NULL || fonts == NULL || story < 0 ||
        story >= paper->story_count) {
        return 0;
    }
    if (!kt_safe_id(paper->stories[story].id)) {
        return 0;
    }
    snprintf(path, sizeof(path), "%s/articles/%s.txt", paper->dir,
             paper->stories[story].id);
    if (!kt_read_file(path, KT_BODY_MAX, &raw)) {
        return 0;
    }
    memset(&opened, 0, sizeof(opened));
    opened.story = story;
    opened.has_next = story + 1 < paper->story_count;
    opened.size = size;
    opened.raw = raw;
    cursor = raw;
    headline = kt_next_line(&cursor);
    meta = kt_next_line(&cursor);
    blank = kt_next_line(&cursor);
    body = cursor != NULL ? cursor : raw + strlen(raw);

    if (headline != NULL && headline[0] != 0) {
        snprintf(opened.head, sizeof(opened.head), "%s", headline);
    } else {
        snprintf(opened.head, sizeof(opened.head), "%s",
                 paper->stories[story].title);
    }
    snprintf(opened.headline, sizeof(opened.headline), "%s", opened.head);
    snprintf(opened.source, sizeof(opened.source), "%s",
             paper->stories[story].source);
    snprintf(opened.published, sizeof(opened.published), "%s",
             paper->stories[story].published);
    if (meta != NULL && meta[0] != 0) {
        char *tab = strchr(meta, '\t');
        if (tab != NULL) {
            *tab = 0;
            if (meta[0] != 0) {
                snprintf(opened.source, sizeof(opened.source), "%s", meta);
            }
            if (tab[1] != 0) {
                char *next = strchr(tab + 1, '\t');
                if (next != NULL) {
                    *next = 0;
                }
                snprintf(opened.published, sizeof(opened.published), "%s",
                         tab + 1);
            }
        }
    }
    (void)blank;
    opened.body = body;
    if (!kt_article_reflow(&opened, fonts)) {
        kt_article_close(&opened);
        return 0;
    }
    kt_article_close(article);
    *article = opened;
    return 1;
}

int kt_article_set_size(KtArticle *article, const KtPaper *paper,
                        const Fonts *fonts, int size)
{
    if (article == NULL || paper == NULL || fonts == NULL ||
        article->raw == NULL) {
        return 0;
    }
    if (kt_size_index(size) == article->size_index && article->lines != NULL) {
        return 1;
    }
    article->has_next = article->story + 1 < paper->story_count;
    article->size = size;
    return kt_article_reflow(article, fonts);
}

void kt_article_close(KtArticle *article)
{
    if (article == NULL) {
        return;
    }
    free(article->raw);
    free(article->lines);
    free(article->head_lines);
    memset(article, 0, sizeof(*article));
}

static void kt_article_span(const KtArticle *a, int page, int *first,
                            int *count)
{
    if (a == NULL || page < 0 || page >= a->page_count) {
        *first = 0;
        *count = 0;
        return;
    }
    *first = a->pages[page].first;
    *count = a->pages[page].count;
}

int kt_article_next_at(const KtArticle *article, int x, int y)
{
    int first;
    int count;
    int body_y;
    int bottom;
    int box;

    if (article == NULL || article->page != article->page_count - 1) {
        return 0;
    }
    if (x < KT_M || x >= KT_W - KT_M) {
        return 0;
    }
    kt_article_span(article, article->page, &first, &count);
    (void)first;
    body_y = article->page == 0 ? article->body_top : KT_BODY_TOP;
    bottom = body_y + (count > 0 ? (count - 1) * article->line_height : 0) +
             article->size;
    box = bottom + 36;
    return y >= box && y < box + 70;
}

int kt_article_font_at(int x, int y)
{
    return x >= KT_W - KT_M - 34 && x < KT_W - KT_M && y >= 26 && y < 60;
}

static void kt_date_long(const KtEdition *e, char *out, size_t cap)
{
    static const char *const weekdays[7] = { "日", "月", "火", "水",
                                             "木", "金", "土" };
    int wd = 0;

    if (e->year <= 0) {
        out[0] = 0;
        return;
    }
    /* 1970-01-01 was a Thursday. */
    wd = (days_from_civil(e->year, e->month, e->day) % 7 + 11) % 7;
    snprintf(out, cap, "%04d年%d月%d日 %s曜日", e->year, e->month, e->day,
             weekdays[wd]);
}

static void kt_date_short(const KtEdition *e, char *out, size_t cap)
{
    if (e->year <= 0) {
        out[0] = 0;
        return;
    }
    snprintf(out, cap, "%04d年%d月%d日", e->year, e->month, e->day);
}

static void kt_edition_label(const KtEdition *e, char *out, size_t cap)
{
    snprintf(out, cap, "%s", e->morning ? "朝刊" : "夕刊");
}

static void kt_footer(Canvas *c, const Fonts *fonts, int page, int total,
                      float rule_y, float s)
{
    char number[32];

    kt_hline(c, KT_M, rule_y, KT_COL_W, KT_RULE_H, s);
    kt_text_top(c, fonts, FONT_SANS_M, 12, KT_M, rule_y + 10, "Kindle Times",
                85, TEXT_LEFT, KT_COL_W, s);
    snprintf(number, sizeof(number), "%d / %d", page + 1, total);
    kt_text_top(c, fonts, FONT_SANS_M, 14, KT_M, rule_y + 8, number, 85,
                TEXT_RIGHT, KT_COL_W, s);
}

/* The article mock puts the page number left and the brand right, the reverse
 * of the paper's footer. */
static void kt_article_footer(Canvas *c, const Fonts *fonts, int page,
                              int total, float s)
{
    char number[32];

    kt_hline(c, KT_M, KT_FOOTER_RULE, KT_COL_W, KT_RULE_H, s);
    snprintf(number, sizeof(number), "%d / %d", page + 1, total);
    kt_text_top(c, fonts, FONT_SANS_M, 17, KT_M, KT_FOOTER_RULE + 12, number, 0,
                TEXT_LEFT, KT_COL_W, s);
    kt_text_top(c, fonts, FONT_SANS_M, 12, KT_M, KT_FOOTER_RULE + 14,
                "Kindle Times", 85, TEXT_RIGHT, KT_COL_W, s);
}

static void kt_masthead(Canvas *c, const Fonts *fonts, const KtPaper *paper,
                        float s)
{
    char date[64];
    char label[16];
    char issue[48];

    kt_hline(c, KT_M, 20, KT_COL_W, KT_RULE_H, s);
    kt_text_top(c, fonts, FONT_SERIF_B, 62, KT_M, 30, "Kindle Times", 0,
                TEXT_CENTER, KT_COL_W, s);
    kt_date_long(&paper->edition, date, sizeof(date));
    if (date[0] != 0) {
        kt_text_top(c, fonts, FONT_SANS_M, 13, KT_M, 124, date, 85, TEXT_LEFT,
                    KT_COL_W, s);
    }
    kt_edition_label(&paper->edition, label, sizeof(label));
    kt_text_top(c, fonts, FONT_SANS_M, 14, KT_M, 124, label, 0, TEXT_CENTER,
                KT_COL_W, s);
    snprintf(issue, sizeof(issue), "第 %d 号", paper->edition.issue);
    kt_text_top(c, fonts, FONT_SANS_M, 13, KT_M, 124, issue, 85, TEXT_RIGHT,
                KT_COL_W, s);
    kt_hline(c, KT_M, 150, KT_COL_W, 3, s);
    kt_hline(c, KT_M, 156, KT_COL_W, KT_RULE_H, s);
}

static void kt_page_header(Canvas *c, const Fonts *fonts, const KtPaper *paper,
                           int page, float s)
{
    char date[64];
    char label[16];
    char title[48];
    char range[32];
    int first;
    int end;
    int first_story;
    int last_story;

    kt_hline(c, KT_M, 20, KT_COL_W, KT_RULE_H, s);
    kt_text_top(c, fonts, FONT_SERIF_B, 28, KT_M, 28, "Kindle Times", 0,
                TEXT_LEFT, KT_COL_W, s);
    kt_edition_label(&paper->edition, label, sizeof(label));
    kt_text_top(c, fonts, FONT_SANS_M, 12, KT_M, 36, label, 85, TEXT_CENTER,
                KT_COL_W, s);
    kt_date_short(&paper->edition, date, sizeof(date));
    if (date[0] != 0) {
        kt_text_top(c, fonts, FONT_SANS_M, 12, KT_M, 36, date, 85, TEXT_RIGHT,
                    KT_COL_W, s);
    }
    kt_hline(c, KT_M, 70, KT_COL_W, KT_RULE_H, s);
    snprintf(title, sizeof(title), "第 %d 面　総合", page + 1);
    kt_text_top(c, fonts, FONT_SERIF_B, 30, KT_M, 84, title, 0, TEXT_LEFT,
                KT_COL_W, s);
    first = paper->layout.pages[page].first;
    end = first + paper->layout.pages[page].count;
    if (end > first) {
        first_story = paper->layout.items[first].story + 1;
        last_story = paper->layout.items[end - 1].story + 1;
        if (first_story == last_story) {
            snprintf(range, sizeof(range), "%02d", first_story);
        } else {
            snprintf(range, sizeof(range), "%02d—%02d", first_story,
                     last_story);
        }
        kt_text_top(c, fonts, FONT_SANS_M, 17, KT_M, 94, range, 85,
                    TEXT_RIGHT, KT_COL_W, s);
    }
    kt_hline(c, KT_M, 132, KT_COL_W, KT_RULE_H, s);
}

static int kt_story_minutes(const KtStory *story)
{
    int minutes = (story->chars + 399) / 400;
    return minutes < 1 ? 1 : minutes;
}

static void kt_draw_index(Canvas *c, const Fonts *fonts, const KtPaper *paper,
                          float s)
{
    char label[32];
    char sections[160];
    size_t used = 0;
    int i;

    if (paper->layout.index_count <= 0) {
        return;
    }
    kt_outline(c, KT_M, (float)paper->layout.index_top, KT_COL_W,
               (float)(KT_INDEX_BOTTOM - paper->layout.index_top), s);
    kt_text_top(c, fonts, FONT_SERIF_B, 19, KT_M + 14,
                (float)paper->layout.index_top + 10, "きょうの紙面", 0,
                TEXT_LEFT, KT_COL_W - 28, s);
    if (paper->layout.page_count <= 2) {
        snprintf(label, sizeof(label), "2 面");
    } else {
        snprintf(label, sizeof(label), "2—%d 面", paper->layout.page_count);
    }
    kt_text_top(c, fonts, FONT_SANS_M, 11, KT_M, (float)paper->layout.index_top + 14,
                label, 85, TEXT_RIGHT, KT_COL_W - 28, s);
    {
        const Font *sans = fonts_get(fonts, FONT_SANS_M);
        float section_w = 0.0f;
        float inner_left = (float)KT_M + 14.0f;
        float inner_right = (float)(KT_W - KT_M - 14);
        float title_x;
        float title_w;

        /* The name column takes the widest section, so a long name such as
         * テクノロジー never reaches into the headline column. */
        for (i = 0; i < paper->story_count; i++) {
            float w = text_width(sans, 10.0f, 0.0f,
                                 paper->stories[i].section);
            if (w > section_w) {
                section_w = w;
            }
        }
        title_x = inner_left + section_w + 8.0f;
        title_w = inner_right - 20.0f - 6.0f - title_x;
        if (title_w < 40.0f) {
            title_w = 40.0f;
        }
        for (i = 0; i < paper->layout.index_count; i++) {
            int story = paper->layout.index_story[i];
            int y = paper->layout.index_top + 40 + i * 24;
            char title[KT_TITLE_MAX];
            char number[16];
            const Font *font = fonts_get(fonts, FONT_SERIF_B);

            kt_text_top(c, fonts, FONT_SANS_M, 10, inner_left, (float)y + 3,
                        paper->stories[story].section, 85, TEXT_LEFT, section_w,
                        s);
            text_ellipsize(font, 14.0f * s, 0.0f, paper->stories[story].title,
                           title_w * s, title, sizeof(title));
            kt_text_top(c, fonts, FONT_SERIF_B, 14, title_x, (float)y, title, 0,
                        TEXT_LEFT, title_w, s);
            snprintf(number, sizeof(number), "%d",
                     paper->layout.story_page[story] + 1);
            kt_text_top(c, fonts, FONT_SANS_M, 10, KT_M, (float)y + 2, number,
                        85, TEXT_RIGHT, KT_COL_W - 14, s);
        }
    }
    for (i = 0; i < paper->story_count; i++) {
        const char *section = paper->stories[i].section;
        size_t len = strlen(section);
        int seen = 0;
        int j;

        if (len == 0) {
            continue;
        }
        for (j = 0; j < i; j++) {
            if (strcmp(paper->stories[j].section, section) == 0) {
                seen = 1;
                break;
            }
        }
        if (seen || used + len + 4 >= sizeof(sections)) {
            continue;
        }
        if (used > 0) {
            const char *sep = "・";
            memcpy(sections + used, sep, 3);
            used += 3;
        }
        memcpy(sections + used, section, len);
        used += len;
    }
    sections[used] = 0;
    kt_text_top(c, fonts, FONT_SANS_M, 10, KT_M + 14,
                (float)(KT_INDEX_BOTTOM - 22), sections, 85, TEXT_LEFT,
                KT_COL_W - 28, s);
}

static void kt_draw_item(Canvas *c, const Fonts *fonts, const KtPaper *paper,
                         const KtPlanItem *item, float s)
{
    const KtStory *story = &paper->stories[item->story];
    const KtStoryText *text = &paper->text[item->story];
    char meta[256];
    int y = item->content_y;
    int head_y;
    int lead_y;
    int lead_adv;

    if (item->role == KT_ROLE_TOP) {
        snprintf(meta, sizeof(meta), "%02d　%s　・　%s", item->story + 1,
                 story->section, story->source);
        kt_text_top(c, fonts, FONT_SANS_M, 12, KT_M, (float)y, meta, 85,
                    TEXT_LEFT, KT_COL_W, s);
        head_y = y + KT_TOP_GAP;
        kt_draw_lines(c, fonts, FONT_SERIF_B, KT_TOP_HEAD, KT_TOP_HEAD_LH,
                      text->head, text->head_lines, item->head_count, KT_M,
                      (float)head_y, KT_COL_W, s);
        lead_y = head_y +
                 kt_line_advance(item->head_count, KT_TOP_HEAD_LH, KT_TOP_HEAD) +
                 12;
        lead_adv = kt_line_advance(item->lead_count, KT_TOP_LEAD_LH, KT_TOP_LEAD);
        kt_draw_lines(c, fonts, FONT_SERIF_R, KT_TOP_LEAD, KT_TOP_LEAD_LH,
                      text->summary, text->summary_lines, item->lead_count,
                      KT_M, (float)lead_y, KT_COL_W, s);
        snprintf(meta, sizeof(meta), "%s　%s　・　約%d分", story->source,
                 story->published, kt_story_minutes(story));
        kt_text_top(c, fonts, FONT_SANS_M, 12, KT_M,
                    (float)(lead_y + lead_adv + 10), meta, 85, TEXT_LEFT,
                    KT_COL_W, s);
    } else {
        snprintf(meta, sizeof(meta), "%02d　%s　・　%s　・　%s",
                 item->story + 1, story->section, story->source,
                 story->published);
        kt_text_top(c, fonts, FONT_SANS_M, 11, KT_M, (float)y, meta, 85,
                    TEXT_LEFT, KT_COL_W, s);
        head_y = y + KT_BRIEF_META;
        kt_draw_lines(c, fonts, FONT_SERIF_B, KT_BRIEF_HEAD, KT_BRIEF_HEAD_LH,
                      text->head, text->head_lines, item->head_count, KT_M,
                      (float)head_y, KT_COL_W, s);
        lead_y = head_y +
                 kt_line_advance(item->head_count, KT_BRIEF_HEAD_LH,
                                 KT_BRIEF_HEAD) +
                 KT_BRIEF_GAP;
        kt_draw_lines(c, fonts, FONT_SERIF_R, KT_BRIEF_LEAD, KT_BRIEF_LEAD_LH,
                      item->compact ? text->compact : text->summary,
                      item->compact ? text->compact_lines : text->summary_lines,
                      item->lead_count, KT_M, (float)lead_y, KT_COL_W, s);
    }
    kt_hline(c, KT_M, (float)item->rule_y, KT_COL_W, KT_RULE_H, s);
}

void kt_draw_paper(Canvas *c, const Fonts *fonts, const KtPaper *paper,
                   int page, float s)
{
    int i;
    int first;
    int end;

    if (c == NULL || fonts == NULL || paper == NULL ||
        paper->layout.page_count <= 0) {
        return;
    }
    if (page < 0) {
        page = 0;
    }
    if (page >= paper->layout.page_count) {
        page = paper->layout.page_count - 1;
    }
    if (page == 0) {
        kt_masthead(c, fonts, paper, s);
    } else {
        kt_page_header(c, fonts, paper, page, s);
    }
    first = paper->layout.pages[page].first;
    end = first + paper->layout.pages[page].count;
    for (i = first; i < end; i++) {
        kt_draw_item(c, fonts, paper, &paper->layout.items[i], s);
    }
    if (page == 0) {
        kt_draw_index(c, fonts, paper, s);
    }
    kt_footer(c, fonts, page, paper->layout.page_count, KT_PAPER_FOOTER_RULE,
              s);
}

static void kt_article_header(Canvas *c, const Fonts *fonts,
                              const KtArticle *article, float s)
{
    char label[KT_SOURCE_MAX + 8];
    char title[KT_TITLE_MAX];
    float label_w;

    kt_hline(c, KT_M, 24, KT_COL_W, KT_RULE_H, s);
    snprintf(label, sizeof(label), "%s ・ ", article->source);
    kt_text_top(c, fonts, FONT_SANS_M, 13, KT_M, 34, label, 85, TEXT_LEFT,
                KT_COL_W, s);
    label_w = text_width(fonts_get(fonts, FONT_SANS_M), 13.0f * s, 0.0f, label);
    text_ellipsize(fonts_get(fonts, FONT_SERIF_B), 15.0f * s, 0.0f,
                   article->headline,
                   (KT_COL_W - 36.0f) * s - label_w, title, sizeof(title));
    kt_text_top(c, fonts, FONT_SERIF_B, 15, KT_M + label_w / s, 32, title, 0,
                TEXT_LEFT, KT_COL_W - label_w / s, s);
    kt_text_top(c, fonts, FONT_SANS_M, 15, KT_W - KT_M - 26, 30, "Aa", 0,
                TEXT_LEFT, 26, s);
    kt_hline(c, KT_M, 62, KT_COL_W, KT_RULE_H, s);
}

void kt_draw_article(Canvas *c, const Fonts *fonts, const KtPaper *paper,
                     const KtArticle *article, int page, int next_story,
                     float s)
{
    int first;
    int count;
    int body_y;

    if (c == NULL || fonts == NULL || paper == NULL || article == NULL ||
        article->lines == NULL) {
        return;
    }
    if (page < 0) {
        page = 0;
    }
    if (page >= article->page_count) {
        page = article->page_count - 1;
    }
    kt_article_header(c, fonts, article, s);
    if (page == 0) {
        int head_adv = kt_line_advance(article->head_count,
                                       article->head_leading,
                                       article->head_size);
        char meta[KT_SOURCE_MAX + 32];

        kt_draw_lines(c, fonts, FONT_SERIF_B, (float)article->head_size,
                      (float)article->head_leading, article->head,
                      article->head_lines, article->head_count, KT_M, 84,
                      KT_COL_W, s);
        snprintf(meta, sizeof(meta), "%s　%s", article->source,
                 article->published);
        kt_text_top(c, fonts, FONT_SANS_M, 13, KT_M, (float)(84 + head_adv + 12),
                    meta, 85, TEXT_LEFT, KT_COL_W, s);
        kt_hline(c, KT_M, (float)(84 + head_adv + 33), KT_COL_W, KT_RULE_H, s);
        body_y = article->body_top;
    } else {
        body_y = KT_BODY_TOP;
    }
    kt_article_span(article, page, &first, &count);
    /* text_draw_lines indexes from the start of the array, so the page slice
     * is passed through a temporary offset copy. */
    if (count > 0) {
        TextLine sliced[KT_ARTICLE_PAGE_LINES];
        int i;
        if (count > KT_ARTICLE_PAGE_LINES) {
            count = KT_ARTICLE_PAGE_LINES;
        }
        for (i = 0; i < count; i++) {
            sliced[i] = article->lines[first + i];
        }
        kt_draw_lines(c, fonts, FONT_SERIF_R, (float)article->size,
                      (float)article->line_height, article->body, sliced, count,
                      KT_M, (float)body_y, KT_COL_W, s);
    }
    if (page == article->page_count - 1 && next_story >= 0 &&
        next_story < paper->story_count) {
        char next_title[KT_TITLE_MAX];
        int bottom = body_y +
                     (count > 0 ? (count - 1) * article->line_height : 0) +
                     article->size;
        int box = bottom + 36;
        const Font *font = fonts_get(fonts, FONT_SERIF_B);

        kt_hline(c, KT_M, (float)box, KT_COL_W, KT_RULE_H, s);
        kt_text_top(c, fonts, FONT_SANS_M, 18, KT_M, (float)(box + 12),
                    "次の記事 ›", 0, TEXT_LEFT, KT_COL_W, s);
        text_ellipsize(font, 19.0f * s, 0.0f, paper->stories[next_story].title,
                       (float)KT_COL_W * s, next_title, sizeof(next_title));
        kt_text_top(c, fonts, FONT_SERIF_B, 19, KT_M, (float)(box + 42),
                    next_title, 0, TEXT_LEFT, KT_COL_W, s);
    }
    kt_article_footer(c, fonts, page, article->page_count, s);
}
