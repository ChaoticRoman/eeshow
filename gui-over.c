/*
 * gui-over.c - GUI: overlays
 *
 * Written 2016 by Werner Almesberger
 * Copyright 2016 by Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
 * Resources:
 *
 * http://zetcode.com/gfx/cairo/cairobackends/
 * https://developer.gnome.org/gtk3/stable/gtk-migrating-2-to-3.html
 * https://www.cairographics.org/samples/rounded_rectangle/
 */

#include <stddef.h>
#include <stdlib.h>
#include <math.h>

#include <cairo/cairo.h>

#include "util.h"
#include "gui-over.h"


#define	OVER_FONT_SIZE	16
#define	OVER_BORDER	8
#define	OVER_RADIUS	6
#define	OVER_SEP	8
#define	OVER_X0		10
#define	OVER_Y0		10


struct overlay {
	const char *s;
	struct overlay *next;
};


static void rrect(cairo_t *cr, int x, int y, int w, int h, int r)
{
	const double deg = M_PI / 180.0;

	cairo_new_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -90 * deg, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, 90 * deg);
	cairo_arc(cr, x + r, y + h - r, r, 90 * deg, 180 * deg);
	cairo_arc(cr, x + r, y + r, r, 180 * deg, 270 * deg);
	cairo_close_path(cr);
}


static void overlay_draw(const struct overlay *over, cairo_t *cr,
    int *x, int *y)
{
	cairo_text_extents_t ext;

	cairo_set_font_size(cr, OVER_FONT_SIZE);
	cairo_text_extents(cr, over->s, &ext);

	rrect(cr, *x, *y,
	    ext.width + 2 * OVER_BORDER, ext.height + 2 * OVER_BORDER,
	    OVER_RADIUS);

	cairo_set_source_rgba(cr, 0.8, 0.9, 1, 0.8);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.5, 0.5, 1, 0.7);
	cairo_set_line_width(cr, 2);
	cairo_stroke(cr);

	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_move_to(cr, *x + OVER_BORDER, *y + OVER_BORDER + ext.height);
	cairo_show_text(cr, over->s);

	*y += ext.height + OVER_SEP;
}


void overlay_draw_all(const struct overlay *overlays, cairo_t *cr)
{
	const struct overlay *over;
	int x = OVER_X0;
	int y = OVER_Y0;

	for (over = overlays; over; over = over->next)
		overlay_draw(over, cr, &x, &y);
}


struct overlay *overlay_add(struct overlay **overlays, const char *s)
{
	struct overlay *over;
	struct overlay **anchor;
	
	over = alloc_type(struct overlay);
	over->s = stralloc(s);

	for (anchor = overlays; *anchor; anchor = &(*anchor)->next);
	over->next = NULL;
	*anchor = over;

	return over;
}


static void overlay_free(struct overlay *over)
{
	free((void *) over->s);
	free(over);
}


void overlay_remove(struct overlay **overlays, struct overlay *over)
{
	struct overlay **anchor;

	for (anchor = overlays; *anchor; anchor = &(*anchor)->next)
		if (*anchor == over) {
			*anchor = over->next;
			overlay_free(over);
			return;
		}
	abort();
}


void overlay_remove_all(struct overlay **overlays)
{
	struct overlay *next;

	while (*overlays) {
		next = (*overlays)->next;
		overlay_free(*overlays);
		*overlays = next;
	}
}
