/*
 * gfx/cro.h - Cairo graphics back-end
 *
 * Written 2016 by Werner Almesberger
 * Copyright 2016 by Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#ifndef GFX_CRO_H
#define	GFX_CRO_H

#include <stdint.h>

#include <cairo/cairo.h>

#include "gfx/gfx.h"


struct cro_ctx;


extern const struct gfx_ops cro_png_ops;
extern const struct gfx_ops cro_pdf_ops;

#define	cro_img_ops	cro_png_ops	/* just don't call cro_img_ops.end */
#define	cro_canvas_ops	cro_png_ops	/* just don't call cro_canvas_ops.end */


void cro_color_override(struct cro_ctx *cc, int color);

void cro_get_size(const struct cro_ctx *cc, int *w, int *h, int *x, int *y);

uint32_t *cro_img_end(struct cro_ctx *cc, int *w, int *h, int *stride);
void cro_img_write(struct cro_ctx *cc, const char *name);

void cro_canvas_end(struct cro_ctx *cc, int *w, int *h, int *xmin, int *ymin);
void cro_canvas_prepare(cairo_t *cr);
void cro_canvas_draw(struct cro_ctx *cc, cairo_t *cr,
    int x, int y, float scale);

uint32_t *cro_img(struct cro_ctx *ctx, int x0, int yo, int w, int h,
    float scale, cairo_t **res_cr, int *res_stride);

#endif /* !GFX_CRO_H */
