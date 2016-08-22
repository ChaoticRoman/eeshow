/*
 * gfx/diff.c - Schematics difference
 *
 * Written 2016 by Werner Almesberger
 * Copyright 2016 by Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <cairo/cairo.h>

#include "misc/util.h"
#include "misc/diag.h"
#include "main.h"
#include "gfx/cro.h"
#include "file/file.h"
#include "kicad/sch.h"
#include "kicad/lib.h"
#include "gfx/record.h"
#include "gfx/diff.h"


#define	DEFAULT_FRAME_RADIUS	30

#define	FADE_SHIFT	3
#define	FADE_MASK	((0xff >> FADE_SHIFT) * (0x010101))
#define	FADE_OFFSET	(~FADE_MASK & 0xffffff)

#define	MASK 0xffffff

/* steal from schhist/ppmdiff.c */

#define	ONLY_OLD	0xff5050
#define	ONLY_NEW	0x00c000
#define	BOTH		0x707070

#define	AREA_FILL	0xffd0f0


struct diff {
	void *cr_ctx;
	uint32_t *new_img;
	int w, h, stride;
	const char *output_name;
	int frame_radius;
	struct area *areas;
};


/* ----- Wrappers ---------------------------------------------------------- */


static void diff_line(void *ctx, int sx, int sy, int ex, int ey,
    int color, unsigned layer)
{
	const struct diff *diff = ctx;

	cro_img_ops.line(diff->cr_ctx, sx, sy, ex, ey, color, layer);
}


static void diff_poly(void *ctx,
    int points, const int x[points], const int y[points],
    int color, int fill_color, unsigned layer)
{
	const struct diff *diff = ctx;

	cro_img_ops.poly(diff->cr_ctx, points, x, y, color, fill_color, layer);
}


static void diff_circ(void *ctx, int x, int y, int r,
    int color, int fill_color, unsigned layer)
{
	const struct diff *diff = ctx;

	cro_img_ops.circ(diff->cr_ctx, x, y, r, color, fill_color, layer);
}


static void diff_arc(void *ctx, int x, int y, int r, int sa, int ea,
    int color, int fill_color, unsigned layer)
{
	const struct diff *diff = ctx;

	cro_img_ops.arc(diff->cr_ctx, x, y, r, sa, ea,
	    color, fill_color, layer);
}


static void diff_text(void *ctx, int x, int y, const char *s, unsigned size,
    enum text_align align, int rot, unsigned color, unsigned layer)
{
	const struct diff *diff = ctx;

	cro_img_ops.text(diff->cr_ctx, x, y, s, size, align, rot,
	    color, layer);
}


static unsigned diff_text_width(void *ctx, const char *s, unsigned size)
{
	const struct diff *diff = ctx;

	return cro_img_ops.text_width(diff->cr_ctx, s, size);
}


/* ----- Initialization ---------------------------------------------------- */


static void *diff_init(int argc, char *const *argv)
{
	struct diff *diff;
	char c;
	int arg;
	struct sch_ctx new_sch;
	struct file sch_file;
	struct lib new_lib;

	diff = alloc_type(struct diff);
	diff->areas = NULL;

	sch_init(&new_sch, 0);
	lib_init(&new_lib);

	diff->output_name = NULL;
	diff->frame_radius = DEFAULT_FRAME_RADIUS;
	while ((c = getopt(argc, argv, "o:s:")) != EOF)
		switch (c) {
		case 'o':
			diff->output_name = optarg;
			break;
		case 's':
			/* for cro_png */
			break;
		default:
			usage(*argv);
		}

	if (argc - optind < 1)
		usage(*argv);

	if (!file_open(&sch_file, argv[argc - 1], NULL)) 
		goto fail_open;
	for (arg = optind; arg != argc - 1; arg++)
		if (!lib_parse(&new_lib, argv[arg], &sch_file))
			goto fail_parse;
	if (!sch_parse(&new_sch, &sch_file, &new_lib, NULL))
		goto fail_parse;
	file_close(&sch_file);

	optind = 0;
	gfx_init(&cro_img_ops, argc, argv);
	diff->cr_ctx = gfx_ctx;
	sch_render(new_sch.sheets);
	diff->new_img = cro_img_end(gfx_ctx,
	    &diff->w, &diff->h, &diff->stride);

	optind = 0;
	diff->cr_ctx = cro_img_ops.init(argc, argv);

	return diff;

fail_parse:
	file_close(&sch_file);
fail_open:
	sch_free(&new_sch);
	lib_free(&new_lib);
	free(diff);
	return NULL;
}


/* ----- Area highlighting ------------------------------------------------- */


void add_area(struct area **areas, int xa, int ya, int xb, int yb,
    uint32_t color)
{
	struct area *area;

	area = alloc_type(struct area);

	area->xa = xa;
	area->ya = ya;
	area->xb = xb;
	area->yb = yb;
	area->color = color;

	area->next = *areas;
	*areas = area;
}


static void mark_area(struct diff *diff, int x, int y)
{
	struct area *area;
	int xa = x - diff->frame_radius;
	int ya = y - diff->frame_radius;
	int xb = x + diff->frame_radius;
	int yb = y + diff->frame_radius;

	for (area = diff->areas; area; area = area->next)
		if (x >= area->xa && x <= area->xb &&
		    y >= area->ya && y <= area->yb) {
			if (area->xa > xa)
				area->xa = xa;
			if (area->xb < xb)
				area->xb = xb;
			if (area->ya > ya)
				area->ya = ya;
			if (area->yb < yb)
				area->yb = yb;
			return;
		}

	add_area(&diff->areas, xa, ya, xb, yb, AREA_FILL);
}


static void complement_box(struct diff *diff, uint32_t *a,
    int xa, int ya, int xb, int yb, uint32_t color)
{
	int sx, sy, ex, ey;
	uint32_t *p;
	int x, y;

	sx = xa > 0 ? xa : 0;
	ex = xb < diff->w ? xb : diff->w;
	sy = ya > 0 ? ya : 0;
	ey = yb < diff->h ? yb : diff->h;

	if (sx >= ex || sy >= ey)
		return;

	for (y = sy; y != ey; y++) {
		p = a + y * (diff->stride >> 2);
		for (x = sx; x != ex; x++)
			if ((p[x] & MASK) == MASK)
				p[x] = color;
	}
}


static void show_areas(struct diff *diff, uint32_t *a)
{
	const struct area *area;

	for (area = diff->areas; area; area = area->next)
		complement_box(diff, a, area->xa, area->ya, area->xb, area->yb,
		    area->color);
}


void free_areas(struct area **areas)
{
	struct area *next;

	while (*areas) {
		next = (*areas)->next;
		free(*areas);
		*areas = next;
	}
}


/* ----- Termination ------------------------------------------------------- */


static void differences(struct diff *diff, uint32_t *a, const uint32_t *b)
{
	unsigned skip = diff->w * 4 - diff->stride;
	int x, y;

	for (y = 0; y != diff->h; y++) {
		for (x = 0; x != diff->w; x++) {
			if (!((*a ^ *b) & MASK)) {
				*a = ((*a >> FADE_SHIFT) & FADE_MASK) |
				    FADE_OFFSET;
			} else {
				mark_area(diff, x, y);
				*a = (*a & MASK) == MASK ? ONLY_NEW :
				    (*b & MASK) == MASK ? ONLY_OLD : BOTH;
			}
			a++;
			b++;
		}
		a += skip;
		b += skip;
	}
}


static void diff_end(void *ctx)
{
	struct diff *diff = ctx;
	uint32_t *old_img;
	int w, h, stride;

	old_img = cro_img_end(diff->cr_ctx, &w, &h, &stride);
	if (diff->w != w || diff->h != h)
		fatal("%d x %d vs. %d x %d image\n", w, h, diff->w, diff->h);

	differences(diff, old_img, diff->new_img);
	show_areas(diff, old_img);
	free_areas(&diff->areas);

	cro_img_write(diff->cr_ctx, diff->output_name);
}


/* ----- Diff to canvas ---------------------------------------------------- */


static void merge_coord(int pos_a, int pos_b, int dim_a, int dim_b,
    int *pos_res, int *res_dim)
{
	if (pos_a < pos_b) {
		*pos_res = pos_a;
		dim_b += pos_b - pos_a;
	} else {
		*pos_res = pos_b;
		dim_a += pos_a - pos_b;
	}
	*res_dim = dim_a > dim_b ? dim_a : dim_b;
}


void diff_to_canvas(cairo_t *cr, int cx, int cy, float scale,
    struct cro_ctx *old, struct cro_ctx *new,
    const struct area *areas)
{
	int old_xmin, old_ymin, old_w, old_h;
	int new_xmin, new_ymin, new_w, new_h;
	int xmin, ymin, w, h, stride;
	uint32_t *img_old, *img_new;
	double x1, y1, x2, y2;
	int sw, sh;
	cairo_t *old_cr;
	cairo_surface_t *s;

	cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
	sw = x2 - x1;
	sh = y2 - y1;

	/* @@@ baeh ! */
	record_bbox((const struct record *) old,
	    &old_xmin, &old_ymin, &old_w, &old_h);
	record_bbox((const struct record *) new,
	    &new_xmin, &new_ymin, &new_w, &new_h);

	merge_coord(old_xmin, new_xmin, old_w, new_w, &xmin, &w);
	merge_coord(old_ymin, new_ymin, old_h, new_h, &ymin, &h);

	img_old = cro_img(old,
	    sw / 2.0 - (cx + xmin) * scale,
	    sh / 2.0 - (cy + ymin) * scale,
	    sw, sh,
	    scale, &old_cr, &stride);
	img_new = cro_img(new,
	    sw / 2.0 - (cx + xmin) * scale,
	    sh / 2.0 - (cy + ymin) * scale,
	    sw, sh,
	    scale, NULL, NULL);

	struct diff diff = {
		.w		= sw,
		.h		= sh,
		.stride		= stride,
		.frame_radius	= DEFAULT_FRAME_RADIUS,
		.areas		= NULL,
	};

	s = cairo_get_target(old_cr);
	cairo_surface_flush(s);
	differences(&diff, img_old, img_new);
	show_areas(&diff, img_old);
	free_areas(&diff.areas);
	if (areas) {
		diff.areas = (struct area *) areas;
		show_areas(&diff, img_old);
	}
	cairo_surface_mark_dirty(s);

	cairo_set_source_surface(cr, s, 0, 0);
	cairo_paint(cr);

	cairo_surface_destroy(s);
	cairo_destroy(old_cr);
	free(img_old);
	free(img_new);
}


/* ----- Operations -------------------------------------------------------- */


const struct gfx_ops diff_ops = {
	.name		= "diff",
	.line		= diff_line,
	.poly		= diff_poly,
	.circ		= diff_circ,
	.arc		= diff_arc,
	.text		= diff_text,
	.text_width	= diff_text_width,
	.init		= diff_init,
	.end		= diff_end,
};
