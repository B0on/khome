#ifndef KHOME_FB_H
#define KHOME_FB_H

#include <stddef.h>

#include "canvas.h"

/* The Kindle framebuffer is 8bpp grayscale with line_length > width, so a row
 * copy cannot be a single memcpy of the canvas. */
typedef struct {
    int fd;
    int width;
    int height;
    int line_length;
    unsigned char *mem;
    size_t mem_size;
} Fb;

int fb_open(Fb *fb);
void fb_close(Fb *fb);
/* Re-reads the panel geometry.  winmgr changes xres/yres and line_length when
 * an app asks for landscape, and changes them back when it unloads, so a
 * drawing pass must not trust the values fb_open() saw at startup. */
int fb_refresh_geometry(Fb *fb);
int fb_blit(Fb *fb, const Canvas *canvas);
/* Copies only the given device-pixel rectangle, leaving the rest of the
 * framebuffer (for example the browser X wrote) untouched. */
int fb_blit_rect(Fb *fb, const Canvas *canvas, int x, int y, int w, int h);
/* These fork/exec the FBInk CLI and block until it exits.  The _wave variants
 * let the newspaper pick GL16 for a page flip and GC16 for a full one. */
int fb_refresh_full(Fb *fb);
int fb_refresh_rect(Fb *fb, int x, int y, int w, int h);
int fb_refresh_full_wave(Fb *fb, const char *wave);
int fb_refresh_rect_wave(Fb *fb, int x, int y, int w, int h,
                         const char *wave);

#endif
