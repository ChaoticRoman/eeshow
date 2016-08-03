/*
 * cro.h - Cairo graphics back-end
 *
 * Written 2016 by Werner Almesberger
 * Copyright 2016 by Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#ifndef CRO_H
#define	CRO_H

#include <stdint.h>

#include <cairo/cairo.h>

#include "gfx.h"


struct cro_ctx;


extern const struct gfx_ops cro_png_ops;
extern const struct gfx_ops cro_pdf_ops;

#define	cro_img_ops	cro_png_ops	/* just don't call cro_img_ops.end */
#define	cro_canvas_ops	cro_png_ops	/* just don't call cro_canvas_ops.end */


uint32_t *cro_img_end(void *ctx, int *w, int *h, int *stride);
void cro_img_write(void *ctx, const char *name);

void cro_canvas_end(void *ctx);
void cro_canvas_draw(struct cro_ctx *cc, cairo_t *cr);

#endif /* !CRO_H */
