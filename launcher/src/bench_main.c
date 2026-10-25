#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "canvas.h"
#include "fixtures.h"
#include "layout.h"
#include "text.h"

static const char *const screen_names[] = {
    "home",       "home-many", "home-many-2", "home-empty",
    "settings",   "settings-wifi-off", "launching", "sleep"
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
    } else if (strcmp(screen, "launching") == 0) {
        LaunchingModel m;
        fixture_launching(&m);
        draw_launching(canvas, fonts, icons, &m, scale);
    } else if (strcmp(screen, "sleep") == 0) {
        SleepModel m;
        fixture_sleep(&m);
        draw_sleep(canvas, fonts, icons, &m, scale);
    } else {
        return 0;
    }
    return 1;
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

int main(int argc, char **argv)
{
    static const char *const font_dirs[] = { "design/system/fonts",
                                             "../design/system/fonts" };
    static const char *const icon_dirs[] = { "assets/icons",
                                             "launcher/assets/icons" };
    char font_buf[1024];
    char icon_buf[1024];
    const char *font_dir =
        pick_dir("KHOME_FONTS", font_dirs, 2, font_buf, sizeof(font_buf));
    const char *icon_dir =
        pick_dir("KHOME_ICONS", icon_dirs, 2, icon_buf, sizeof(icon_buf));
    Fonts fonts;
    Icons icons;
    Canvas canvas;
    int i;

    (void)argc;
    (void)argv;
    if (!fonts_load(&fonts, font_dir)) {
        fprintf(stderr, "khome-bench: fonts not found (set KHOME_FONTS)\n");
        return 1;
    }
    if (!icons_load(&icons, icon_dir)) {
        fprintf(stderr, "khome-bench: icons not found (set KHOME_ICONS)\n");
        fonts_free(&fonts);
        return 1;
    }
    if (!canvas_init(&canvas, 758, 1024)) {
        fprintf(stderr, "khome-bench: out of memory\n");
        icons_free(&icons);
        fonts_free(&fonts);
        return 1;
    }
    printf("khome-bench: 758x1024, %d screens, average of 5 draws\n",
           SCREEN_COUNT);
    for (i = 0; i < SCREEN_COUNT; i++) {
        double best = 0.0;
        int run;
        if (!render_screen(&canvas, screen_names[i], &fonts, &icons,
                           758.0f / 1072.0f)) {
            fprintf(stderr, "khome-bench: unknown screen %s\n",
                    screen_names[i]);
            continue;
        }
        for (run = 0; run < 5; run++) {
            double t0 = now_ms();
            render_screen(&canvas, screen_names[i], &fonts, &icons,
                          758.0f / 1072.0f);
            best += now_ms() - t0;
        }
        printf("  %-18s %8.2f ms\n", screen_names[i], best / 5.0);
    }
    canvas_free(&canvas);
    icons_free(&icons);
    fonts_free(&fonts);
    return 0;
}
