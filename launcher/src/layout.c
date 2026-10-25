#include "layout.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_W 1072
#define SCREEN_H 1448

#define PAPER 255
#define INK 0
#define GRAY1 85
#define GRAY2 153

static int sp(float v, float scale)
{
    return (int)lroundf((double)v * (double)scale);
}

static int slen(float v, float scale)
{
    int n = sp(v, scale);
    return n < 1 ? 1 : n;
}

static void fill_d(Canvas *c, float x, float y, float w, float h, float s,
                   uint8_t value)
{
    int x0 = sp(x, s);
    int y0 = sp(y, s);
    int x1 = sp(x + w, s);
    int y1 = sp(y + h, s);
    canvas_fill_rect(c, x0, y0, x1 - x0, y1 - y0, value);
}

static void rule_d(Canvas *c, float x, float y, float w, float s)
{
    fill_d(c, x, y, w, 3.0f, s, INK);
}

static void outline_d(Canvas *c, float x, float y, float w, float h, float t,
                      float s, uint8_t value)
{
    int x0 = sp(x, s);
    int y0 = sp(y, s);
    int x1 = sp(x + w, s);
    int y1 = sp(y + h, s);
    canvas_outline_rect(c, x0, y0, x1 - x0, y1 - y0, slen(t, s), value);
}

static void circle_outline_d(Canvas *c, float x, float y, float w, float h,
                             float t, float s, uint8_t value)
{
    int x0 = sp(x, s);
    int y0 = sp(y, s);
    int x1 = sp(x + w, s);
    int y1 = sp(y + h, s);
    double rx = (double)(x1 - x0) * 0.5;
    double ry = (double)(y1 - y0) * 0.5;
    double radius = rx < ry ? rx : ry;
    canvas_outline_circle(c, (double)x0 + rx, (double)y0 + ry, radius,
                          (double)slen(t, s), value);
}

static void circle_fill_d(Canvas *c, float x, float y, float w, float h,
                          float s, uint8_t value)
{
    int x0 = sp(x, s);
    int y0 = sp(y, s);
    int x1 = sp(x + w, s);
    int y1 = sp(y + h, s);
    double rx = (double)(x1 - x0) * 0.5;
    double ry = (double)(y1 - y0) * 0.5;
    double radius = rx < ry ? rx : ry;
    canvas_fill_circle(c, (double)x0 + rx, (double)y0 + ry, radius, value);
}

static void line_d(Canvas *c, float x0, float y0, float x1, float y1, float t,
                   float s, uint8_t value)
{
    canvas_line(c, (double)sp(x0, s), (double)sp(y0, s), (double)sp(x1, s),
                (double)sp(y1, s), (double)slen(t, s), value);
}

static void icon_d(Canvas *c, const Icons *icons, int id, float x, float y,
                   float s)
{
    const Bitmap *bmp = icons_get(icons, id, s);
    if (bmp != NULL && bmp->pixels != NULL) {
        canvas_blit(c, sp(x, s), sp(y, s), bmp);
    }
}

/* CSS letter-spacing adds one trailing gap that is part of the line box, so
 * centred/right-aligned runs are positioned as if the final gap existed. */
static float align_width(const Font *font, float size, float ls,
                         const char *text)
{
    float width = text_width(font, size, ls, text);
    if (text != NULL && text[0] != 0) {
        width += ls;
    }
    return width;
}

static float align_x(const Font *font, float size, float ls, const char *text,
                     float x, float boxw, float s, TextAlign align)
{
    float left = (float)sp(x, s);
    float right = (float)sp(x + boxw, s);
    float width;
    if (align == TEXT_LEFT) {
        return left;
    }
    width = align_width(font, size, ls, text);
    if (align == TEXT_CENTER) {
        return left + (right - left - width) * 0.5f;
    }
    return right - width;
}

static void dtext(Canvas *c, const Fonts *fonts, FontRole role, float size,
                  float ls, float x, float baseline, const char *text,
                  uint8_t color, TextAlign align, float boxw, float s)
{
    const Font *font = fonts_get(fonts, role);
    float start;
    if (font == NULL || text == NULL) {
        return;
    }
    start = align_x(font, size * s, ls * s, text, x, boxw, s, align);
    text_draw(c, font, size * s, ls * s, start, (float)sp(baseline, s), text,
              color, TEXT_LEFT, 0.0f);
}

static void status_bar(Canvas *c, const Fonts *fonts, const Icons *icons,
                       const HomeModel *m, float s)
{
    const Font *font = fonts_get(fonts, FONT_SANS_M);
    float size = 24.0f * s;
    float ls = 3.0f * s;
    char ssid[160];
    char line[200];
    float prefix_w;

    /* 680 design px keeps the SSID clear of the battery percentage that sits
     * between it and the Wi-Fi icon (SPEC 2.1 says 796; the battery readout is
     * a stage 2 addition). */
    if (m->wifi_on) {
        snprintf(line, sizeof(line), "WIFI / %s", m->ssid);
        if (text_width(font, size, ls, line) <= 680.0f * s) {
            dtext(c, fonts, FONT_SANS_M, 24, 3, 64, 56, line, INK, TEXT_LEFT,
                  0, s);
        } else {
            const char *prefix = "WIFI / ";
            float pen;
            prefix_w = text_width(font, size, ls, prefix) + ls;
            text_ellipsize(font, size, ls, m->ssid, 680.0f * s - prefix_w, ssid,
                           sizeof(ssid));
            pen = text_draw(c, font, size, ls, (float)sp(64, s),
                            (float)sp(56, s), prefix, INK, TEXT_LEFT, 0.0f);
            text_draw_clip(c, font, size, ls, pen, (float)sp(56, s), ssid, INK,
                           TEXT_LEFT, 0.0f, sp(64, s), 0, sp(860, s),
                           sp(88, s));
        }
        icon_d(c, icons, ICON_WIFI, 896, 32, s);
    } else {
        dtext(c, fonts, FONT_SANS_M, 24, 3, 64, 56, "WIFI / OFF", INK,
              TEXT_LEFT, 0, s);
    }
    if (m->battery_level >= 0) {
        char batt[16];
        snprintf(batt, sizeof(batt), "%d%%", m->battery_level);
        dtext(c, fonts, FONT_SANS_M, 24, 2, 760, 56, batt, GRAY1, TEXT_RIGHT,
              112, s);
    }
    icon_d(c, icons, ICON_BATTERY, 952, 32, s);
    rule_d(c, 0, 88, SCREEN_W, s);
}

static void masthead(Canvas *c, const Fonts *fonts, const HomeModel *m,
                     int settings, float s)
{
    char no[48];
    const Font *font;
    float size, ls, start, pen;
    dtext(c, fonts, FONT_SERIF_B, 40, 12, 64, 152, "FOLIO", INK, TEXT_LEFT, 0,
          s);
    if (settings) {
        dtext(c, fonts, FONT_SERIF_R, 32, 6, 768, 152, "Settings", INK,
              TEXT_RIGHT, 240, s);
    } else {
        font = fonts_get(fonts, FONT_SERIF_R);
        size = 32.0f * s;
        ls = 2.0f * s;
        snprintf(no, sizeof(no), "NO. %03d / %d", m->day_of_year,
                 m->days_in_year);
        start = (float)sp(768, s) + (float)(sp(1008, s) - sp(768, s)) -
                align_width(font, size, ls, no);
        /* The day part is ink and the year part is gray; the shared " / "
         * separator is part of the gray run. */
        {
            char head[48];
            const char *slash = strstr(no, " / ");
            size_t head_len = slash != NULL ? (size_t)(slash - no) : strlen(no);
            memcpy(head, no, head_len);
            head[head_len] = 0;
            pen = text_draw(c, font, size, ls, start, (float)sp(152, s), head,
                            INK, TEXT_LEFT, 0.0f);
            text_draw(c, font, size, ls, pen, (float)sp(152, s), no + head_len,
                      GRAY1, TEXT_LEFT, 0.0f);
        }
    }
    rule_d(c, 0, 176, SCREEN_W, s);
}

static void section_head(Canvas *c, const Fonts *fonts, const char *title,
                         float baseline, float rule_start, float s)
{
    fill_d(c, 64, baseline - 24, 16, 16, s, INK);
    dtext(c, fonts, FONT_SANS_M, 32, 6, 96, baseline, title, INK, TEXT_LEFT, 0,
          s);
    rule_d(c, rule_start, baseline - 16, 1008 - rule_start, s);
}

static void app_cells(Canvas *c, const Fonts *fonts, const Icons *icons,
                      float s)
{
    static const float centers[4] = { 224, 432, 640, 848 };
    static const char *const labels[4] = { "Newspaper", "Manga", "Browser",
                                           "Settings" };
    static const int icon_ids[4] = { ICON_NEWSPAPER, ICON_MANGA, ICON_BROWSER,
                                     ICON_SETTINGS };
    int i;
    for (i = 0; i < 4; i++) {
        float cx = centers[i];
        circle_outline_d(c, cx - 80, 632, 160, 160, 4, s, INK);
        icon_d(c, icons, icon_ids[i], cx - 48, 664, s);
        dtext(c, fonts, FONT_SERIF_B, 32, 2, cx - 104, 832, labels[i], INK,
              TEXT_CENTER, 208, s);
    }
}

/* The stock browser is the only app with a page orientation of its own.  This
 * sits just under its tile and shows the value khome will write next: a tall
 * rectangle for portrait, a wide one for landscape. */
static void browser_orientation_control(Canvas *c, const Fonts *fonts,
                                        const HomeModel *m, float s)
{
    const char *label =
        m->browser_landscape ? "Landscape" : "Portrait";

    outline_d(c, 536, 848, 208, 40, 2, s, INK);
    if (m->browser_landscape) {
        outline_d(c, 556, 861, 24, 14, 2, s, INK);
    } else {
        outline_d(c, 561, 857, 14, 22, 2, s, INK);
    }
    dtext(c, fonts, FONT_SANS_M, 22, 2, 592, 875, label, INK, TEXT_LEFT, 140,
          s);
}

static void clock_date(Canvas *c, const Fonts *fonts, const HomeModel *m,
                       float s)
{
    char clock[16];
    char date[80];
    float pen;
    snprintf(clock, sizeof(clock), "%02d:%02d", m->hour, m->minute);
    dtext(c, fonts, FONT_SERIF_B, 264, -8, 64, 432, clock, INK, TEXT_LEFT, 0,
          s);
    snprintf(date, sizeof(date), "%s ", m->date);
    pen = text_draw(c, fonts_get(fonts, FONT_SERIF_R), 44.0f * s, 4.0f * s,
                    (float)sp(64, s), (float)sp(512, s), date, INK, TEXT_LEFT,
                    0.0f);
    text_draw(c, fonts_get(fonts, FONT_SERIF_R), 44.0f * s, 7.0f * s, pen,
              (float)sp(512, s), m->weekday, GRAY1, TEXT_LEFT, 0.0f);
    rule_d(c, 0, 536, SCREEN_W, s);
}

static int utf8_count(const char *s)
{
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (p != NULL && *p != 0) {
        if ((*p & 0xC0) != 0x80) {
            n++;
        }
        p++;
    }
    return n;
}

static void favorite_tile(Canvas *c, const Fonts *fonts, const Icons *icons,
                          const Favorite *fav, int index, float x, float y,
                          float s)
{
    char number[16];
    char name[256];
    const Font *font = fonts_get(fonts, FONT_SERIF_B);
    int chars = utf8_count(fav->initial);

    (void)icons;
    snprintf(number, sizeof(number), "%02d", index);
    dtext(c, fonts, FONT_SANS_M, 24, 2, x + 232, y + 32, number, GRAY1,
          TEXT_RIGHT, 48, s);
    outline_d(c, x + 96, y + 32, 96, 96, 5, s, INK);
    if (chars >= 2) {
        dtext(c, fonts, FONT_SERIF_B, 30, -1, x + 96, y + 88, fav->initial,
              INK, TEXT_CENTER, 96, s);
    } else {
        dtext(c, fonts, FONT_SERIF_B, 44, 0, x + 96, y + 96, fav->initial, INK,
              TEXT_CENTER, 96, s);
    }
    text_ellipsize(font, 36.0f * s, 1.0f * s, fav->name, 256.0f * s, name,
                   sizeof(name));
    if (font != NULL) {
        float start = align_x(font, 36.0f * s, 1.0f * s, name, x + 16, 256, s,
                              TEXT_CENTER);
        text_draw_clip(c, font, 36.0f * s, 1.0f * s, start,
                       (float)sp(y + 168, s), name, INK, TEXT_LEFT, 0.0f,
                       sp(x + 16, s), sp(y + 134, s), sp(x + 16 + 256, s),
                       sp(y + 134 + 44, s));
    }
}

static void add_row(Canvas *c, const Fonts *fonts, float s)
{
    rule_d(c, 64, 1128, 944, s);
    fill_d(c, 88, 1214, 32, 4, s, INK);
    fill_d(c, 102, 1200, 4, 32, s, INK);
    dtext(c, fonts, FONT_SERIF_B, 40, 2, 144, 1232,
          "Add favorites in the browser", INK, TEXT_LEFT, 0, s);
    line_d(c, 963, 1203, 981, 1220, 4, s, INK);
    line_d(c, 981, 1220, 963, 1237, 4, s, INK);
}

static void footer_rule(Canvas *c, const Fonts *fonts, float s)
{
    rule_d(c, 0, 1328, SCREEN_W, s);
    dtext(c, fonts, FONT_SANS_M, 24, 8, 64, 1400, "FOLIO", INK, TEXT_LEFT, 0,
          s);
}

static void footer_meta(Canvas *c, const Fonts *fonts, const char *text,
                        float s)
{
    footer_rule(c, fonts, s);
    dtext(c, fonts, FONT_SANS_M, 24, 2, 808, 1400, text, GRAY1, TEXT_RIGHT,
          200, s);
}

/* A failed launch replaces the count or pager with one ink line, so the
 * error is visible on home without adding a new banner. */
static void footer_notice(Canvas *c, const Fonts *fonts, const char *text,
                          float s)
{
    footer_rule(c, fonts, s);
    dtext(c, fonts, FONT_SANS_M, 24, 2, 64, 1400, text, INK, TEXT_RIGHT, 944,
          s);
}

static void footer_pager(Canvas *c, const Fonts *fonts, int page, int pages,
                         float s)
{
    char label[24];
    uint8_t prev = page > 0 ? INK : GRAY2;
    uint8_t next = page + 1 < pages ? INK : GRAY2;
    footer_rule(c, fonts, s);
    line_d(c, 268, 1367, 252, 1388, 5, s, prev);
    line_d(c, 252, 1388, 268, 1409, 5, s, prev);
    snprintf(label, sizeof(label), "%d / %d", page + 1, pages);
    dtext(c, fonts, FONT_SANS_M, 24, 4, 376, 1400, label, INK, TEXT_CENTER,
          320, s);
    line_d(c, 940, 1367, 956, 1388, 5, s, next);
    line_d(c, 956, 1388, 940, 1409, 5, s, next);
}

void draw_home(Canvas *c, const Fonts *fonts, const Icons *icons,
               const HomeModel *m, float s)
{
    int n = m->favorite_count;
    int pages = 1;
    int page = 0;
    int start = 0;
    int i;
    char meta[32];

    if (n == 0) {
        draw_home_empty(c, fonts, icons, m, s);
        return;
    }
    status_bar(c, fonts, icons, m, s);
    masthead(c, fonts, m, 0, s);
    clock_date(c, fonts, m, s);
    section_head(c, fonts, "Apps", 608, 224, s);
    app_cells(c, fonts, icons, s);
    section_head(c, fonts, "Favorites", 912, 336, s);
    browser_orientation_control(c, fonts, m, s);

    if (n > 6) {
        pages = (n + 5) / 6;
        page = m->page;
        if (page < 0) {
            page = 0;
        }
        if (page >= pages) {
            page = pages - 1;
        }
        start = page * 6;
    }
    for (i = 0; i < 6 && start + i < n; i++) {
        float x = 64.0f + (float)(i % 3) * 328.0f;
        float y = i < 3 ? 928.0f : 1128.0f;
        favorite_tile(c, fonts, icons, &m->favorites[start + i], start + i + 1,
                      x, y, s);
    }
    if (n <= 3) {
        add_row(c, fonts, s);
    }
    if (m->notice[0] != 0) {
        footer_notice(c, fonts, m->notice, s);
    } else if (n > 6) {
        footer_pager(c, fonts, page, pages, s);
    } else {
        snprintf(meta, sizeof(meta), "%d favorite%s", n, n == 1 ? "" : "s");
        footer_meta(c, fonts, meta, s);
    }
}

void draw_home_empty(Canvas *c, const Fonts *fonts, const Icons *icons,
                     const HomeModel *m, float s)
{
    status_bar(c, fonts, icons, m, s);
    masthead(c, fonts, m, 0, s);
    clock_date(c, fonts, m, s);
    section_head(c, fonts, "Apps", 608, 224, s);
    app_cells(c, fonts, icons, s);
    section_head(c, fonts, "Favorites", 912, 336, s);
    browser_orientation_control(c, fonts, m, s);

    outline_d(c, 488, 928, 96, 96, 3, s, INK);
    fill_d(c, 516, 974, 40, 4, s, INK);
    fill_d(c, 534, 956, 4, 40, s, INK);
    dtext(c, fonts, FONT_SERIF_B, 40, 2, 64, 1072, "No favorites yet.", INK,
          TEXT_CENTER, 944, s);
    dtext(c, fonts, FONT_SANS_M, 28, 2, 64, 1112,
          "Swipe down from the top in the browser, then tap Favorite.", GRAY1,
          TEXT_CENTER, 944, s);
    add_row(c, fonts, s);
    if (m->notice[0] != 0) {
        footer_notice(c, fonts, m->notice, s);
    } else {
        footer_meta(c, fonts, "0 favorites", s);
    }
}

static void settings_row_rule(Canvas *c, float y, float s)
{
    rule_d(c, 64, y, 944, s);
}

static void settings_button(Canvas *c, float x, float y, float w, float h,
                            float s)
{
    outline_d(c, x, y, w, h, 3, s, INK);
}

void draw_settings(Canvas *c, const Fonts *fonts, const Icons *icons,
                   const SettingsModel *m, float s)
{
    int i;
    char value[32];
    const HomeModel *home = &m->home;

    status_bar(c, fonts, icons, home, s);
    masthead(c, fonts, home, 1, s);

    /* back row */
    line_d(c, 68, 244, 100, 244, 4, s, INK);
    line_d(c, 80, 232, 68, 244, 4, s, INK);
    line_d(c, 68, 244, 80, 256, 4, s, INK);
    dtext(c, fonts, FONT_SANS_M, 32, 2, 120, 256, "Home", INK, TEXT_LEFT, 0,
          s);
    rule_d(c, 0, 304, SCREEN_W, s);

    /* connection */
    section_head(c, fonts, "Connection", 360, 352, s);
    dtext(c, fonts, FONT_SERIF_B, 36, 2, 64, 440, "Wi-Fi", INK, TEXT_LEFT, 0,
          s);
    {
        char wifi[192];
        char ssid[160];
        const Font *font = fonts_get(fonts, FONT_SANS_M);
        if (home->wifi_on && m->ip[0] != 0) {
            snprintf(wifi, sizeof(wifi), "%s · %s", home->ssid, m->ip);
        } else {
            snprintf(wifi, sizeof(wifi), "%s", home->ssid);
        }
        text_ellipsize(font, 28.0f * s, 1.0f * s, wifi, 640.0f * s, ssid,
                       sizeof(ssid));
        dtext(c, fonts, FONT_SANS_M, 28, 1, 64, 480, ssid,
              home->wifi_on ? INK : GRAY1, TEXT_LEFT, 0, s);
    }
    dtext(c, fonts, FONT_SANS_M, 24, 2, 720, 480,
          home->wifi_on ? "Connected" : "Off", GRAY1, TEXT_RIGHT, 140, s);
    if (home->wifi_on) {
        canvas_round_rect_fill(c, (double)sp(912, s), (double)sp(420, s),
                               (double)(sp(1008, s) - sp(912, s)),
                               (double)(sp(476, s) - sp(420, s)),
                               (double)sp(28, s), INK);
        circle_fill_d(c, 960, 428, 40, 40, s, PAPER);
    } else {
        canvas_round_rect_outline(c, (double)sp(912, s), (double)sp(420, s),
                                  (double)(sp(1008, s) - sp(912, s)),
                                  (double)(sp(476, s) - sp(420, s)),
                                  (double)sp(28, s), (double)slen(3, s), INK);
        circle_fill_d(c, 920, 428, 40, 40, s, INK);
    }
    settings_row_rule(c, 512, s);

    /* display */
    section_head(c, fonts, "Display", 568, 272, s);
    dtext(c, fonts, FONT_SERIF_B, 36, 2, 64, 640, "Front light", INK,
          TEXT_LEFT, 0, s);
    snprintf(value, sizeof(value), "%d / 16", m->brightness);
    dtext(c, fonts, FONT_SANS_M, 28, 1, 536, 640, value, GRAY1, TEXT_RIGHT,
          160, s);
    for (i = 0; i < 16; i++) {
        float x = 64.0f + 40.0f * (float)i;
        if (i < m->brightness) {
            fill_d(c, x, 676, 32, 32, s, INK);
        } else {
            outline_d(c, x, 676, 32, 32, 3, s, INK);
        }
    }
    settings_button(c, 744, 632, 120, 120, s);
    fill_d(c, 784, 690, 40, 4, s, INK);
    settings_button(c, 888, 632, 120, 120, s);
    fill_d(c, 928, 690, 40, 4, s, INK);
    fill_d(c, 946, 672, 4, 40, s, INK);
    settings_row_rule(c, 792, s);

    /* system */
    section_head(c, fonts, "System", 848, 256, s);
    dtext(c, fonts, FONT_SERIF_B, 36, 2, 64, 928, "Newspaper", INK, TEXT_LEFT,
          0, s);
    {
        char updated[96];
        if (m->notice[0] != 0) {
            snprintf(updated, sizeof(updated), "%s", m->notice);
            dtext(c, fonts, FONT_SANS_M, 24, 2, 64, 968, updated, INK,
                  TEXT_LEFT, 0, s);
        } else {
            snprintf(updated, sizeof(updated), "Last updated %s",
                     m->last_update);
            dtext(c, fonts, FONT_SANS_M, 24, 2, 64, 968, updated, GRAY1,
                  TEXT_LEFT, 0, s);
        }
    }
    settings_button(c, 808, 880, 200, 120, s);
    dtext(c, fonts, FONT_SANS_M, 28, 2, 808, 952,
          m->updating ? "Updating…" : "Update now", INK, TEXT_CENTER, 200, s);
    settings_row_rule(c, 1008, s);

    /* Charge limit: a normal 128 px row; the version line lives in the
     * footer. */
    dtext(c, fonts, FONT_SERIF_B, 36, 2, 64, 1064, "Charge limit", INK,
          TEXT_LEFT, 0, s);
    dtext(c, fonts, FONT_SANS_M, 28, 1, 64, 1104, "Stop at 80% for storage",
          GRAY1, TEXT_LEFT, 0, s);
    if (m->charge_limit == 1) {
        canvas_round_rect_fill(c, (double)sp(912, s), (double)sp(1044, s),
                               (double)(sp(1008, s) - sp(912, s)),
                               (double)(sp(1100, s) - sp(1044, s)),
                               (double)sp(28, s), INK);
        circle_fill_d(c, 960, 1052, 40, 40, s, PAPER);
    } else {
        canvas_round_rect_outline(c, (double)sp(912, s), (double)sp(1044, s),
                                  (double)(sp(1008, s) - sp(912, s)),
                                  (double)(sp(1100, s) - sp(1044, s)),
                                  (double)sp(28, s), (double)slen(3, s), INK);
        circle_fill_d(c, 920, 1052, 40, 40, s, INK);
    }
    settings_row_rule(c, 1136, s);

    dtext(c, fonts, FONT_SERIF_B, 36, 2, 64, 1196, "Restart", INK, TEXT_LEFT,
          0, s);
    line_d(c, 987, 1171, 997, 1184, 4, s, INK);
    line_d(c, 997, 1184, 987, 1197, 4, s, INK);
    settings_row_rule(c, 1232, s);

    dtext(c, fonts, FONT_SERIF_B, 36, 2, 64, 1276, "Stock system",
          INK, TEXT_LEFT, 0, s);
    dtext(c, fonts, FONT_SANS_M, 24, 2, 64, 1308, "Quit this launcher",
          GRAY1, TEXT_LEFT, 0, s);
    line_d(c, 987, 1267, 997, 1280, 4, s, INK);
    line_d(c, 997, 1280, 987, 1293, 4, s, INK);
    footer_meta(c, fonts, m->version, s);
}

#define CONFIRM_W 720
#define CONFIRM_X ((SCREEN_W - CONFIRM_W) / 2)
#define CONFIRM_PAD 48
#define CONFIRM_TITLE_SIZE 44
#define CONFIRM_BODY_SIZE 28
#define CONFIRM_BODY_LINE 40
#define CONFIRM_BUTTON_H 96
#define CONFIRM_BUTTON_GAP 24
#define CONFIRM_GAP 32

/* base.css spells the house hairline as 3 px, the section text as SerifB, and
 * secondary copy as SansM in #555. */
typedef struct {
    const char *title;
    const char *body[2];
    int body_count;
    const char *buttons[3];
    const char *actions[3];
    int button_count;
} ConfirmDef;

static const ConfirmDef confirm_defs[] = {
    { "", { NULL, NULL }, 0, { NULL, NULL, NULL }, { NULL, NULL, NULL }, 0 },
    { "Restart",
      { "After a restart, this home screen",
        "opens again on its own." }, 2,
      { "Restart device", "Restart home only", "Cancel" },
      { "confirm_reboot_kindle", "confirm_reboot_home", "confirm_cancel" }, 3 },
    { "Stock system",
      { "This quits the launcher and", "opens the stock system." }, 2,
      { "Return", "Cancel", NULL },
      { "confirm_stock_back", "confirm_cancel", NULL }, 2 },
    { "Remove favorite?",
      { "This site will be removed.", NULL }, 1,
      { "Remove", "Cancel", NULL },
      { "confirm_favorite_remove", "confirm_cancel", NULL }, 2 }
};

typedef struct {
    int x;
    int y;
    int w;
    int h;
    int title_y;
    int rule_y;
    int body_y;
    int button_y;
} ConfirmLayout;

static const ConfirmDef *confirm_def(KhomeConfirm confirm)
{
    if (confirm != KHOME_CONFIRM_REBOOT && confirm != KHOME_CONFIRM_STOCK &&
        confirm != KHOME_CONFIRM_REMOVE_FAVORITE) {
        return NULL;
    }
    return &confirm_defs[confirm];
}

static int confirm_layout(KhomeConfirm confirm, ConfirmLayout *out)
{
    const ConfirmDef *d = confirm_def(confirm);
    int h;

    if (d == NULL) {
        return 0;
    }
    h = CONFIRM_PAD + CONFIRM_TITLE_SIZE + 24 + 3 + CONFIRM_GAP;
    if (d->body_count > 0) {
        h += d->body_count * CONFIRM_BODY_LINE + CONFIRM_GAP;
    }
    h += d->button_count * CONFIRM_BUTTON_H +
         (d->button_count - 1) * CONFIRM_BUTTON_GAP + CONFIRM_PAD;

    out->x = CONFIRM_X;
    out->y = (SCREEN_H - h) / 2;
    out->w = CONFIRM_W;
    out->h = h;
    out->title_y = out->y + CONFIRM_PAD + CONFIRM_TITLE_SIZE;
    out->rule_y = out->title_y + 24;
    out->body_y = out->rule_y + 3 + CONFIRM_GAP + CONFIRM_BODY_SIZE;
    out->button_y = out->rule_y + 3 + CONFIRM_GAP;
    if (d->body_count > 0) {
        out->button_y += d->body_count * CONFIRM_BODY_LINE + CONFIRM_GAP;
    }
    return 1;
}

/* body overrides the definition's copy with one ellipsized line. */
static void draw_confirm_box(Canvas *c, const Fonts *fonts, KhomeConfirm confirm,
                             const char *body, float s)
{
    ConfirmLayout l;
    const ConfirmDef *d = confirm_def(confirm);
    int i;

    if (d == NULL || !confirm_layout(confirm, &l)) {
        return;
    }
    fill_d(c, l.x, l.y, l.w, l.h, s, PAPER);
    outline_d(c, l.x, l.y, l.w, l.h, 3, s, INK);
    dtext(c, fonts, FONT_SERIF_B, CONFIRM_TITLE_SIZE, 2, l.x + CONFIRM_PAD,
          l.title_y, d->title, INK, TEXT_LEFT, l.w - 2 * CONFIRM_PAD, s);
    rule_d(c, l.x + CONFIRM_PAD, l.rule_y, l.w - 2 * CONFIRM_PAD, s);
    if (body != NULL) {
        char line[KHOME_FAVORITE_NAME_MAX + 8];
        text_ellipsize(fonts_get(fonts, FONT_SANS_M), CONFIRM_BODY_SIZE * s,
                       1.0f * s, body,
                       (float)(l.w - 2 * CONFIRM_PAD) * s, line, sizeof(line));
        dtext(c, fonts, FONT_SANS_M, CONFIRM_BODY_SIZE, 1, l.x + CONFIRM_PAD,
              l.body_y, line, GRAY1, TEXT_LEFT, l.w - 2 * CONFIRM_PAD, s);
    } else {
        for (i = 0; i < d->body_count; i++) {
            dtext(c, fonts, FONT_SANS_M, CONFIRM_BODY_SIZE, 1,
                  l.x + CONFIRM_PAD, l.body_y + i * CONFIRM_BODY_LINE,
                  d->body[i], GRAY1, TEXT_LEFT, l.w - 2 * CONFIRM_PAD, s);
        }
    }
    for (i = 0; i < d->button_count; i++) {
        int y = l.button_y + i * (CONFIRM_BUTTON_H + CONFIRM_BUTTON_GAP);
        outline_d(c, l.x + CONFIRM_PAD, y, l.w - 2 * CONFIRM_PAD,
                  CONFIRM_BUTTON_H, 3, s, INK);
        dtext(c, fonts, FONT_SANS_M, 32, 2, l.x + CONFIRM_PAD, y + 60,
              d->buttons[i], INK, TEXT_CENTER, l.w - 2 * CONFIRM_PAD, s);
    }
}

void draw_confirm(Canvas *c, const Fonts *fonts, KhomeConfirm confirm, float s)
{
    draw_confirm_box(c, fonts, confirm, NULL, s);
}

void draw_confirm_favorite(Canvas *c, const Fonts *fonts, const char *name,
                           float s)
{
    draw_confirm_box(c, fonts, KHOME_CONFIRM_REMOVE_FAVORITE, name, s);
}

void draw_launching(Canvas *c, const Fonts *fonts, const Icons *icons,
                    const LaunchingModel *m, float s)
{
    status_bar(c, fonts, icons, &m->home, s);
    masthead(c, fonts, &m->home, 0, s);
    circle_outline_d(c, 456, 440, 160, 160, 4, s, INK);
    icon_d(c, icons, m->app_icon, 488, 472, s);
    dtext(c, fonts, FONT_SANS_M, 24, 8, 64, 680, "App", GRAY1,
          TEXT_CENTER, 944, s);
    dtext(c, fonts, FONT_SERIF_B, 168, 0, 64, 864, m->app_name, INK,
          TEXT_CENTER, 944, s);
    rule_d(c, 416, 912, 240, s);
    dtext(c, fonts, FONT_SANS_M, 32, 6, 64, 976, "Opening soon", INK,
          TEXT_CENTER, 944, s);
    dtext(c, fonts, FONT_SANS_M, 24, 4, 64, 1300, "Please wait a moment",
          GRAY1, TEXT_CENTER, 944, s);
    footer_meta(c, fonts, "Launching", s);
}

void draw_sleep(Canvas *c, const Fonts *fonts, const Icons *icons,
                const SleepModel *m, float s)
{
    char date[80];
    float pen;
    (void)icons;
    rule_d(c, 0, 88, SCREEN_W, s);
    dtext(c, fonts, FONT_SERIF_B, 40, 12, 64, 152, "FOLIO", INK, TEXT_LEFT, 0,
          s);
    {
        char no[48];
        const Font *font = fonts_get(fonts, FONT_SERIF_R);
        float size = 32.0f * s;
        float ls = 2.0f * s;
        float start;
        snprintf(no, sizeof(no), "NO. %03d / %d", m->day_of_year,
                 m->days_in_year);
        start = (float)sp(768, s) + (float)(sp(1008, s) - sp(768, s)) -
                align_width(font, size, ls, no);
        text_draw(c, font, size, ls, start, (float)sp(152, s), no, INK,
                  TEXT_LEFT, 0.0f);
    }
    rule_d(c, 0, 176, SCREEN_W, s);

    circle_fill_d(c, 472, 360, 128, 128, s, INK);
    circle_fill_d(c, 492, 348, 104, 104, s, PAPER);
    dtext(c, fonts, FONT_SANS_M, 24, 10, 64, 568, "SLEEP", GRAY1,
          TEXT_CENTER, 944, s);
    dtext(c, fonts, FONT_SERIF_B, 96, 0, 64, 704, "Books never sleep.", INK,
          TEXT_CENTER, 944, s);
    rule_d(c, 376, 752, 320, s);
    dtext(c, fonts, FONT_SANS_M, 32, 8, 64, 816, "Sleeping", GRAY1,
          TEXT_CENTER, 944, s);
    snprintf(date, sizeof(date), "%s ", m->date);
    {
        const Font *font = fonts_get(fonts, FONT_SERIF_R);
        float size_date = 40.0f * s;
        float size_day = 40.0f * s;
        float total = text_width(font, size_date, 4.0f * s, date) + 4.0f * s +
                      text_width(font, size_day, 7.0f * s, m->weekday) +
                      7.0f * s;
        float start = (float)sp(64, s) +
                      ((float)(sp(1008, s) - sp(64, s)) - total) * 0.5f;
        pen = text_draw(c, font, size_date, 4.0f * s, start,
                        (float)sp(864, s), date, INK, TEXT_LEFT, 0.0f);
        text_draw(c, font, size_day, 7.0f * s, pen, (float)sp(864, s),
                  m->weekday, GRAY1, TEXT_LEFT, 0.0f);
    }
    dtext(c, fonts, FONT_SANS_M, 28, 2, 64, 1300,
          "Press the power button to return home", GRAY1, TEXT_CENTER, 944, s);
    footer_meta(c, fonts, "SLEEP", s);
}

void draw_news_empty(Canvas *c, const Fonts *fonts, const Icons *icons,
                     const HomeModel *m, float s)
{
    status_bar(c, fonts, icons, m, s);
    masthead(c, fonts, m, 0, s);
    dtext(c, fonts, FONT_SANS_M, 24, 8, 64, 600, "Newspaper", GRAY1,
          TEXT_CENTER, 944, s);
    rule_d(c, 416, 648, 240, s);
    dtext(c, fonts, FONT_SERIF_B, 72, 0, 64, 776, "新聞はまだ届いていません",
          INK, TEXT_CENTER, 944, s);
    dtext(c, fonts, FONT_SANS_M, 28, 2, 64, 856,
          "Wi-Fi 接続後に設定から更新できます", GRAY1, TEXT_CENTER, 944, s);
    footer_meta(c, fonts, "NEWS", s);
}

void layout_clock_rect(int *x, int *y, int *w, int *h)
{
    if (x != NULL) {
        *x = 48;
    }
    if (y != NULL) {
        *y = 192;
    }
    if (w != NULL) {
        *w = 976;
    }
    if (h != NULL) {
        *h = 336;
    }
}

/* Tab sheet in design units.  The row height defaults to 108 (about 76 px on
 * the 758-wide panel) and shrinks only as far as the 70% screen budget needs,
 * never below 88 (about 62 px on the panel). */
#define SHEET_MAX_HEIGHT 1013
#define SHEET_STATUS_RULE 48
#define SHEET_BUTTON_Y 48
#define SHEET_BUTTON_H 120
/* Five equal slots across the 944 px content width, 4 gaps of 16. */
#define SHEET_BUTTON_W 176
#define SHEET_BUTTON_GAP 16
#define SHEET_BUTTON_STEP (SHEET_BUTTON_W + SHEET_BUTTON_GAP)
#define SHEET_BUTTON_X(i) (64 + (i) * SHEET_BUTTON_STEP)
#define SHEET_RULE_Y 172
#define SHEET_URL_LABEL_Y 204
#define SHEET_URL_BOX_Y 212
#define SHEET_URL_BOX_H 48
#define SHEET_ROWS_Y 272
#define SHEET_ROW_H_DEFAULT 108
#define SHEET_ROW_H_MIN 88
#define SHEET_GRAB_H 32

static int sheet_tab_count(const TabSheetModel *model)
{
    int n = model != NULL ? model->tabs.count : 0;
    if (n < 0) {
        n = 0;
    }
    if (n > KHOME_MAX_TABS) {
        n = KHOME_MAX_TABS;
    }
    return n;
}

static int sheet_row_height(int n)
{
    int available = SHEET_MAX_HEIGHT - SHEET_ROWS_Y - SHEET_GRAB_H;
    int height = SHEET_ROW_H_DEFAULT;

    if (n > 0 && n * height > available) {
        height = available / n;
    }
    if (height < SHEET_ROW_H_MIN) {
        height = SHEET_ROW_H_MIN;
    }
    return height;
}

int layout_tab_sheet_height(const TabSheetModel *model)
{
    int n = sheet_tab_count(model);
    return SHEET_ROWS_Y + n * sheet_row_height(n) + SHEET_GRAB_H;
}

static void sheet_button(Canvas *c, float x, float s)
{
    outline_d(c, x, SHEET_BUTTON_Y, SHEET_BUTTON_W, SHEET_BUTTON_H, 3, s, INK);
}

static void draw_sheet_status_strip(Canvas *c, const Fonts *fonts,
                                    const Icons *icons, int hour, int minute,
                                    int battery_level, float s)
{
    char clock[16];
    fill_d(c, 0, 0, SCREEN_W, SHEET_STATUS_RULE, s, PAPER);
    snprintf(clock, sizeof(clock), "%02d:%02d", hour, minute);
    dtext(c, fonts, FONT_SANS_M, 24, 3, 64, 32, clock, INK, TEXT_LEFT, 0, s);
    if (battery_level >= 0) {
        char batt[16];
        snprintf(batt, sizeof(batt), "%d%%", battery_level);
        dtext(c, fonts, FONT_SANS_M, 24, 2, 760, 32, batt, GRAY1, TEXT_RIGHT,
              112, s);
    }
    icon_d(c, icons, ICON_BATTERY, 952, 8, s);
    rule_d(c, 0, SHEET_STATUS_RULE, SCREEN_W, s);
}

void draw_tab_sheet_status(Canvas *c, const Fonts *fonts, const Icons *icons,
                           const TabSheetModel *m, float s)
{
    draw_sheet_status_strip(c, fonts, icons, m->hour, m->minute,
                            m->battery_level, s);
}

static void draw_sheet_buttons(Canvas *c, const Fonts *fonts,
                               const TabSheetModel *m, float s)
{
    float back = (float)SHEET_BUTTON_X(0);
    float home = (float)SHEET_BUTTON_X(1);
    float new_tab = (float)SHEET_BUTTON_X(2);
    float favorite = (float)SHEET_BUTTON_X(3);
    float reload = (float)SHEET_BUTTON_X(4);
    float center = SHEET_BUTTON_Y + SHEET_BUTTON_H / 2;
    float baseline = center + 12;
    int has_tab = tabs_active_url(&m->tabs) != NULL;

    sheet_button(c, back, s);
    line_d(c, back + 24, center, back + 52, center, 4, s, INK);
    line_d(c, back + 40, center - 16, back + 24, center, 4, s, INK);
    line_d(c, back + 24, center, back + 40, center + 16, 4, s, INK);
    dtext(c, fonts, FONT_SANS_M, 32, 2, back + 64, baseline, "Back", INK,
          TEXT_LEFT, 0, s);

    sheet_button(c, home, s);
    dtext(c, fonts, FONT_SANS_M, 32, 2, home, baseline, "Home", INK,
          TEXT_CENTER, SHEET_BUTTON_W, s);

    sheet_button(c, new_tab, s);
    fill_d(c, new_tab + 12, center - 2, 24, 4, s, INK);
    fill_d(c, new_tab + 22, center - 12, 4, 24, s, INK);
    dtext(c, fonts, FONT_SANS_M, 28, 2, new_tab + 44, baseline, "New tab",
          INK, TEXT_LEFT, 0, s);

    /* The saved state inverts the button, so the sheet shows the favorite at
     * a glance without a second row of chrome. */
    if (has_tab) {
        if (m->favorite) {
            fill_d(c, favorite, SHEET_BUTTON_Y, SHEET_BUTTON_W, SHEET_BUTTON_H,
                   s, INK);
        }
        sheet_button(c, favorite, s);
        dtext(c, fonts, FONT_SANS_M, 32, 2, favorite, baseline,
              m->favorite ? "Favorited" : "Favorite",
              m->favorite ? PAPER : INK, TEXT_CENTER, SHEET_BUTTON_W, s);

        sheet_button(c, reload, s);
        dtext(c, fonts, FONT_SANS_M, 32, 2, reload, baseline, "Reload", INK,
              TEXT_CENTER, SHEET_BUTTON_W, s);
    }
}

static void draw_sheet_url(Canvas *c, const Fonts *fonts,
                           const TabSheetModel *m, float s)
{
    const char *url = tabs_active_url(&m->tabs);
    char shown[128];

    dtext(c, fonts, FONT_SANS_M, 24, 2, 64, SHEET_URL_LABEL_Y,
          m->notice[0] != 0 ? m->notice : "Current tab",
          m->notice[0] != 0 ? INK : GRAY1, TEXT_LEFT, 0, s);
    outline_d(c, 64, SHEET_URL_BOX_Y, SCREEN_W - 128, SHEET_URL_BOX_H, 3, s,
              INK);
    if (url == NULL) {
        dtext(c, fonts, FONT_SANS_M, 28, 1, 80,
              SHEET_URL_BOX_Y + 32, "No open tabs", GRAY1, TEXT_LEFT, 0,
              s);
        return;
    }
    text_ellipsize(fonts_get(fonts, FONT_SANS_M), 28.0f * s, 1.0f * s, url,
                   880.0f * s, shown, sizeof(shown));
    dtext(c, fonts, FONT_SANS_M, 28, 1, 80, SHEET_URL_BOX_Y + 32, shown, INK,
          TEXT_LEFT, 0, s);
}

static void draw_sheet_row(Canvas *c, const Fonts *fonts, const Tab *tab,
                           int index, int active, int row_h, float s)
{
    float y = (float)(SHEET_ROWS_Y + index * row_h);
    float top = (float)(row_h - 72) / 2.0f;
    float host_base = y + top + 30.0f;
    float path_base = host_base + 36.0f;
    float center = y + (float)row_h / 2.0f;
    char host[KHOME_TAB_HOST_MAX];
    char path[KHOME_TAB_PATH_MAX];
    char shown[256];
    const Font *font = fonts_get(fonts, FONT_SANS_M);

    tabs_url_parts(tab->url, host, sizeof(host), path, sizeof(path));
    rule_d(c, 64, y, 944, s);
    fill_d(c, 64, y + (float)(row_h - 64) / 2.0f, 8, 64, s,
           active ? INK : PAPER);
    text_ellipsize(font, 32.0f * s, 1.0f * s, host, 800.0f * s, shown,
                   sizeof(shown));
    dtext(c, fonts, FONT_SANS_M, 32, 1, 96, host_base, shown,
          active ? INK : GRAY1, TEXT_LEFT, 0, s);
    text_ellipsize(fonts_get(fonts, FONT_SANS_M), 24.0f * s, 1.0f * s, path,
                   800.0f * s, shown, sizeof(shown));
    dtext(c, fonts, FONT_SANS_M, 24, 1, 96, path_base, shown, GRAY1, TEXT_LEFT,
          0, s);

    line_d(c, 966, center - 10, 986, center + 10, 4, s,
           active ? INK : GRAY1);
    line_d(c, 986, center - 10, 966, center + 10, 4, s,
           active ? INK : GRAY1);
}

void draw_tab_sheet(Canvas *c, const Fonts *fonts, const Icons *icons,
                    const TabSheetModel *m, float s)
{
    int n = sheet_tab_count(m);
    int row_h = sheet_row_height(n);
    int height = layout_tab_sheet_height(m);
    int i;

    fill_d(c, 0, 0, SCREEN_W, (float)height, s, PAPER);
    if (m == NULL) {
        return;
    }
    draw_tab_sheet_status(c, fonts, icons, m, s);
    draw_sheet_buttons(c, fonts, m, s);
    rule_d(c, 0, SHEET_RULE_Y, SCREEN_W, s);
    draw_sheet_url(c, fonts, m, s);
    for (i = 0; i < n; i++) {
        draw_sheet_row(c, fonts, &m->tabs.tabs[i], i,
                       i == m->tabs.active, row_h, s);
    }
    if (n > 0) {
        rule_d(c, 64, SHEET_ROWS_Y + n * row_h, 944, s);
    }
    canvas_round_rect_fill(c, (double)sp(488, s),
                           (double)sp((float)(height - 24), s),
                           (double)sp(96, s), (double)sp(8, s),
                           (double)sp(4, s), INK);
    rule_d(c, 0, (float)(height - 3), SCREEN_W, s);
}

/* The NEWS menu borrows the sheet's button row, rule and grab handle so the
 * two pull-downs read as the same surface.  Four 224 px slots fit the three
 * page buttons and the article-only Back to paper. */
#define NEWS_MENU_HEIGHT 280
#define NEWS_MENU_BUTTON_Y 48
#define NEWS_MENU_BUTTON_H 120
#define NEWS_MENU_BUTTON_W 224
#define NEWS_MENU_BUTTON_GAP 16
#define NEWS_MENU_BUTTON_STEP (NEWS_MENU_BUTTON_W + NEWS_MENU_BUTTON_GAP)
#define NEWS_MENU_BUTTON_X(i) (64 + (i) * NEWS_MENU_BUTTON_STEP)
#define NEWS_MENU_RULE_Y 172
#define NEWS_MENU_POS_Y 216
#define NEWS_MENU_GRAB_H 32

int layout_news_menu_height(void)
{
    return NEWS_MENU_HEIGHT;
}

static void news_menu_button(Canvas *c, const Fonts *fonts, float x,
                             const char *label, float s)
{
    outline_d(c, x, NEWS_MENU_BUTTON_Y, NEWS_MENU_BUTTON_W,
              NEWS_MENU_BUTTON_H, 3, s, INK);
    dtext(c, fonts, FONT_SANS_M, 28, 1, x, NEWS_MENU_BUTTON_Y + 72, label,
          INK, TEXT_CENTER, NEWS_MENU_BUTTON_W, s);
}

void draw_news_menu_status(Canvas *c, const Fonts *fonts, const Icons *icons,
                           const NewsMenuModel *m, float s)
{
    draw_sheet_status_strip(c, fonts, icons, m->hour, m->minute,
                            m->battery_level, s);
}

void draw_news_menu(Canvas *c, const Fonts *fonts, const Icons *icons,
                    const NewsMenuModel *m, float s)
{
    char position[48];

    fill_d(c, 0, 0, SCREEN_W, NEWS_MENU_HEIGHT, s, PAPER);
    if (m == NULL) {
        return;
    }
    draw_news_menu_status(c, fonts, icons, m, s);
    news_menu_button(c, fonts, NEWS_MENU_BUTTON_X(0), "Home", s);
    news_menu_button(c, fonts, NEWS_MENU_BUTTON_X(1), "Front page", s);
    news_menu_button(c, fonts, NEWS_MENU_BUTTON_X(2), "Refresh", s);
    if (m->article) {
        news_menu_button(c, fonts, NEWS_MENU_BUTTON_X(3), "Back to paper",
                         s);
    }
    rule_d(c, 0, NEWS_MENU_RULE_Y, SCREEN_W, s);
    if (m->notice[0] != 0) {
        /* The settings screen shows its Update notice in this 24 px ink
         * line; the menu uses the same size and colour for the same text. */
        dtext(c, fonts, FONT_SANS_M, 24, 2, 64, NEWS_MENU_POS_Y, m->notice,
              INK, TEXT_LEFT, 0, s);
    } else {
        if (m->count > 0) {
            snprintf(position, sizeof(position), "%s %d / %d",
                     m->article ? "Article" : "Page", m->position, m->count);
        } else {
            snprintf(position, sizeof(position), "No pages");
        }
        dtext(c, fonts, FONT_SANS_M, 28, 2, 64, NEWS_MENU_POS_Y, position,
              INK, TEXT_LEFT, 0, s);
    }
    canvas_round_rect_fill(c, (double)sp(488, s),
                           (double)sp((float)(NEWS_MENU_HEIGHT - 24), s),
                           (double)sp(96, s), (double)sp(8, s),
                           (double)sp(4, s), INK);
    rule_d(c, 0, (float)(NEWS_MENU_HEIGHT - 3), SCREEN_W, s);
}

#define EDIT_BAR_RULE_Y 120
#define EDIT_FIELD_X 64
#define EDIT_FIELD_Y 160
#define EDIT_FIELD_W (SCREEN_W - 128)
#define EDIT_FIELD_H 248
#define EDIT_FIELD_REFRESH_H 360
#define EDIT_TEXT_X 80
#define EDIT_TEXT_TOP 176
#define EDIT_TEXT_BASELINE 216
#define EDIT_LINE_H 56
#define EDIT_LINES 4
#define EDIT_FONT_SIZE 28.0f
#define EDIT_LETTER_SPACING 1.0f
#define EDIT_TEXT_W ((float)EDIT_FIELD_W - 32.0f)
#define EDIT_CLEAR_X 64
#define EDIT_CLEAR_Y 432
#define EDIT_CLEAR_W 240
#define EDIT_CLEAR_H 88
#define EDIT_KEYS_Y 900
#define EDIT_KEY_H 92
#define EDIT_KEY_GAP 10
#define EDIT_KEY_MARGIN 20
#define EDIT_KEYBOARD_Y 880
#define EDIT_LINE_ITEMS_MAX 64

typedef struct {
    size_t start;
    size_t len;
} EditLine;

typedef struct {
    const char *const *keys;
    int count;
} EditKeyRow;

static const char *const edit_letters_1[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0"
};
static const char *const edit_letters_2[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p"
};
static const char *const edit_letters_3[] = {
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "-"
};
static const char *const edit_letters_4[] = {
    "⇧", "z", "x", "c", "v", "b", "n", "m", "⌫"
};
static const char *const edit_letters_5[] = {
    "Sym", "/", ".", ":", "←", "→", ".com"
};

static const char *const edit_symbols_1[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0"
};
static const char *const edit_symbols_2[] = {
    "_", "?", "=", "&", "#", "%", "~", "@", "+", ","
};
static const char *const edit_symbols_3[] = {
    ";", "!", "*", "'", "(", ")", "$", "[", "]", "-"
};
static const char *const edit_symbols_4[] = {
    "⇧", "{", "}", "\\", "|", "^", "\"", "<", ">", "⌫"
};

static const EditKeyRow edit_letter_rows[5] = {
    { edit_letters_1, 10 },
    { edit_letters_2, 10 },
    { edit_letters_3, 10 },
    { edit_letters_4, 9 },
    { edit_letters_5, 7 }
};

static const EditKeyRow edit_symbol_rows[5] = {
    { edit_symbols_1, 10 },
    { edit_symbols_2, 10 },
    { edit_symbols_3, 10 },
    { edit_symbols_4, 10 },
    { edit_letters_5, 7 }
};

static char edit_display_char(const UrlEdit *edit, size_t i)
{
    unsigned char c = (unsigned char)edit->buf[i];

    if (urledit_char_len(edit, i) == 1 && c >= 0x20 && c <= 0x7E) {
        return (char)c;
    }
    return '?';
}

static float edit_char_width(const Font *font, char c)
{
    char text[2];
    text[0] = c;
    text[1] = 0;
    return text_width(font, EDIT_FONT_SIZE, EDIT_LETTER_SPACING, text);
}

static int edit_wrap(const Font *font, const UrlEdit *edit, EditLine *lines,
                     int max)
{
    int count = 0;
    size_t i = 0;

    if (max <= 0) {
        return 0;
    }
    if (edit == NULL || edit->len == 0) {
        lines[0].start = 0;
        lines[0].len = 0;
        return 1;
    }
    while (i < edit->len && count < max) {
        size_t start = i;
        float width = 0.0f;
        int any = 0;

        while (i < edit->len) {
            float w = edit_char_width(font, edit_display_char(edit, i));
            float add = any ? w + EDIT_LETTER_SPACING : w;
            if (any && width + add > EDIT_TEXT_W) {
                break;
            }
            width += add;
            i += urledit_char_len(edit, i);
            any = 1;
        }
        lines[count].start = start;
        lines[count].len = i - start;
        count++;
    }
    if (count == 0) {
        lines[0].start = 0;
        lines[0].len = 0;
        count = 1;
    }
    return count;
}

static int edit_cursor_line(const EditLine *lines, int count, size_t cursor)
{
    int i;

    for (i = 0; i < count; i++) {
        /* A line boundary belongs to the line that starts there; only the end
         * of the text belongs to the last line. */
        if (cursor < lines[i].start + lines[i].len) {
            return i;
        }
    }
    return count - 1;
}

static int edit_clamped_first(int count, int first)
{
    int max_first = count > EDIT_LINES ? count - EDIT_LINES : 0;

    if (first < 0) {
        return 0;
    }
    if (first > max_first) {
        return max_first;
    }
    return first;
}

static int edit_visible_first(int count, int first, int cursor_line)
{
    first = edit_clamped_first(count, first);
    if (cursor_line < first) {
        first = cursor_line;
    } else if (cursor_line >= first + EDIT_LINES) {
        first = cursor_line - EDIT_LINES + 1;
    }
    return edit_clamped_first(count, first);
}

/* Keeps the scroll position on the cursor's page, moving only as far as the
 * cursor needs.  Drawing calls this after every edit, so a page is always
 * already on the cursor when hit-testing runs. */
static int edit_sync_first(UrlEdit *edit, int count, int cursor_line)
{
    int first = edit_visible_first(count, edit->first_line, cursor_line);

    edit->first_line = first;
    return first;
}

static void edit_draw_line(Canvas *c, const Font *font, const UrlEdit *edit,
                           const EditLine *line, int row, float s)
{
    char text[KHOME_TAB_URL_MAX];
    size_t i = 0;
    size_t n = 0;

    while (i < line->len) {
        text[n++] = edit_display_char(edit, line->start + i);
        i += urledit_char_len(edit, line->start + i);
    }
    text[n] = 0;
    text_draw(c, font, EDIT_FONT_SIZE * s, EDIT_LETTER_SPACING * s,
              (float)sp(EDIT_TEXT_X, s),
              (float)sp(EDIT_TEXT_BASELINE + row * EDIT_LINE_H, s), text, INK,
              TEXT_LEFT, 0.0f);
}

static float edit_cursor_pen(const Font *font, const UrlEdit *edit,
                             const EditLine *line)
{
    size_t stop = edit->cursor;
    float pen = (float)EDIT_TEXT_X;
    size_t i = line->start;

    if (stop > line->start + line->len) {
        stop = line->start + line->len;
    }
    while (i < stop) {
        size_t n = urledit_char_len(edit, i);

        if (i + n > stop) {
            break;
        }
        pen += edit_char_width(font, edit_display_char(edit, i)) +
               EDIT_LETTER_SPACING;
        i += n;
    }
    return pen;
}

static const EditKeyRow *edit_rows(const UrlEdit *edit)
{
    return edit != NULL && edit->symbols ? edit_symbol_rows : edit_letter_rows;
}

static void edit_key_rect(int row, int index, int count, int *x, int *y,
                          int *w, int *h)
{
    int usable = SCREEN_W - 2 * EDIT_KEY_MARGIN -
                 (count - 1) * EDIT_KEY_GAP;
    int key_w = usable / count;
    int row_w = count * key_w + (count - 1) * EDIT_KEY_GAP;
    int start = (SCREEN_W - row_w) / 2;

    if (x != NULL) {
        *x = start + index * (key_w + EDIT_KEY_GAP);
    }
    if (y != NULL) {
        *y = EDIT_KEYS_Y + row * (EDIT_KEY_H + EDIT_KEY_GAP);
    }
    if (w != NULL) {
        *w = key_w;
    }
    if (h != NULL) {
        *h = EDIT_KEY_H;
    }
}

/* Noto Sans JP has no U+232B, so the delete key gets the usual left arrow
 * with an x drawn from the same strokes as the other symbols. */
static void edit_backspace_icon(Canvas *c, int x, int y, int w, int h,
                                float s, uint8_t color)
{
    float cx = (float)x + (float)w / 2.0f;
    float cy = (float)y + (float)h / 2.0f;

    line_d(c, cx - 18, cy, cx - 6, cy - 12, 4, s, color);
    line_d(c, cx - 6, cy - 12, cx + 16, cy - 12, 4, s, color);
    line_d(c, cx + 16, cy - 12, cx + 16, cy + 12, 4, s, color);
    line_d(c, cx + 16, cy + 12, cx - 6, cy + 12, 4, s, color);
    line_d(c, cx - 6, cy + 12, cx - 18, cy, 4, s, color);
    line_d(c, cx - 2, cy - 6, cx + 8, cy + 6, 3, s, color);
    line_d(c, cx + 8, cy - 6, cx - 2, cy + 6, 3, s, color);
}

static void edit_draw_key(Canvas *c, const Fonts *fonts, const UrlEdit *edit,
                          const char *label, int x, int y, int w, int h,
                          float s)
{
    int active = 0;
    const char *shown = label;
    char upper[2];

    if (strcmp(label, "⇧") == 0) {
        active = edit->shift;
    } else if (strcmp(label, "Sym") == 0) {
        active = edit->symbols;
        if (edit->symbols) {
            shown = "abc";
        }
    }
    if (!edit->symbols && edit->shift && label[0] >= 'a' &&
        label[0] <= 'z' && label[1] == 0) {
        upper[0] = (char)(label[0] - 'a' + 'A');
        upper[1] = 0;
        shown = upper;
    }
    if (active) {
        fill_d(c, (float)x, (float)y, (float)w, (float)h, s, INK);
    } else {
        outline_d(c, (float)x, (float)y, (float)w, (float)h, 3, s, INK);
    }
    if (strcmp(label, "⌫") == 0) {
        edit_backspace_icon(c, x, y, w, h, s, active ? PAPER : INK);
        return;
    }
    dtext(c, fonts, FONT_SANS_M, 30, 1, (float)x,
          (float)(y + h / 2 + 11), shown, active ? PAPER : INK, TEXT_CENTER,
          (float)w, s);
}

void draw_url_edit(Canvas *c, const Fonts *fonts, UrlEdit *edit,
                   float s)
{
    fill_d(c, 0, 0, SCREEN_W, EDIT_BAR_RULE_Y, s, PAPER);
    dtext(c, fonts, FONT_SANS_M, 30, 1, 64, 76, "Cancel", INK, TEXT_LEFT,
          0, s);
    dtext(c, fonts, FONT_SANS_M, 32, 3, 64, 76, "Edit URL", INK, TEXT_CENTER,
          944, s);
    dtext(c, fonts, FONT_SANS_M, 32, 1, 64, 76, "Open", INK, TEXT_RIGHT, 944,
          s);
    rule_d(c, 0, EDIT_BAR_RULE_Y, SCREEN_W, s);
    draw_url_edit_field(c, fonts, edit, s);
    draw_url_edit_keyboard(c, fonts, edit, s);
}

void draw_url_edit_field(Canvas *c, const Fonts *fonts, UrlEdit *edit,
                         float s)
{
    const Font *font = fonts_get(fonts, FONT_SANS_M);
    EditLine lines[EDIT_LINE_ITEMS_MAX];
    int total;
    int cursor_line;
    int first;
    int row;
    uint8_t clear_ink = edit != NULL && edit->len > 0 ? INK : GRAY2;

    fill_d(c, EDIT_FIELD_X, EDIT_FIELD_Y, EDIT_FIELD_W, EDIT_FIELD_REFRESH_H,
           s, PAPER);
    outline_d(c, EDIT_FIELD_X, EDIT_FIELD_Y, EDIT_FIELD_W, EDIT_FIELD_H, 3, s,
              INK);
    if (font != NULL && edit != NULL) {
        total = edit_wrap(font, edit, lines, EDIT_LINE_ITEMS_MAX);
        cursor_line = edit_cursor_line(lines, total, edit->cursor);
        first = edit_sync_first(edit, total, cursor_line);
        for (row = 0; row < EDIT_LINES && first + row < total; row++) {
            edit_draw_line(c, font, edit, &lines[first + row], row, s);
        }
        if (cursor_line >= first && cursor_line < first + EDIT_LINES) {
            float pen = edit_cursor_pen(font, edit, &lines[cursor_line]);
            int baseline = EDIT_TEXT_BASELINE +
                           (cursor_line - first) * EDIT_LINE_H;
            fill_d(c, pen, (float)(baseline - 32), 3, 38, s, INK);
        }
    }
    outline_d(c, EDIT_CLEAR_X, EDIT_CLEAR_Y, EDIT_CLEAR_W, EDIT_CLEAR_H, 3, s,
              clear_ink);
    dtext(c, fonts, FONT_SANS_M, 30, 1, EDIT_CLEAR_X,
          EDIT_CLEAR_Y + EDIT_CLEAR_H / 2 + 11, "Clear all", clear_ink,
          TEXT_CENTER, EDIT_CLEAR_W, s);
}

void draw_url_edit_keyboard(Canvas *c, const Fonts *fonts,
                            const UrlEdit *edit, float s)
{
    const EditKeyRow *rows = edit_rows(edit);
    int row;
    int i;

    fill_d(c, 0, EDIT_KEYBOARD_Y, SCREEN_W, SCREEN_H - EDIT_KEYBOARD_Y, s,
           PAPER);
    for (row = 0; row < 5; row++) {
        for (i = 0; i < rows[row].count; i++) {
            int x;
            int y;
            int w;
            int h;
            edit_key_rect(row, i, rows[row].count, &x, &y, &w, &h);
            edit_draw_key(c, fonts, edit, rows[row].keys[i], x, y, w, h, s);
        }
    }
}

void layout_url_edit_field_rect(int *x, int *y, int *w, int *h)
{
    if (x != NULL) {
        *x = EDIT_FIELD_X;
    }
    if (y != NULL) {
        *y = EDIT_FIELD_Y;
    }
    if (w != NULL) {
        *w = EDIT_FIELD_W;
    }
    if (h != NULL) {
        *h = EDIT_FIELD_REFRESH_H;
    }
}

void layout_url_edit_keyboard_rect(int *x, int *y, int *w, int *h)
{
    if (x != NULL) {
        *x = 0;
    }
    if (y != NULL) {
        *y = EDIT_KEYBOARD_Y;
    }
    if (w != NULL) {
        *w = SCREEN_W;
    }
    if (h != NULL) {
        *h = SCREEN_H - EDIT_KEYBOARD_Y;
    }
}

size_t layout_url_edit_cursor_at(const Fonts *fonts, const UrlEdit *edit,
                                 int x, int y)
{
    const Font *font = fonts_get(fonts, FONT_SANS_M);
    EditLine lines[EDIT_LINE_ITEMS_MAX];
    int total;
    int first;
    int row;
    int index;
    float pen;
    size_t i;

    if (font == NULL || edit == NULL) {
        return 0;
    }
    total = edit_wrap(font, edit, lines, EDIT_LINE_ITEMS_MAX);
    first = edit_clamped_first(total, edit->first_line);
    row = (y - EDIT_TEXT_TOP) / EDIT_LINE_H;
    if (row < 0) {
        row = 0;
    }
    if (row >= EDIT_LINES) {
        row = EDIT_LINES - 1;
    }
    index = first + row;
    if (index >= total) {
        index = total - 1;
    }
    pen = (float)EDIT_TEXT_X;
    if (x <= (int)pen) {
        return lines[index].start;
    }
    i = lines[index].start;
    while (i < lines[index].start + lines[index].len) {
        size_t n = urledit_char_len(edit, i);
        float w = edit_char_width(font, edit_display_char(edit, i));

        if ((float)x < pen + w * 0.5f) {
            return i;
        }
        pen += w + EDIT_LETTER_SPACING;
        i += n;
    }
    return lines[index].start + lines[index].len;
}

static int add_tap(TapRect *out, int count, int max, const char *action, int x,
                   int y, int w, int h)
{
    if (count >= max) {
        return count;
    }
    snprintf(out[count].action, sizeof(out[count].action), "%s", action);
    out[count].x = x;
    out[count].y = y;
    out[count].w = w;
    out[count].h = h;
    out[count].index = 0;
    return count + 1;
}

static int add_indexed_tap(TapRect *out, int count, int max,
                           const char *action, int x, int y, int w, int h,
                           int index)
{
    int before = count;
    count = add_tap(out, count, max, action, x, y, w, h);
    if (count > before) {
        out[before].index = index;
    }
    return count;
}

int layout_home_taps(const HomeModel *m, TapRect *out, int max)
{
    int count = 0;
    int i;
    int n;
    int start = 0;
    int pages;
    int page;
    if (out == NULL || max <= 0) {
        return 0;
    }
    count = add_tap(out, count, max, "app_newspaper", 120, 624, 208, 224);
    count = add_tap(out, count, max, "app_manga", 328, 624, 208, 224);
    count = add_tap(out, count, max, "app_browser", 536, 624, 208, 224);
    count = add_tap(out, count, max, "app_settings", 744, 624, 208, 224);
    count = add_tap(out, count, max, "browser_orientation", 536, 848, 208,
                    40);
    n = m->favorite_count;
    pages = n > 6 ? (n + 5) / 6 : 1;
    page = m->page;
    if (page < 0) {
        page = 0;
    }
    if (page >= pages) {
        page = pages - 1;
    }
    if (n > 6) {
        start = page * 6;
    }
    for (i = 0; i < 6 && start + i < n; i++) {
        char action[32];
        int x = 64 + (i % 3) * 328;
        int y = i < 3 ? 928 : 1128;
        snprintf(action, sizeof(action), "fav_open_%02d", start + i + 1);
        count = add_indexed_tap(out, count, max, action, x, y, 288, 184,
                                start + i);
    }
    if (n == 0) {
        /* The empty state is one block: plus, copy, and the add row. */
        count = add_tap(out, count, max, "open_browser_add", 64, 888, 944,
                        424);
    } else if (n <= 3) {
        count = add_tap(out, count, max, "open_browser_add", 64, 1128, 944,
                        184);
    }
    if (n > 6) {
        count = add_tap(out, count, max, "page_prev", 200, 1328, 120, 120);
        count = add_tap(out, count, max, "page_next", 888, 1328, 120, 120);
    }
    return count;
}

#define NEWS_PREV_W ((SCREEN_W * 30 + 50) / 100)

int layout_news_taps(TapRect *out, int max)
{
    int count = 0;

    if (out == NULL || max <= 0) {
        return 0;
    }
    /* Links are checked before these rects, so a rectangle that reaches into
     * the left 30% still opens its article. */
    count = add_tap(out, count, max, "news_prev", 0, 0, NEWS_PREV_W, SCREEN_H);
    count = add_tap(out, count, max, "news_next", NEWS_PREV_W, 0,
                    SCREEN_W - NEWS_PREV_W, SCREEN_H);
    return count;
}

int layout_news_menu_taps(const NewsMenuModel *m, TapRect *out, int max)
{
    int count = 0;

    if (out == NULL || max <= 0) {
        return 0;
    }
    count = add_tap(out, count, max, "news_menu_home", NEWS_MENU_BUTTON_X(0),
                    NEWS_MENU_BUTTON_Y, NEWS_MENU_BUTTON_W, NEWS_MENU_BUTTON_H);
    count = add_tap(out, count, max, "news_menu_front", NEWS_MENU_BUTTON_X(1),
                    NEWS_MENU_BUTTON_Y, NEWS_MENU_BUTTON_W, NEWS_MENU_BUTTON_H);
    count = add_tap(out, count, max, "news_menu_refresh",
                    NEWS_MENU_BUTTON_X(2), NEWS_MENU_BUTTON_Y,
                    NEWS_MENU_BUTTON_W, NEWS_MENU_BUTTON_H);
    if (m != NULL && m->article) {
        count = add_tap(out, count, max, "news_menu_back",
                        NEWS_MENU_BUTTON_X(3), NEWS_MENU_BUTTON_Y,
                        NEWS_MENU_BUTTON_W, NEWS_MENU_BUTTON_H);
    }
    /* The panel absorbs taps that miss a control; only taps outside it (or on
     * the grab strip) dismiss it.  Body must stay after every button. */
    count = add_tap(out, count, max, "news_menu_body", 0, 0, SCREEN_W,
                    NEWS_MENU_HEIGHT - NEWS_MENU_GRAB_H);
    count = add_tap(out, count, max, "news_menu_dismiss", 0,
                    NEWS_MENU_HEIGHT - NEWS_MENU_GRAB_H, SCREEN_W,
                    NEWS_MENU_GRAB_H);
    return count;
}

int layout_settings_taps(TapRect *out, int max)
{
    int count = 0;
    if (out == NULL || max <= 0) {
        return 0;
    }
    count = add_tap(out, count, max, "back_home", 48, 184, 224, 120);
    count = add_tap(out, count, max, "wifi_toggle", 888, 384, 120, 120);
    count = add_tap(out, count, max, "charge_limit_toggle", 888, 1008, 120,
                    120);
    count = add_tap(out, count, max, "brightness_down", 744, 632, 120, 120);
    count = add_tap(out, count, max, "brightness_up", 888, 632, 120, 120);
    count = add_tap(out, count, max, "news_update", 808, 880, 200, 120);
    count = add_tap(out, count, max, "reboot", 64, 1136, 944, 96);
    count = add_tap(out, count, max, "stock_ui", 64, 1232, 944, 96);
    return count;
}

int layout_confirm_taps(KhomeConfirm confirm, TapRect *out, int max)
{
    ConfirmLayout l;
    const ConfirmDef *d = confirm_def(confirm);
    int count = 0;
    int i;

    if (out == NULL || max <= 0 || d == NULL ||
        !confirm_layout(confirm, &l)) {
        return 0;
    }
    for (i = 0; i < d->button_count; i++) {
        count = add_tap(out, count, max, d->actions[i], l.x + CONFIRM_PAD,
                        l.button_y +
                            i * (CONFIRM_BUTTON_H + CONFIRM_BUTTON_GAP),
                        l.w - 2 * CONFIRM_PAD, CONFIRM_BUTTON_H);
    }
    return count;
}

int layout_tab_sheet_taps(const TabSheetModel *model, TapRect *out, int max)
{
    int count = 0;
    int n = sheet_tab_count(model);
    int row_h = sheet_row_height(n);
    int i;

    if (out == NULL || max <= 0) {
        return 0;
    }
    count = add_tap(out, count, max, "sheet_back", SHEET_BUTTON_X(0),
                    SHEET_BUTTON_Y, SHEET_BUTTON_W, SHEET_BUTTON_H);
    count = add_tap(out, count, max, "sheet_home", SHEET_BUTTON_X(1),
                    SHEET_BUTTON_Y, SHEET_BUTTON_W, SHEET_BUTTON_H);
    count = add_tap(out, count, max, "sheet_new", SHEET_BUTTON_X(2),
                    SHEET_BUTTON_Y, SHEET_BUTTON_W, SHEET_BUTTON_H);
    if (model != NULL && tabs_active_url(&model->tabs) != NULL) {
        count = add_tap(out, count, max, "sheet_favorite", SHEET_BUTTON_X(3),
                        SHEET_BUTTON_Y, SHEET_BUTTON_W, SHEET_BUTTON_H);
        count = add_tap(out, count, max, "sheet_reload", SHEET_BUTTON_X(4),
                        SHEET_BUTTON_Y, SHEET_BUTTON_W, SHEET_BUTTON_H);
    }
    for (i = 0; i < n; i++) {
        int y = SHEET_ROWS_Y + i * row_h;
        count = add_indexed_tap(out, count, max, "sheet_tab", 64, y,
                                912 - 64, row_h, i);
        count = add_indexed_tap(out, count, max, "sheet_close", 912, y, 96,
                                row_h, i);
    }
    /* SHEET_URL_LABEL_Y is the heading's baseline, so the target starts at
     * the rule above it to cover the glyphs. */
    count = add_tap(out, count, max, "sheet_edit_url", 64, SHEET_RULE_Y,
                    SCREEN_W - 128,
                    SHEET_URL_BOX_Y + SHEET_URL_BOX_H - SHEET_RULE_Y);
    /* The sheet body absorbs taps that miss a control, so only taps outside
     * the sheet reach sheet_dismiss.  It must stay after every control
     * because the caller uses the first rect that matches. */
    count = add_tap(out, count, max, "sheet_body", 0, 0, SCREEN_W,
                    layout_tab_sheet_height(model) - SHEET_GRAB_H);
    count = add_tap(out, count, max, "sheet_dismiss", 0,
                    layout_tab_sheet_height(model) - SHEET_GRAB_H, SCREEN_W,
                    SHEET_GRAB_H);
    return count;
}

static void url_edit_key_action(char *out, size_t cap, const char *label)
{
    const char *token = label;

    if (strcmp(label, "⇧") == 0) {
        token = "shift";
    } else if (strcmp(label, "⌫") == 0) {
        token = "bs";
    } else if (strcmp(label, "←") == 0) {
        token = "left";
    } else if (strcmp(label, "→") == 0) {
        token = "right";
    } else if (strcmp(label, "Sym") == 0) {
        token = "sym";
    }
    snprintf(out, cap, "key:%s", token);
}

int layout_url_edit_taps(const UrlEdit *edit, TapRect *out, int max)
{
    const EditKeyRow *rows = edit_rows(edit);
    int count = 0;
    int row;
    int i;

    if (out == NULL || max <= 0) {
        return 0;
    }
    count = add_tap(out, count, max, "urledit_cancel", 32, 16, 248, 96);
    count = add_tap(out, count, max, "urledit_open", 792, 16, 248, 96);
    count = add_tap(out, count, max, "urledit_clear", EDIT_CLEAR_X,
                    EDIT_CLEAR_Y, EDIT_CLEAR_W, EDIT_CLEAR_H);
    count = add_tap(out, count, max, "urledit_field", EDIT_FIELD_X,
                    EDIT_FIELD_Y, EDIT_FIELD_W, EDIT_FIELD_H);
    for (row = 0; row < 5; row++) {
        for (i = 0; i < rows[row].count; i++) {
            char action[32];
            int x;
            int y;
            int w;
            int h;
            edit_key_rect(row, i, rows[row].count, &x, &y, &w, &h);
            url_edit_key_action(action, sizeof(action), rows[row].keys[i]);
            count = add_tap(out, count, max, action, x, y, w, h);
        }
    }
    return count;
}
