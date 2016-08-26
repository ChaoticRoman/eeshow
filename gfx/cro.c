/*
 * gfx/cro.c - Cairo graphics back-end
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
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <assert.h>

#include <cairo/cairo.h>
#include <cairo/cairo-pdf.h>
#include <pango/pangocairo.h>

#include "misc/util.h"
#include "misc/diag.h"
#include "gfx/style.h"
#include "gfx/text.h"
#include "gfx/gfx.h"
#include "gfx/record.h"
#include "gfx/pdftoc.h"
#include "main.h"
#include "gfx/cro.h"


/*
 * FIG works with 1/1200 in
 * KiCad works with mil
 * 1 point = 1/72 in
 */

#define	DEFAULT_SCALE	(72.0 / 1200)


bool use_pango = 0;
bool disable_overline = 0;


struct cro_ctx {
	struct record record;	/* must be first */

	int xo, yo;		/* offset in target (e.g., canvas) coord */
	float scale, default_scale;

	cairo_t *cr;
	cairo_surface_t *s;

	PangoFontDescription *pango_desc;
	PangoLayout *pango_layout;
	double pango_size;

	struct record *sheets;	/* for PDF */
	unsigned n_sheets;

	const char *output_name;

	bool add_toc;
	struct pdftoc *toc;

	int color_override;	/* FIG color, COLOR_NONE if no override */
};


static inline double cd(const struct cro_ctx *cc, int x)
{
	return x * cc->scale;
}


static inline double dc(const struct cro_ctx *cc, double x)
{
	return x / cc->scale;
}


static inline int cx(const struct cro_ctx *cc, int x)
{
	return cc->xo + x * cc->scale;
}


static inline int cy(const struct cro_ctx *cc, int y)
{
	return cc->yo + y * cc->scale;
}


static inline float pt(const struct cro_ctx *cc, int x)
{
	return cd(cc, x) * 72 * 1.5 / 1200.0;
}


static void set_color(struct cro_ctx *cc, int color)
{
	uint32_t c;

	if (cc->color_override != COLOR_NONE)
		color = cc->color_override;
	if (color < 0)
		return;
	c = color_rgb[color];
	cairo_set_source_rgb(cc->cr,
	    (c >> 16) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0);
}


static void paint(struct cro_ctx *cc, int color, int fill_color)
{
	if (fill_color != COLOR_NONE) {
		set_color(cc, fill_color);
		if (color == COLOR_NONE)
			cairo_fill(cc->cr);
		else
			cairo_fill_preserve(cc->cr);
	}
	if (color != COLOR_NONE) {
		set_color(cc, color);
		cairo_stroke(cc->cr);
	}
}


/* ----- General items ----------------------------------------------------- */


static void cr_line(void *ctx, int sx, int sy, int ex, int ey,
    int color, unsigned layer)
{
	struct cro_ctx *cc = ctx;
	static const double dashes[] = { 2, 4 };

	cairo_new_path(cc->cr);
	cairo_move_to(cc->cr, cx(cc, sx), cy(cc, sy));
	cairo_line_to(cc->cr, cx(cc, ex), cy(cc, ey));
	cairo_set_dash(cc->cr, dashes, ARRAY_ELEMENTS(dashes), 0);
	paint(cc, color, COLOR_NONE);
	cairo_set_dash(cc->cr, NULL, 0, 0);
}


static void cr_poly(void *ctx,
    int points, const int x[points], const int y[points],
    int color, int fill_color, unsigned layer)
{
	struct cro_ctx *cc = ctx;
	bool closed;
	int i;

	if (points < 2)
		return;
	closed = x[0] == x[points - 1] && y[0] == y[points - 1];

	cairo_new_path(cc->cr);
	cairo_move_to(cc->cr, cx(cc, x[0]), cy(cc, y[0]));

	for (i = 1; i != points - closed; i++)
		cairo_line_to(cc->cr, cx(cc, x[i]), cy(cc, y[i]));
	if (closed)
		cairo_close_path(cc->cr);

	paint(cc, color, fill_color);
}


static void cr_circ(void *ctx, int x, int y, int r,
    int color, int fill_color, unsigned layer)
{
	struct cro_ctx *cc = ctx;

	cairo_new_path(cc->cr);
	cairo_arc(cc->cr, cx(cc, x), cy(cc, y), cd(cc, r), 0, 2 * M_PI);
	paint(cc, color, fill_color);
}


static void cr_arc(void *ctx, int x, int y, int r, int sa, int ea,
    int color, int fill_color, unsigned layer)
{
	struct cro_ctx *cc = ctx;

	cairo_new_path(cc->cr);
	cairo_arc(cc->cr, cx(cc, x), cy(cc, y), cd(cc, r),
	    -ea / 180.0 * M_PI, -sa / 180.0 * M_PI);
	if (fill_color != COLOR_NONE)
		cairo_close_path(cc->cr);
	paint(cc, color, fill_color);
}


static void overline(cairo_t *cr, double ox, double oy, double ex, double ey,
    double h)
{
	if (oy == ey) {
		oy -= h;
		ey -= h;
	} else {
		ox -= h;
		ex -= h;
	}
	cairo_save(cr);
	cairo_move_to(cr, ox, oy);
	cairo_line_to(cr, ex, ey);
	// @@@ should adjust line width to match text
	cairo_stroke(cr);
	cairo_restore(cr);
}


// https://cairographics.org/manual/cairo-cairo-scaled-font-t.html#cairo-scaled-font-text-to-glyphs
// https://en.wikipedia.org/wiki/UTF-8

static void overlined(cairo_t *cr, const char *s, double h)
{
	cairo_status_t status;
	cairo_glyph_t *glyphs = NULL;
	int num_glyphs = 0;
	double cpx, cpy;
	cairo_glyph_t *g, *last;
	double off_x = 0, off_y = 0;
	bool overlining = 0;
	cairo_text_extents_t ext;
	double ox, oy;

	cairo_get_current_point(cr, &cpx, &cpy);
	status = cairo_scaled_font_text_to_glyphs(cairo_get_scaled_font(cr),
	    cpx, cpy, s, -1, &glyphs, &num_glyphs, NULL, NULL, NULL);
	if (status != CAIRO_STATUS_SUCCESS)
		fatal("cairo_scaled_font_text_to_glyphs failed: %s",
		    cairo_status_to_string(status));

	if (!num_glyphs) {
		cairo_glyph_free(glyphs);
		return;
	}

	g = last = glyphs;
	while (*s) {
		assert(g < glyphs + num_glyphs);
		/* skip UTF-8 multi-characters */
		if ((*s & 0xc0) == 0xc0)
			s++;
		g->x += off_x;
		g->y += off_y;
		if (*s == '~') {
			cairo_show_glyphs(cr, last, g - last);
			last = g + 1;
			if (overlining) {
				overline(cr, ox, oy, g->x, g->y, h);
			} else {
				ox = g->x;
				oy = g->y;
			}
			overlining = !overlining;
			if (s[1]) {
				off_x = g[0].x - g[1].x;
				off_y = g[0].y - g[1].y;
			}
		}
		g++;
		s++;
	}
	assert(g == glyphs + num_glyphs);
	assert(g > glyphs);

	if (last != g)
		cairo_show_glyphs(cr, last, g - last);
	if (overlining) {
		cairo_glyph_extents(cr, g - 1, 1, &ext);
		overline(cr, ox, oy, g[-1].x + ext.x_advance,
		    g[-1].y + ext.y_advance, h);
	}
	
	cairo_glyph_free(glyphs);
}


#define	TEXT_STRETCH	1.3


static void cr_text_cairo(void *ctx, int x, int y, const char *s, unsigned size,
    enum text_align align, int rot, enum text_style style,
    unsigned color, unsigned layer)
{
	struct cro_ctx *cc = ctx;
	cairo_text_extents_t ext;
	cairo_matrix_t m;

	cairo_set_font_size(cc->cr, cd(cc, size) * TEXT_STRETCH);
	cairo_text_extents(cc->cr, s, &ext);

	set_color(cc, color);

	cairo_move_to(cc->cr, cx(cc, x), cy(cc, y));

	cairo_get_matrix(cc->cr, &m);
	cairo_rotate(cc->cr, -rot / 180.0 * M_PI);

	switch (align) {
	case text_min:
		break;
	case text_mid:
		cairo_rel_move_to(cc->cr, -ext.width / 2.0, 0);
		break;
	case text_max:
		cairo_rel_move_to(cc->cr, -ext.width, 0);
		break;
	default:
		BUG("invalid alignment %d", align);
	}

	if (!disable_overline && strchr(s, '~'))
		overlined(cc->cr, s, cd(cc, size) * TEXT_STRETCH);
	else
		cairo_show_text(cc->cr, s);
	cairo_set_matrix(cc->cr, &m);
}


static void cr_text_pango(void *ctx, int x, int y, const char *s, unsigned size,
    enum text_align align, int rot, enum text_style style,
    unsigned color, unsigned layer)
{
	struct cro_ctx *cc = ctx;
	PangoRectangle ink;
	double pango_size = cd(cc, size) * TEXT_STRETCH * PANGO_SCALE;

	if (pango_size != cc->pango_size) {
		pango_font_description_set_absolute_size(cc->pango_desc,
		    pango_size);
		pango_layout_set_font_description(cc->pango_layout,
		    cc->pango_desc);
		cc->pango_size = pango_size;
	}
	pango_layout_set_text(cc->pango_layout, s, -1);
	pango_layout_get_extents(cc->pango_layout, &ink, NULL);

	set_color(cc, color);

	cairo_move_to(cc->cr, cx(cc, x), cy(cc, y));

	cairo_save(cc->cr);
	cairo_rotate(cc->cr, -rot / 180.0 * M_PI);

	switch (align) {
	case text_min:
		cairo_rel_move_to(cc->cr,
		    -ink.x / PANGO_SCALE,
		    -(ink.y + ink.height) / PANGO_SCALE);
		break;
	case text_mid:
		cairo_rel_move_to(cc->cr,
		    -(ink.x + ink.width / 2.0) / PANGO_SCALE,
		    -(ink.y + ink.height) / PANGO_SCALE);
		break;
	case text_max:
		cairo_rel_move_to(cc->cr,
		    -(ink.x + ink.width) / PANGO_SCALE,
		    -(ink.y + + ink.height) / PANGO_SCALE);
		break;
	default:
		BUG("invalid alignment %d", align);
	}

//	pango_cairo_update_layout(cc->cr, cc->pango_layout);
	pango_cairo_show_layout(cc->cr, cc->pango_layout);
	cairo_restore(cc->cr);
}


static void cr_text(void *ctx, int x, int y, const char *s, unsigned size,
    enum text_align align, int rot, enum text_style style,
    unsigned color, unsigned layer)
{
	if (use_pango)
		cr_text_pango(ctx, x, y, s, size, align, rot, style,
		    color, layer);
	else
		cr_text_cairo(ctx, x, y, s, size, align, rot, style,
		    color, layer);
}


static unsigned cr_text_width(void *ctx, const char *s, unsigned size,
    enum text_style style)
{
	struct cro_ctx *cc = ctx;
	cairo_text_extents_t ext;

	cairo_set_font_size(cc->cr, cd(cc, size) * TEXT_STRETCH);
	cairo_text_extents(cc->cr, s, &ext);
	return dc(cc, ext.width);
}


/* ----- Color override ---------------------------------------------------- */


void cro_color_override(struct cro_ctx *cc, int color)
{
	cc->color_override = color;
}


/* ----- Initialization and termination ------------------------------------ */


static const struct gfx_ops real_cro_ops = {
	.name		= "cairo",
	.line		= cr_line,
	.poly		= cr_poly,
	.circ		= cr_circ,
	.arc		= cr_arc,
	.text		= cr_text,
	.text_width	= cr_text_width,
};


static struct cro_ctx *new_cc(void)
{
	struct cro_ctx *cc;

	cc = alloc_type(struct cro_ctx);
	cc->xo = cc->yo = 0;
	cc->scale = cc->default_scale = DEFAULT_SCALE;

	cc->pango_size = 0;

	cc->sheets = NULL;
	cc->n_sheets = 0;

	cc->color_override = COLOR_NONE;

	cc->output_name = NULL;

	cc->add_toc = 1;
	cc->toc = NULL;
	/*
	 * record_init does not perform allocations or such, so it's safe to
	 * call it here even if we don't use this facility.
	 */
	record_init(&cc->record, &real_cro_ops, cc);

	return cc;
}


static void setup_pango(struct cro_ctx *cc)
{
	if (use_pango) {
		cc->pango_desc =
		    pango_font_description_from_string("Helvetica Bold");
		cc->pango_layout = pango_cairo_create_layout(cc->cr);
		// pango_font_description_free(cc->pango_desc);
		// @@@ to destroy pango_layout, g_object_unref(layout);
	} else {
		cairo_select_font_face(cc->cr, "Helvetica",
		    CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	}
}


static bool cr_args(void *ctx, int argc, char *const *argv)
{
	struct cro_ctx *cc = ctx;
	char c;

	while ((c = getopt(argc, argv, "o:s:T")) != EOF)
		switch (c) {
		case 'o':
			cc->output_name = optarg;
			break;
		case 's':
			cc->scale = atof(optarg) * cc->default_scale;
			break;
		case 'T':
			cc->add_toc = 0;
			break;
		default:
			usage(*argv);
		}
	return 1;
}


void cro_get_size(const struct cro_ctx *cc, int *w, int *h, int *x, int *y)
{
	int bw, bh;
	int xmin, ymin;

	record_bbox(&cc->record, &xmin, &ymin, &bw, &bh);

//	fprintf(stderr, "%dx%d%+d%+d\n", bw, bh, xmin, ymin);
	*x = xmin;
	*y = ymin;
	if (w)
		*w = cd(cc, bw);
	if (h)
		*h = cd(cc, bh);
//	fprintf(stderr, "%dx%d%+d%+d\n", *w, *h, xmin, ymin);
}


static void end_common(struct cro_ctx *cc, int *w, int *h, int *x, int *y)
{
	int xmin, ymin;

	cairo_surface_destroy(cc->s);
	cairo_destroy(cc->cr);

	cro_get_size(cc, w, h, &xmin, &ymin);
	cc->xo = -cd(cc, xmin);
	cc->yo = -cd(cc, ymin);

	if (x)
		*x = xmin;
	if (y)
		*y = ymin;
}


static cairo_status_t stream_to_stdout(void *closure,
    const unsigned char *data, unsigned length)
{
	ssize_t wrote;

	wrote = write(1, data, length);
	if (wrote == (ssize_t) length)
		return CAIRO_STATUS_SUCCESS;
	diag_perror("stdout");
	return CAIRO_STATUS_WRITE_ERROR;
}


/* ----- PDF (auto-sizing, using redraw) ----------------------------------- */


static cairo_status_t stream_to_pdftoc(void *closure,
    const unsigned char *data, unsigned length)
{
	struct cro_ctx *cc = closure;

	return pdftoc_write(cc->toc, data, length) ?
	    CAIRO_STATUS_SUCCESS : CAIRO_STATUS_WRITE_ERROR;
}


static void *cr_pdf_init(void)
{
	struct cro_ctx *cc = new_cc();

	cc->scale *= 16;
	cc->default_scale *= 16;

	/* cr_text_width needs *something* to work with */

	cc->s = cairo_pdf_surface_create(NULL, 16, 16);
	cc->cr = cairo_create(cc->s);

	if (cc->add_toc)
		cc->toc = pdftoc_begin();

	return cc;
}


static bool cr_pdf_args(void *ctx, int argc, char *const *argv)
{
	struct cro_ctx *cc = ctx;

	if (!cr_args(cc, argc, argv))
		return 0;
	if (cc->add_toc && cc->output_name)
		return pdftoc_set_file(cc->toc, cc->output_name);
	return 1;
}


static void cr_pdf_sheet_name(void *ctx, const char *name)
{
	struct cro_ctx *cc = ctx;

	if (cc->toc)
		pdftoc_title(cc->toc, name ? name : "???");
}


static void cr_pdf_new_sheet(void *ctx)
{
	struct cro_ctx *cc = ctx;

	cc->n_sheets++;
	cc->sheets = realloc_type_n(cc->sheets, struct record, cc->n_sheets);
	cc->sheets[cc->n_sheets - 1] = cc->record;
	record_wipe(&cc->record);
}


static void cr_pdf_end(void *ctx)
{
	struct cro_ctx *cc = ctx;
	int w, h;
	unsigned i;

	end_common(cc, &w, &h, NULL, NULL);

	w = (w + 15) >> 4;
	h = (h + 15) >> 4;

	if (cc->toc)
		cc->s = cairo_pdf_surface_create_for_stream(stream_to_pdftoc,
		    cc, w, h);
	else if (cc->output_name)
		cc->s = cairo_pdf_surface_create(cc->output_name, w, h);
	else
		cc->s = cairo_pdf_surface_create_for_stream(stream_to_stdout,
		    NULL, w, h);
	cc->cr = cairo_create(cc->s);

	cairo_scale(cc->cr, 1.0 / 16.0, 1.0 / 16);
	setup_pango(cc);
	cairo_set_line_width(cc->cr, 0.5 * cc->scale);
	/* @@@ CAIRO_LINE_CAP_ROUND makes all non-dashed lines disappear */
	cairo_set_line_cap(cc->cr, CAIRO_LINE_CAP_SQUARE);

	for (i = 0; i != cc->n_sheets; i++) {
		set_color(cc, COLOR_WHITE);
		cairo_paint(cc->cr);

		record_replay(cc->sheets + i);
		record_destroy(cc->sheets + i);

		cairo_show_page(cc->cr);
	}

	record_replay(&cc->record);
	record_destroy(&cc->record);

	cairo_show_page(cc->cr);

	cairo_surface_destroy(cc->s);
	cairo_destroy(cc->cr);

	if (cc->toc)
		pdftoc_end(cc->toc);
}


/* ----- PNG (auto-sizing, using redraw) ----------------------------------- */


static void *cr_png_init(void)
{
	struct cro_ctx *cc = new_cc();

	/* cr_text_width needs *something* to work with */

	cc->s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 16, 16);
	cc->cr = cairo_create(cc->s);

	return cc;
}


static void cr_png_end(void *ctx)
{
	struct cro_ctx *cc = ctx;
	int w, h, stride;

	cro_img_end(cc, &w, &h, &stride);
	cro_img_write(cc, cc->output_name);
}


/* ----- Images (auto-sizing, using redraw) -------------------------------- */


uint32_t *cro_img_end(struct cro_ctx *cc, int *w, int *h, int *stride)
{
	uint32_t *data;

	end_common(cc, w, h, NULL, NULL);

	*stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, *w);
	data = alloc_size(*stride * *h);

	cc->s = cairo_image_surface_create_for_data((unsigned char  *) data,
	    CAIRO_FORMAT_RGB24, *w, *h, *stride);
	cc->cr = cairo_create(cc->s);

	set_color(cc, COLOR_WHITE);
	cairo_paint(cc->cr);

	setup_pango(cc);
	cairo_set_line_width(cc->cr, 2);
	cairo_set_line_cap(cc->cr, CAIRO_LINE_CAP_ROUND);

	record_replay(&cc->record);
	record_destroy(&cc->record);

	cairo_surface_flush(cc->s);

	return data;
}


void cro_img_write(struct cro_ctx *cc, const char *name)
{
	if (name)
		cairo_surface_write_to_png(cc->s, name);
	else
		cairo_surface_write_to_png_stream(cc->s, stream_to_stdout,
		    NULL);
}


/* ----- Canvas (using redraw) --------------------------------------------- */


void cro_canvas_end(struct cro_ctx *cc, int *w, int *h, int *xmin, int *ymin)
{
	end_common(cc, w, h, xmin, ymin);
	if (w)
		*w /= cc->scale;
	if (h)
		*h /= cc->scale;
}


void cro_canvas_prepare(cairo_t *cr)
{
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_paint(cr);

	cairo_set_line_width(cr, 2);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
}


void cro_canvas_draw(struct cro_ctx *cc, cairo_t *cr, int xo, int yo,
    float scale)
{
	cc->cr = cr;

	setup_pango(cc);

	cc->scale = scale;
	cc->xo = xo;
	cc->yo = yo;
	record_replay(&cc->record);
}


/* ----- Image for external use (simplified API) --------------------------- */


uint32_t *cro_img(struct cro_ctx *cc, struct cro_ctx *cc_extra,
    int xo, int yo, int w, int h,
    float scale, cairo_t **res_cr, int *res_stride)
{
	int stride;
	uint32_t *data;
	cairo_t *cr;
	cairo_surface_t *s;

	stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, w);
	data = alloc_size(stride * h);

	s = cairo_image_surface_create_for_data((unsigned char *) data,
	    CAIRO_FORMAT_RGB24, w, h, stride);
	cr = cairo_create(s);

	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_paint(cr);

	cairo_set_line_width(cr, 2);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

	cc->cr = cr;
	cc->s = s;
	cc->xo = xo;
	cc->yo = yo;
	cc->scale = scale;
	cc->color_override = COLOR_NONE;

	setup_pango(cc);

	if (cc_extra) {
		cc_extra->record.user = cc->record.user;  /* @@@ eww ! */
		record_replay(&cc_extra->record);
	}
	record_replay(&cc->record);

	if (res_cr)
		*res_cr = cr;
	if (res_stride)
		*res_stride = stride;

	return data;
}


/* @@@ redesign this when we get a bit more serious about cleaning up */

cairo_surface_t *cro_img_surface(struct cro_ctx *cc)
{
	return cc->s;
}


/* ----- Operations -------------------------------------------------------- */


const struct gfx_ops cro_png_ops = {
	.name		= "png",
	.line		= record_line,
	.poly		= record_poly,
	.circ		= record_circ,
	.arc		= record_arc,
	.text		= record_text,
	.text_width	= cr_text_width,
	.init		= cr_png_init,
	.args		= cr_args,
	.end		= cr_png_end,
};

const struct gfx_ops cro_pdf_ops = {
	.name		= "pdf",
	.line		= record_line,
	.poly		= record_poly,
	.circ		= record_circ,
	.arc		= record_arc,
	.text		= record_text,
	.text_width	= cr_text_width,
	.init		= cr_pdf_init,
	.args		= cr_pdf_args,
	.sheet_name	= cr_pdf_sheet_name,
	.new_sheet	= cr_pdf_new_sheet,
	.end		= cr_pdf_end,
};
