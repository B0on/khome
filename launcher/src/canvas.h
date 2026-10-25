#ifndef KHOME_CANVAS_H
#define KHOME_CANVAS_H

#include <stddef.h>
#include <stdint.h>

/* 8-bit grayscale drawing surface.  Row 0 is the top of the screen. */
typedef struct {
    int w;
    int h;
    int stride;
    uint8_t *pixels;
} Canvas;

/* A decoded 8-bit grayscale image (used for the icon PNGs). */
typedef struct {
    int w;
    int h;
    uint8_t *pixels;
} Bitmap;

int canvas_init(Canvas *c, int w, int h);
void canvas_free(Canvas *c);
void canvas_clear(Canvas *c, uint8_t value);
void canvas_fill_rect(Canvas *c, int x, int y, int w, int h, uint8_t value);
void canvas_outline_rect(Canvas *c, int x, int y, int w, int h, int thickness,
                         uint8_t value);
void canvas_fill_circle(Canvas *c, double cx, double cy, double radius,
                        uint8_t value);
void canvas_outline_circle(Canvas *c, double cx, double cy, double radius,
                           double thickness, uint8_t value);
void canvas_arc(Canvas *c, double cx, double cy, double radius,
                double thickness, double start_rad, double end_rad,
                uint8_t value);
/* Square-capped stroke, like the SVG symbols in the design system. */
void canvas_line(Canvas *c, double x0, double y0, double x1, double y1,
                 double thickness, uint8_t value);
void canvas_round_rect_fill(Canvas *c, double x, double y, double w, double h,
                            double radius, uint8_t value);
void canvas_round_rect_outline(Canvas *c, double x, double y, double w,
                               double h, double radius, double thickness,
                               uint8_t value);
/* 1:1 opaque blit (the icon PNGs have a white background). */
void canvas_blit(Canvas *c, int x, int y, const Bitmap *src);
/* Opaque blit scaled into a w x h destination box.  Each destination pixel is
 * the area average of the source pixels it covers, so thin newspaper type
 * survives a reduction without the gaps nearest-neighbour leaves. */
void canvas_blit_scaled(Canvas *c, int x, int y, int w, int h,
                        const Bitmap *src);

/* Snap every pixel to the 16 tones the panel can show (0, 17, ..., 255). */
void canvas_quantize16(Canvas *c);

int bitmap_load_gray(Bitmap *b, const char *path);
void bitmap_free(Bitmap *b);

/* The six design-system icons, pre-rendered at the design size (1072-wide
 * artwork) and at the Paperwhite 2 size (round(design * 758 / 1072)). */
enum {
    ICON_NEWSPAPER = 0,
    ICON_MANGA,
    ICON_BROWSER,
    ICON_SETTINGS,
    ICON_WIFI,
    ICON_BATTERY,
    ICON_COUNT
};

typedef struct {
    Bitmap design[ICON_COUNT];
    Bitmap device[ICON_COUNT];
} Icons;

int icons_load(Icons *icons, const char *dir);
void icons_free(Icons *icons);
const Bitmap *icons_get(const Icons *icons, int id, float scale);

#endif
