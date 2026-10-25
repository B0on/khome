#ifndef KHOME_GPOS_H
#define KHOME_GPOS_H

#include <stddef.h>

/* Horizontal pair kerning from GPOS.  stb_truetype does not read GPOS, and the
 * design fonts keep their Latin kerning there. */
typedef struct KernTable KernTable;

KernTable *kern_parse(const unsigned char *data, size_t size, int num_glyphs);
void kern_free(KernTable *kern);
int kern_value(const KernTable *kern, int glyph1, int glyph2);

#endif
