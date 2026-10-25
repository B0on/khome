#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "canvas.h"
#include "fixtures.h"
#include "kindletimes.h"
#include "layout.h"
#include "png_out.h"
#include "text.h"

static const char *const screen_names[] = {
    "home",       "home-many", "home-many-2", "home-empty",
    "settings",   "settings-wifi-off", "settings-confirm-reboot",
    "settings-confirm-stock", "settings-updating", "launching", "sleep",
    "news",       "news-empty", "news-front", "news-list", "news-article",
    "news-article-mid", "news-article-last", "news-menu",
    "news-menu-article", "url-edit", "url-edit-symbols", "home-confirm-remove"
};
#define SCREEN_COUNT ((int)(sizeof(screen_names) / sizeof(screen_names[0])))

static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static const char *pick_dir(const char *env_name, const char *const *candidates,
                            int count, char *buf, size_t cap)
{
    const char *env = getenv(env_name);
    int i;
    if (env != NULL && env[0] != 0) {
        snprintf(buf, cap, "%s", env);
        return buf;
    }
    for (i = 0; i < count; i++) {
        if (is_dir(candidates[i])) {
            return candidates[i];
        }
    }
    return candidates[0];
}

static int ensure_dir(const char *path)
{
    if (mkdir(path, 0777) == 0) {
        return 1;
    }
    if (errno == EEXIST && is_dir(path)) {
        return 1;
    }
    fprintf(stderr, "khome-render: cannot create directory %s: %s\n", path,
            strerror(errno));
    return 0;
}

static float parse_scale(const char *name)
{
    if (strcmp(name, "design") == 0) {
        return 1.0f;
    }
    if (strcmp(name, "device") == 0) {
        return 758.0f / 1072.0f;
    }
    return -1.0f;
}

static const char *news_dir(void)
{
    const char *env = getenv("KHOME_NEWS_TEXT_DIR");

    return env != NULL && env[0] != 0 ? env : "/mnt/us/newspaper/text/current";
}

static float news_scale(float scale)
{
    return scale * 1072.0f / 758.0f;
}

static int news_text_size(void)
{
    const char *env = getenv("KHOME_NEWS_TEXT_SIZE");
    int value = env != NULL ? atoi(env) : 30;

    return value == 26 || value == 30 || value == 36 ? value : 30;
}

static int load_paper(KtPaper *paper, const Fonts *fonts);

static int check_indents(const Fonts *fonts)
{
    static KtPaper paper;
    static KtArticle article;
    static const int sizes[3] = { 26, 30, 36 };
    int bad = 0;
    int paragraphs = 0;
    int i;
    int k;

    if (!load_paper(&paper, fonts)) {
        fprintf(stderr, "check-indent: no text edition in %s\n", news_dir());
        return 1;
    }
    for (k = 0; k < 3; k++) {
        for (i = 0; i < paper.story_count; i++) {
            char *p;
            char *nl;

            if (!kt_article_open(&article, &paper, fonts, i, sizes[k])) {
                fprintf(stderr, "check-indent: cannot open %s\n",
                        paper.stories[i].id);
                bad++;
                continue;
            }
            p = article.body;
            while (p != NULL && *p != 0) {
                TextLine lines[4];
                int truncated = 0;
                int n;
                size_t len;

                nl = strchr(p, '\n');
                len = nl != NULL ? (size_t)(nl - p) : strlen(p);
                if (len > 0) {
                    if (nl != NULL) {
                        *nl = 0;
                    }
                    n = text_wrap(fonts_get(fonts, FONT_SERIF_R),
                                  (float)sizes[k], 0.0f, p, 694.0f, lines, 4,
                                  &truncated);
                    if (nl != NULL) {
                        *nl = '\n';
                    }
                    paragraphs++;
                    if (n > 0 && lines[0].indent != (float)sizes[k]) {
                        fprintf(stderr,
                                "check-indent: %s at %d px paragraph %d has "
                                "indent %.1f, expected %d: %.30s\n",
                                paper.stories[i].id, sizes[k], paragraphs,
                                lines[0].indent, sizes[k], p);
                        bad++;
                    }
                }
                p = nl != NULL ? nl + 1 : NULL;
            }
            kt_article_close(&article);
        }
    }
    printf("check-indent: %d articles x 3 sizes, %d paragraphs, %d missing\n",
           paper.story_count, paragraphs, bad);
    return bad == 0 ? 0 : 1;
}

static int news_story(const KtPaper *paper)
{
    const char *env = getenv("KHOME_NEWS_STORY");
    int i;

    if (env == NULL || env[0] == 0) {
        return 0;
    }
    for (i = 0; i < paper->story_count; i++) {
        if (strcmp(env, paper->stories[i].id) == 0) {
            return i;
        }
    }
    i = atoi(env);
    return i >= 0 && i < paper->story_count ? i : 0;
}

static int load_paper(KtPaper *paper, const Fonts *fonts)
{
    if (!kt_paper_load(paper, news_dir())) {
        return 0;
    }
    kt_paper_build(paper, fonts);
    return paper->story_count > 0;
}

static void draw_empty_news(Canvas *canvas, const Fonts *fonts,
                            const Icons *icons, float scale)
{
    HomeModel m;
    fixture_news_empty(&m);
    draw_news_empty(canvas, fonts, icons, &m, scale);
}

static int render_news_screen(Canvas *canvas, const char *screen,
                              const Fonts *fonts, const Icons *icons,
                              float scale)
{
    static KtPaper paper;
    static KtArticle article;
    int page = 0;
    int page_count;

    if (!load_paper(&paper, fonts)) {
        if (strcmp(screen, "news-menu") == 0 ||
            strcmp(screen, "news-menu-article") == 0) {
            NewsMenuModel m;

            draw_empty_news(canvas, fonts, icons, scale);
            memset(&m, 0, sizeof(m));
            m.hour = 9;
            m.minute = 41;
            m.battery_level = 87;
            m.article = strcmp(screen, "news-menu-article") == 0;
            m.position = m.article ? 2 : 0;
            m.count = m.article ? 4 : 0;
            draw_news_menu(canvas, fonts, icons, &m, scale);
            return 1;
        }
        draw_empty_news(canvas, fonts, icons, scale);
        return 1;
    }
    page_count = paper.layout.page_count;
    if (strcmp(screen, "news-list") == 0) {
        page = page_count > 1 ? 1 : 0;
    } else if (strcmp(screen, "news-article") == 0 ||
               strcmp(screen, "news-article-mid") == 0 ||
               strcmp(screen, "news-article-last") == 0) {
        int story = news_story(&paper);

        if (!kt_article_open(&article, &paper, fonts, story, news_text_size())) {
            draw_empty_news(canvas, fonts, icons, scale);
            return 1;
        }
        if (strcmp(screen, "news-article-mid") == 0) {
            page = article.page_count > 1 ? 1 : 0;
        } else if (strcmp(screen, "news-article-last") == 0) {
            page = article.page_count - 1;
        }
        kt_draw_article(canvas, fonts, &paper, &article, page,
                        story + 1 < paper.story_count ? story + 1 : -1,
                        news_scale(scale));
        kt_article_close(&article);
        return 1;
    }
    kt_draw_paper(canvas, fonts, &paper, page, news_scale(scale));
    if (strcmp(screen, "news-menu") == 0 ||
        strcmp(screen, "news-menu-article") == 0) {
        NewsMenuModel m;

        memset(&m, 0, sizeof(m));
        m.hour = 9;
        m.minute = 41;
        m.battery_level = 87;
        m.article = strcmp(screen, "news-menu-article") == 0;
        m.position = m.article ? 2 : 1;
        m.count = m.article ? 4 : page_count;
        draw_news_menu(canvas, fonts, icons, &m, scale);
    }
    return 1;
}

static int render_screen(Canvas *canvas, const char *screen, const Fonts *fonts,
                         const Icons *icons, float scale)
{
    canvas_clear(canvas, 255);
    if (strcmp(screen, "home") == 0) {
        HomeModel m;
        fixture_home(&m);
        draw_home(canvas, fonts, icons, &m, scale);
    } else if (strcmp(screen, "home-many") == 0) {
        HomeModel m;
        fixture_home_many_page(&m, 0);
        draw_home(canvas, fonts, icons, &m, scale);
    } else if (strcmp(screen, "home-many-2") == 0) {
        HomeModel m;
        fixture_home_many_page(&m, 1);
        draw_home(canvas, fonts, icons, &m, scale);
    } else if (strcmp(screen, "home-empty") == 0) {
        HomeModel m;
        fixture_home_empty(&m);
        draw_home_empty(canvas, fonts, icons, &m, scale);
    } else if (strcmp(screen, "settings") == 0) {
        SettingsModel m;
        fixture_settings(&m, 1);
        draw_settings(canvas, fonts, icons, &m, scale);
    } else if (strcmp(screen, "settings-wifi-off") == 0) {
        SettingsModel m;
        fixture_settings(&m, 0);
        draw_settings(canvas, fonts, icons, &m, scale);
    } else if (strcmp(screen, "settings-updating") == 0) {
        SettingsModel m;
        fixture_settings_updating(&m);
        draw_settings(canvas, fonts, icons, &m, scale);
    } else if (strcmp(screen, "news") == 0 ||
               strcmp(screen, "news-empty") == 0 ||
               strcmp(screen, "news-front") == 0 ||
               strcmp(screen, "news-list") == 0 ||
               strcmp(screen, "news-article") == 0 ||
               strcmp(screen, "news-article-mid") == 0 ||
               strcmp(screen, "news-article-last") == 0 ||
               strcmp(screen, "news-menu") == 0 ||
               strcmp(screen, "news-menu-article") == 0) {
        if (strcmp(screen, "news-empty") == 0) {
            draw_empty_news(canvas, fonts, icons, scale);
        } else {
            render_news_screen(canvas, screen, fonts, icons, scale);
        }
    } else if (strcmp(screen, "settings-confirm-reboot") == 0 ||
               strcmp(screen, "settings-confirm-stock") == 0) {
        SettingsModel m;
        KhomeConfirm confirm = strcmp(screen, "settings-confirm-reboot") == 0
                                   ? KHOME_CONFIRM_REBOOT
                                   : KHOME_CONFIRM_STOCK;
        fixture_settings(&m, 1);
        draw_settings(canvas, fonts, icons, &m, scale);
        draw_confirm(canvas, fonts, confirm, scale);
    } else if (strcmp(screen, "home-confirm-remove") == 0) {
        HomeModel m;
        fixture_home(&m);
        snprintf(m.favorites[0].name, sizeof(m.favorites[0].name), "%s",
                 "Hatena Bookmark - Japan's largest social bookmarking "
                 "service");
        draw_home(canvas, fonts, icons, &m, scale);
        draw_confirm_favorite(canvas, fonts, m.favorites[0].name, scale);
    } else if (strcmp(screen, "launching") == 0) {
        LaunchingModel m;
        fixture_launching(&m);
        draw_launching(canvas, fonts, icons, &m, scale);
    } else if (strcmp(screen, "sleep") == 0) {
        SleepModel m;
        fixture_sleep(&m);
        draw_sleep(canvas, fonts, icons, &m, scale);
    } else if (strcmp(screen, "url-edit") == 0 ||
               strcmp(screen, "url-edit-symbols") == 0 ||
               strcmp(screen, "url-edit-new") == 0) {
        UrlEdit e;
        if (strcmp(screen, "url-edit-symbols") == 0) {
            fixture_url_edit_symbols(&e);
        } else if (strcmp(screen, "url-edit-new") == 0) {
            urledit_init(&e, "");
        } else {
            fixture_url_edit(&e);
        }
        draw_url_edit(canvas, fonts, &e, scale);
    } else if (strcmp(screen, "sheet-3") == 0 ||
               strcmp(screen, "sheet-8") == 0) {
        TabSheetModel m;
        /* The sheet only covers the top, so the rest of the frame stands in
         * for whatever the browser has on screen. */
        canvas_clear(canvas, 153);
        if (strcmp(screen, "sheet-8") == 0) {
            fixture_tab_sheet_8(&m);
        } else {
            fixture_tab_sheet_3(&m);
        }
        draw_tab_sheet(canvas, fonts, icons, &m, scale);
    } else {
        return 0;
    }
    return 1;
}

static int render_one(const char *screen, float scale, const char *out_path,
                      const Fonts *fonts, const Icons *icons)
{
    int w = (int)(1072.0f * scale + 0.5f);
    int h = (int)(1448.0f * scale + 0.5f);
    Canvas canvas;
    if (!canvas_init(&canvas, w, h)) {
        fprintf(stderr, "khome-render: out of memory for %dx%d canvas\n", w, h);
        return 0;
    }
    if (!render_screen(&canvas, screen, fonts, icons, scale)) {
        fprintf(stderr, "khome-render: unknown screen '%s'\n", screen);
        canvas_free(&canvas);
        return 0;
    }
    if (!png_write_gray(out_path, &canvas)) {
        fprintf(stderr, "khome-render: cannot write %s\n", out_path);
        canvas_free(&canvas);
        return 0;
    }
    canvas_free(&canvas);
    return 1;
}

static int parse_and_render(const char *screen, const char *scale_name,
                            const char *out_path, const Fonts *fonts,
                            const Icons *icons)
{
    float scale = parse_scale(scale_name);
    if (scale < 0.0f) {
        fprintf(stderr, "khome-render: scale must be design or device\n");
        return 0;
    }
    return render_one(screen, scale, out_path, fonts, icons);
}

int main(int argc, char **argv)
{
    static const char *const font_dirs[] = { "design/system/fonts",
                                             "../design/system/fonts" };
    static const char *const icon_dirs[] = { "assets/icons",
                                             "launcher/assets/icons" };
    char font_buf[1024];
    char icon_buf[1024];
    const char *font_dir;
    const char *icon_dir;
    Fonts fonts;
    Icons icons;

    font_dir = pick_dir("KHOME_FONTS", font_dirs, 2, font_buf, sizeof(font_buf));
    icon_dir = pick_dir("KHOME_ICONS", icon_dirs, 2, icon_buf, sizeof(icon_buf));

    if (argc < 2) {
        fprintf(stderr,
                "usage: khome-render <screen> <design|device> <out.png>\n"
                "       khome-render all <outdir>\n"
                "       khome-render check-indent\n"
                "       screens include sheet-3, sheet-8, url-edit and "
                "url-edit-symbols\n");
        return 2;
    }
    if (!fonts_load(&fonts, font_dir)) {
        fprintf(stderr, "khome-render: fonts not found (set KHOME_FONTS)\n");
        return 1;
    }
    if (!icons_load(&icons, icon_dir)) {
        fprintf(stderr, "khome-render: icons not found (set KHOME_ICONS)\n");
        fonts_free(&fonts);
        return 1;
    }

    if (argc == 2 && strcmp(argv[1], "check-indent") == 0) {
        int status = check_indents(&fonts);

        fonts_free(&fonts);
        icons_free(&icons);
        return status;
    }

    if (strcmp(argv[1], "all") == 0) {
        char design_dir[1024];
        char device_dir[1024];
        int i;
        int ok = 1;
        if (argc < 3) {
            fprintf(stderr, "khome-render: all needs an output directory\n");
            fonts_free(&fonts);
            icons_free(&icons);
            return 2;
        }
        if (!ensure_dir(argv[2])) {
            ok = 0;
        }
        snprintf(design_dir, sizeof(design_dir), "%s/design", argv[2]);
        snprintf(device_dir, sizeof(device_dir), "%s/device", argv[2]);
        if (!ensure_dir(design_dir) || !ensure_dir(device_dir)) {
            ok = 0;
        }
        for (i = 0; ok && i < SCREEN_COUNT; i++) {
            char path[1200];
            snprintf(path, sizeof(path), "%s/%s.png", design_dir,
                     screen_names[i]);
            if (!render_one(screen_names[i], 1.0f, path, &fonts, &icons)) {
                ok = 0;
                break;
            }
            snprintf(path, sizeof(path), "%s/%s.png", device_dir,
                     screen_names[i]);
            if (!render_one(screen_names[i], 758.0f / 1072.0f, path, &fonts,
                            &icons)) {
                ok = 0;
                break;
            }
            printf("wrote %s\n", screen_names[i]);
        }
        fonts_free(&fonts);
        icons_free(&icons);
        return ok ? 0 : 1;
    }

    if (argc != 4) {
        fprintf(stderr,
                "usage: khome-render <screen> <design|device> <out.png>\n");
        fonts_free(&fonts);
        icons_free(&icons);
        return 2;
    }
    {
        int ok = parse_and_render(argv[1], argv[2], argv[3], &fonts, &icons);
        fonts_free(&fonts);
        icons_free(&icons);
        return ok ? 0 : 1;
    }
}
