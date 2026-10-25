#ifndef KHOME_LAYOUT_H
#define KHOME_LAYOUT_H

#include "canvas.h"
#include "model.h"
#include "state.h"
#include "text.h"
#include "urledit.h"

/* A tap rectangle in design units (1072 x 1448).  The action name is stable so
 * the input layer can dispatch without knowing about pixels. */
typedef struct {
    char action[32];
    int x;
    int y;
    int w;
    int h;
    /* Tab index for sheet_tab / sheet_close, favorite index for fav_open;
     * 0 for every other action. */
    int index;
} TapRect;

/* SPEC 5: the fixed rectangle repainted every minute. */
void layout_clock_rect(int *x, int *y, int *w, int *h);

/* SPEC 4: fills up to max entries and returns the number written. */
int layout_home_taps(const HomeModel *model, TapRect *out, int max);
int layout_settings_taps(TapRect *out, int max);
/* Button rectangles of the confirm dialog drawn over the settings screen. */
int layout_confirm_taps(KhomeConfirm confirm, TapRect *out, int max);
/* NEWS screen: the left 30% turns back a page and the rest turns forward.
 * The text paper's article and index rectangles are tested before these. */
int layout_news_taps(TapRect *out, int max);

/* NEWS pull-down menu height in design units, from y=0. */
int layout_news_menu_height(void);
/* Fills the menu buttons and its dismiss targets, and returns the number
 * written.  The Back to paper button exists only for an article. */
int layout_news_menu_taps(const NewsMenuModel *model, TapRect *out, int max);
void draw_news_menu(Canvas *canvas, const Fonts *fonts, const Icons *icons,
                    const NewsMenuModel *model, float scale);
/* Just the clock, battery and rule at the top of the menu. */
void draw_news_menu_status(Canvas *canvas, const Fonts *fonts,
                           const Icons *icons, const NewsMenuModel *model,
                           float scale);

/* Sheet height in design units, from y=0.  Eight tabs stay under 70% of the
 * screen. */
int layout_tab_sheet_height(const TabSheetModel *model);
/* Fills the sheet actions and returns the number written. */
int layout_tab_sheet_taps(const TabSheetModel *model, TapRect *out, int max);

void draw_home(Canvas *canvas, const Fonts *fonts, const Icons *icons,
               const HomeModel *model, float scale);
void draw_home_empty(Canvas *canvas, const Fonts *fonts, const Icons *icons,
                     const HomeModel *model, float scale);
void draw_settings(Canvas *canvas, const Fonts *fonts, const Icons *icons,
                   const SettingsModel *model, float scale);
/* Draws only the dialog box; the caller draws the settings screen under it. */
void draw_confirm(Canvas *canvas, const Fonts *fonts, KhomeConfirm confirm,
                  float scale);
/* The remove-favorite dialog, with the site name as its body line. */
void draw_confirm_favorite(Canvas *canvas, const Fonts *fonts, const char *name,
                           float scale);
void draw_launching(Canvas *canvas, const Fonts *fonts, const Icons *icons,
                    const LaunchingModel *model, float scale);
/* Stand-in when the text edition is missing. */
void draw_news_empty(Canvas *canvas, const Fonts *fonts, const Icons *icons,
                     const HomeModel *model, float scale);
void draw_sleep(Canvas *canvas, const Fonts *fonts, const Icons *icons,
                const SleepModel *model, float scale);
/* Draws the browser tab sheet from the top edge down to
 * layout_tab_sheet_height().  The caller draws whatever is below it. */
void draw_tab_sheet(Canvas *canvas, const Fonts *fonts, const Icons *icons,
                    const TabSheetModel *model, float scale);
/* Just the clock, battery and rule at the top of the sheet. */
void draw_tab_sheet_status(Canvas *canvas, const Fonts *fonts,
                           const Icons *icons, const TabSheetModel *model,
                           float scale);

/* Full-screen URL editor.  The field and keyboard rectangles are separate so
 * a keystroke can repaint only one of them. */
void draw_url_edit(Canvas *canvas, const Fonts *fonts, UrlEdit *edit,
                   float scale);
void draw_url_edit_field(Canvas *canvas, const Fonts *fonts,
                         UrlEdit *edit, float scale);
void draw_url_edit_keyboard(Canvas *canvas, const Fonts *fonts,
                            const UrlEdit *edit, float scale);
void layout_url_edit_field_rect(int *x, int *y, int *w, int *h);
void layout_url_edit_keyboard_rect(int *x, int *y, int *w, int *h);
/* Fills the editor actions and keys, and returns the number written. */
int layout_url_edit_taps(const UrlEdit *edit, TapRect *out, int max);
/* Byte offset in edit closest to a tap at design (x, y) inside the field. */
size_t layout_url_edit_cursor_at(const Fonts *fonts, const UrlEdit *edit,
                                 int x, int y);

#endif
