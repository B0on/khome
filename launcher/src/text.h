#ifndef KHOME_TEXT_H
#define KHOME_TEXT_H

#include <stddef.h>
#include <stdint.h>

#include "canvas.h"
#include "stb_truetype.h"

typedef enum {
    FONT_SERIF_B = 0,
    FONT_SERIF_R,
    FONT_SANS_M,
    FONT_COUNT
} FontRole;

typedef enum {
    TEXT_LEFT = 0,
    TEXT_CENTER,
    TEXT_RIGHT
} TextAlign;

typedef struct GlyphCache GlyphCache;
typedef struct KernTable KernTable;

typedef struct {
    unsigned char *data;
    size_t size;
    stbtt_fontinfo info;
    GlyphCache *cache;
    KernTable *kern;
} Font;

typedef struct {
    Font font[FONT_COUNT];
} Fonts;

int fonts_load(Fonts *fonts, const char *dir);
void fonts_free(Fonts *fonts);
const Font *fonts_get(const Fonts *fonts, FontRole role);

/* Width in pixels, letter-spacing between characters only (no trailing one). */
float text_width(const Font *font, float size, float letter_spacing,
                 const char *utf8);
/* Copy src into dst, truncated to max_width with a trailing U+2026. */
void text_ellipsize(const Font *font, float size, float letter_spacing,
                    const char *src, float max_width, char *dst,
                    size_t dst_size);
/* Draw one line.  Returns the pen position after the run (including the
 * trailing letter-spacing, useful when chaining differently coloured runs). */
float text_draw(Canvas *canvas, const Font *font, float size,
                float letter_spacing, float x, float baseline,
                const char *utf8, uint8_t color, TextAlign align,
                float box_width);
/* Same, but only pixels inside the half-open clip rectangle are written. */
float text_draw_clip(Canvas *canvas, const Font *font, float size,
                     float letter_spacing, float x, float baseline,
                     const char *utf8, uint8_t color, TextAlign align,
                     float box_width, int clip_x0, int clip_y0, int clip_x1,
                     int clip_y1);

/* One wrapped line: [start, start + length) in the UTF-8 source, the
 * first-line indent in pixels (one em for a Japanese paragraph), and the
 * extra pixels to add to every inter-character gap so the line reaches the
 * column edge.  justify is 0 for a ragged line. */
typedef struct {
    size_t start;
    size_t length;
    float indent;
    float justify;
} TextLine;

/* Break utf8 into lines no wider than line_width and write them to the
 * caller's array.  "\n" starts a new paragraph; a paragraph whose first
 * character is not ASCII is indented one em.  Japanese breaks per character;
 * only "、。，．｡､" may hang past the right edge, one per line, and a closing
 * mark at the head of the next line pulls the character before it down with
 * it, or the whole word that character belongs to when that word fits a line.
 * Latin and fullwidth alphanumerics are words: a line breaks before the
 * word, and mid-word only when one word is wider than the line.
 * Returns the number of lines written.  When the text needs more than
 * max_lines lines, *truncated is set to 1 and the array holds the first
 * max_lines lines. */
int text_wrap(const Font *font, float size, float letter_spacing,
              const char *utf8, float line_width, TextLine *lines,
              int max_lines, int *truncated);

/* Draw lines [first_line, first_line + count) from a text_wrap result into
 * the rectangle (x, y, w, h), spreading each line by its justify value.
 * Baselines step by line_height, starting at y + ascent; pixels outside the
 * rectangle are clipped, except that one em to the right of w is kept so a
 * hanging punctuation mark can draw past the column edge.  Returns the number
 * of lines drawn. */
int text_draw_lines(Canvas *canvas, const Font *font, float size,
                    float letter_spacing, const char *utf8,
                    const TextLine *lines, int line_count, int first_line,
                    int count, float x, float y, float w, float h,
                    float line_height, uint8_t color);

#endif
