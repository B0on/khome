#ifndef KHOME_PNG_OUT_H
#define KHOME_PNG_OUT_H

#include "canvas.h"

/* Quantize to the panel's 16 tones and write an 8-bit grayscale PNG. */
int png_write_gray(const char *path, Canvas *canvas);

#endif
