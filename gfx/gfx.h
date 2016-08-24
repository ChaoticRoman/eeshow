/*
 * gfx/gfx.h - Generate graphical output for Eeschema items
 *
 * Written 2016 by Werner Almesberger
 * Copyright 2016 by Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#ifndef GFX_GFX_H
#define	GFX_GFX_H

#include <stdbool.h>

#include "gfx/text.h"


struct gfx;

struct gfx_ops {
	const char *name;
	void (*line)(void *ctx, int sx, int sy, int ex, int ey,
	    int color, unsigned layer);
	void (*rect)(void *ctx, int sx, int sy, int ex, int ey,
	    int color, int fill_color, unsigned layer);
	void (*poly)(void *ctx,
	    int points, const int x[points], const int y[points],
	    int color, int fill_color, unsigned layer);
	void (*circ)(void *ctx, int x, int y, int r,
	    int color, int fill_color, unsigned layer);
	void (*arc)(void *ctx, int x, int y, int r, int sa, int ea,
	    int color, int fill_color, unsigned layer);
	void (*text)(void *ctx, int x, int y, const char *s, unsigned size,
	    enum text_align align, int rot, unsigned color, unsigned layer);
	void (*tag)(void *ctx,  const char *s,
	    int points, const int x[points], const int y[points]);
	unsigned (*text_width)(void *ctx, const char *s, unsigned size);
	void *(*init)(void);
	bool (*args)(void *ctx, int argc, char *const *argv);
	void (*sheet_name)(void *ctx, const char *name);
	void (*new_sheet)(void *ctx);
	void (*end)(void *ctx);
};


/* wrappers */

void gfx_line(struct gfx *gfx,
    int sx, int sy, int ex, int ey, int color, unsigned layer);
void gfx_rect(struct gfx *gfx, int sx, int sy, int ex, int ey,
    int color, int fill_color, unsigned layer);
void gfx_poly(struct gfx *gfx,
    int points, const int x[points], const int y[points],
    int color, int fill_color, unsigned layer);
void gfx_circ(struct gfx *gfx,
    int x, int y, int r, int color, int fill_color, unsigned layer);
void gfx_arc(struct gfx *gfx, int x, int y, int r, int sa, int ea,
    int color, int fill_color, unsigned layer);
void gfx_text(struct gfx *gfx, int x, int y, const char *s, unsigned size,
    enum text_align align, int rot, unsigned color, unsigned layer);
void gfx_tag(struct gfx *gfx, const char *s,
    unsigned points, const int x[points], int const y[points]);
unsigned gfx_text_width(struct gfx *gfx, const char *s, unsigned size);

/* inititalization and termination */

struct gfx *gfx_init(const struct gfx_ops *ops);
bool gfx_args(struct gfx *gfx, int argc, char *const *argv);
void gfx_sheet_name(struct gfx *gfx, const char *name);
void gfx_new_sheet(struct gfx *gfx);
bool gfx_multi_sheet(struct gfx *gfx);
void *gfx_user(struct gfx *gfx);	/* transitional kludge */
void gfx_end(struct gfx *gfx);

#endif /* !GFX_GFX_H */
