#include "fixtures.h"

#include <stdio.h>
#include <string.h>

#include "canvas.h"

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (cap == 0) {
        return;
    }
    snprintf(dst, cap, "%s", src);
}

static void home_common(HomeModel *m)
{
    memset(m, 0, sizeof(*m));
    m->hour = 10;
    m->minute = 42;
    m->day_of_year = 296;
    m->days_in_year = 365;
    copy_str(m->date, sizeof(m->date), "Oct 23");
    copy_str(m->weekday, sizeof(m->weekday), "Fri");
    copy_str(m->ssid, sizeof(m->ssid), "HOME-5G");
    m->wifi_on = 1;
    m->battery_level = 91;
}

static void add_favorite(HomeModel *m, const char *name, const char *initial)
{
    Favorite *fav;
    if (m->favorite_count >= KHOME_MAX_FAVORITES) {
        return;
    }
    fav = &m->favorites[m->favorite_count++];
    copy_str(fav->name, sizeof(fav->name), name);
    copy_str(fav->initial, sizeof(fav->initial), initial);
}

void fixture_home(HomeModel *m)
{
    home_common(m);
    add_favorite(m, "Zenn", "Z");
    add_favorite(m, "Hacker News", "HN");
    add_favorite(m, "Wikipedia", "W");
}

void fixture_home_many(HomeModel *m)
{
    home_common(m);
    add_favorite(m, "Zenn", "Z");
    add_favorite(m, "Hacker News", "HN");
    add_favorite(m, "Wikipedia", "W");
    add_favorite(m, "GitHub", "G");
    add_favorite(m, "Hatena Bookmark", "H");
    add_favorite(m, "NHK NEWS", "N");
    add_favorite(m, "Kagi", "K");
    add_favorite(m, "Lobsters", "L");
    add_favorite(m, "YouTube", "Y");
}

void fixture_home_many_page(HomeModel *m, int page)
{
    fixture_home_many(m);
    m->page = page;
}

void fixture_home_empty(HomeModel *m)
{
    home_common(m);
}

void fixture_settings(SettingsModel *m, int wifi_on)
{
    memset(m, 0, sizeof(*m));
    home_common(&m->home);
    m->home.wifi_on = wifi_on;
    m->brightness = 12;
    copy_str(m->last_update, sizeof(m->last_update), "Oct 23 06:00");
    copy_str(m->version, sizeof(m->version), "Folio 1.0 (PW2)");
}

void fixture_settings_updating(SettingsModel *m)
{
    fixture_settings(m, 1);
    m->updating = 1;
}

void fixture_launching(LaunchingModel *m)
{
    memset(m, 0, sizeof(*m));
    home_common(&m->home);
    copy_str(m->app_name, sizeof(m->app_name), "Manga");
    m->app_icon = ICON_MANGA;
}

void fixture_sleep(SleepModel *m)
{
    memset(m, 0, sizeof(*m));
    m->day_of_year = 296;
    m->days_in_year = 365;
    copy_str(m->date, sizeof(m->date), "Oct 23");
    copy_str(m->weekday, sizeof(m->weekday), "Fri");
}

void fixture_news_empty(HomeModel *m)
{
    home_common(m);
    m->wifi_on = 0;
    m->ssid[0] = 0;
}

static void sheet_common(TabSheetModel *m)
{
    memset(m, 0, sizeof(*m));
    m->hour = 10;
    m->minute = 42;
    m->battery_level = 91;
}

void fixture_tab_sheet_3(TabSheetModel *m)
{
    sheet_common(m);
    tabs_open(&m->tabs, "https://example.com/");
    tabs_open(&m->tabs,
              "https://www3.nhk.or.jp/news/html/20261023/"
              "k10014000000000.html?utm_source=khome&utm_medium=tab");
    tabs_open(&m->tabs,
              "https://ja.example.jp/articles/2026/10/23/"
              "a-very-long-path-that-keeps-going");
    tabs_select(&m->tabs, 1);
}

#define FIXTURE_EDIT_URL                                                     \
    "https://www.gutenberg.org/ebooks/search/?query=jane+austen&"          \
    "submit_search=Go%21&lang=en&format=html&page=2&sort=popular&"          \
    "start_index=26&session=8f3c2a1b9d0e4f5a6b7c8d9e&"                     \
    "utm_source=khome&utm_medium=launcher&utm_campaign=url-edit-screen&"    \
    "ref=tab-sheet"

void fixture_url_edit(UrlEdit *edit)
{
    const char *mark;

    urledit_init(edit, FIXTURE_EDIT_URL);
    mark = strstr(edit->buf, "austen");
    if (mark != NULL) {
        urledit_set_cursor(edit, (size_t)(mark - edit->buf) + 6);
    }
}

void fixture_url_edit_symbols(UrlEdit *edit)
{
    urledit_init(edit, FIXTURE_EDIT_URL);
    urledit_set_cursor(edit, edit->len > 18 ? edit->len - 18 : edit->len);
    edit->symbols = 1;
}

void fixture_tab_sheet_8(TabSheetModel *m)
{
    sheet_common(m);
    m->minute = 43;
    m->battery_level = 68;
    m->favorite = 1;
    tabs_open(&m->tabs, "https://example.com/");
    tabs_open(&m->tabs, "https://news.ycombinator.com/item?id=45678901");
    tabs_open(&m->tabs, "https://ja.wikipedia.org/wiki/Electronic_paper");
    tabs_open(&m->tabs,
              "https://example.com/kindle_jailbreak/tree/main/launcher/src");
    tabs_open(&m->tabs,
              "https://ja.example.jp/articles/2026/10/23/"
              "a-very-long-path-that-keeps-going");
    tabs_open(&m->tabs, "https://example.com/articles/8f3c2a1b9d0e");
    tabs_open(&m->tabs,
              "https://docs.aws.amazon.com/AmazonS3/latest/userguide/"
              "Welcome.html");
    tabs_open(&m->tabs, "https://www.amazon.co.jp/dp/B00OQVZDJM");
    tabs_select(&m->tabs, 4);
}
