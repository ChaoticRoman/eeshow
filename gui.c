/*
 * gui.c - GUI for eeshow
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
 */

#define	_GNU_SOURCE	/* for asprintf */
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <cairo/cairo.h>
#include <gtk/gtk.h>

#include "util.h"
#include "cro.h"
#include "gfx.h"
#include "git-hist.h"
#include "sch.h"
#include "gui-aoi.h"
#include "gui-over.h"
#include "gui.h"


struct gui_ctx;

struct gui_sheet {
	const const struct sheet *sch;
	struct cro_ctx *gfx_ctx;

	int w, h;
	int xmin, ymin;

	struct aoi *aois;	/* areas of interest; in schematics coord  */

	struct gui_sheet *next;
};

struct gui_hist {
	struct hist *hist;
	struct gui_hist *next;
};

struct gui_ctx {
	GtkWidget *da;

	int curr_x;		/* last on-screen mouse position */
	int curr_y;

	unsigned zoom;		/* scale by 1.0 / (1 << zoom) */
	int x, y;		/* center, in eeschema coordinates */

	bool panning;
	int pan_x, pan_y;

	struct gui_hist *hist;	/* revision history; NULL if none */
	struct hist *vcs_hist;	/* underlying VCS data; NULL if none */

	struct overlay *sheet_overlays;
	struct overlay *vcs_overlays;
	struct aoi *aois;	/* areas of interest; in canvas coord  */

	struct gui_sheet *curr_sheet;
				/* current sheet */
	struct gui_sheet *sheets;
};


/* ----- Helper functions -------------------------------------------------- */


static void redraw(const struct gui_ctx *ctx)
{
	gtk_widget_queue_draw(ctx->da);
}


/* ----- Rendering --------------------------------------------------------- */


static void draw_vcs_overlays(const struct gui_ctx *ctx, cairo_t *cr)
{
	struct overlay *over;
	int x = 200;
	int y = 5;

	for (over = ctx->vcs_overlays; over;)
		over = overlay_draw(over, cr, &x, &y);
}


static gboolean on_draw_event(GtkWidget *widget, cairo_t *cr,
    gpointer user_data)
{
	const struct gui_ctx *ctx = user_data;
	const struct gui_sheet *sheet = ctx->curr_sheet;
	GtkAllocation alloc;

	float f = 1.0 / (1 << ctx->zoom);
	int x, y;

	gtk_widget_get_allocation(ctx->da, &alloc);
	x = -(sheet->xmin + ctx->x) * f + alloc.width / 2;
	y = -(sheet->ymin + ctx->y) * f + alloc.height / 2;
	cro_canvas_draw(sheet->gfx_ctx, cr, x, y, f);

	overlay_draw_all(ctx->sheet_overlays, cr);
	draw_vcs_overlays(ctx, cr);

	return FALSE;
}


static void render(struct gui_ctx *ctx, struct gui_sheet *sheet)
{
	char *argv[] = { "gui", NULL };

	gfx_init(&cro_canvas_ops, 1, argv);
	sch_render(sheet->sch);
	cro_canvas_end(gfx_ctx,
	    &sheet->w, &sheet->h, &sheet->xmin, &sheet->ymin);
	sheet->gfx_ctx = gfx_ctx;

	ctx->x = sheet->w >> 1;
	ctx->y = sheet->h >> 1;
	// gfx_end();
}


/* ----- Tools ------------------------------------------------------------- */


static void canvas_coord(const struct gui_ctx *ctx,
    int ex, int ey, int *x, int *y)
{
	GtkAllocation alloc;
	int sx, sy;

	gtk_widget_get_allocation(ctx->da, &alloc);
	sx = ex - alloc.width / 2;
	sy = ey - alloc.height / 2;
	*x = (sx << ctx->zoom) + ctx->x;
	*y = (sy << ctx->zoom) + ctx->y;
}


/* ----- Panning ----------------------------------------------------------- */


static void pan_begin(struct gui_ctx *ctx, int x, int y)
{
	if (ctx->panning)
		return;
	ctx->panning = 1;
	ctx->pan_x = x;
	ctx->pan_y = y;
}


static void pan_update(struct gui_ctx *ctx, int x, int y)
{
	if (!ctx->panning)
		return;

	ctx->x -= (x - ctx->pan_x) << ctx->zoom;
	ctx->y -= (y - ctx->pan_y) << ctx->zoom;
	ctx->pan_x = x;
	ctx->pan_y = y;

	redraw(ctx);
}


static void pan_end(struct gui_ctx *ctx, int x, int y)
{
	pan_update(ctx, x, y);
	ctx->panning = 0;
}


/* ----- Zoom -------------------------------------------------------------- */



static void zoom_in(struct gui_ctx *ctx, int x, int y)
{
	if (ctx->zoom == 0)
		return;
	ctx->zoom--;
	ctx->x = (ctx->x + x) / 2;
	ctx->y = (ctx->y + y) / 2;
	redraw(ctx);
}


static void zoom_out(struct gui_ctx *ctx, int x, int y)
{
	if (ctx->curr_sheet->w >> ctx->zoom <= 16)
		return;
	ctx->zoom++;
	ctx->x = 2 * ctx->x - x;
	ctx->y = 2 * ctx->y - y;
	redraw(ctx);
}


static void zoom_to_extents(struct gui_ctx *ctx)
{
	const struct gui_sheet *sheet = ctx->curr_sheet;
	GtkAllocation alloc;

	ctx->x = sheet->w / 2;
	ctx->y = sheet->h / 2;

	gtk_widget_get_allocation(ctx->da, &alloc);
	ctx->zoom = 0;
	while (sheet->w >> ctx->zoom > alloc.width ||
	    sheet->h >> ctx->zoom > alloc.height)
		ctx->zoom++;

	redraw(ctx);
}


/* ----- Revision history -------------------------------------------------- */


static void hide_history(void *user)
{
	struct gui_ctx *ctx = user;

	overlay_remove_all(&ctx->vcs_overlays);
	redraw(ctx);
}


static void show_history(struct gui_ctx *ctx)
{
	struct gui_hist *h = ctx->hist;
	char *s;

	overlay_remove_all(&ctx->vcs_overlays);
	for (h = ctx->hist; h; h = h->next) {
		// @@@ \n doesn't work with cairo_show_text :-(
		if (asprintf(&s, "commit\n%s", vcs_git_summary(h->hist))) {}
		overlay_add(&ctx->vcs_overlays, s, &ctx->aois,
		    NULL, hide_history, ctx);
	}
	redraw(ctx);
}


static void show_history_cb(void *user)
{
	struct gui_ctx *ctx = user;

	show_history(ctx);
}


/* ----- Navigate sheets --------------------------------------------------- */


static bool go_up_sheet(struct gui_ctx *ctx);


static void close_subsheet(void *user)
{
	struct gui_ctx *ctx = user;

	go_up_sheet(ctx);
}


static void go_to_sheet(struct gui_ctx *ctx, struct gui_sheet *sheet)
{
	ctx->curr_sheet = sheet;
	overlay_remove_all(&ctx->sheet_overlays);
	if (ctx->hist) {
		char *s;

		if (asprintf(&s, "%.40s", vcs_git_summary(ctx->hist->hist))) {}
		overlay_add(&ctx->sheet_overlays, s, &ctx->aois,
		    NULL, show_history_cb, ctx);
	}
	if (sheet->sch->title)
		overlay_add(&ctx->sheet_overlays, sheet->sch->title,
		    &ctx->aois, NULL, close_subsheet, ctx);
	zoom_to_extents(ctx);
}


static bool go_up_sheet(struct gui_ctx *ctx)
{
	struct gui_sheet *sheet;
	const struct sch_obj *obj;

	for (sheet = ctx->sheets; sheet; sheet = sheet->next)
		for (obj = sheet->sch->objs; obj; obj = obj->next)
			if (obj->type == sch_obj_sheet &&
			    obj->u.sheet.sheet == ctx->curr_sheet->sch) {
				go_to_sheet(ctx, sheet);
				return 1;
			}
	return 0;
}


static bool go_prev_sheet(struct gui_ctx *ctx)
{
	struct gui_sheet *sheet;

	for (sheet = ctx->sheets; sheet; sheet = sheet->next)
		if (sheet->next && sheet->next == ctx->curr_sheet) {
			go_to_sheet(ctx, sheet);
			return 1;
		}
	return 0;
}


static bool go_next_sheet(struct gui_ctx *ctx)
{
	if (!ctx->curr_sheet->next)
		return 0;
	go_to_sheet(ctx, ctx->curr_sheet->next);
	return 1;
}


/* ----- Event handlers ---------------------------------------------------- */


static gboolean motion_notify_event(GtkWidget *widget, GdkEventMotion *event,
    gpointer data)
{
	struct gui_ctx *ctx = data;
	const struct gui_sheet *curr_sheet = ctx->curr_sheet;
	int x, y;

	ctx->curr_x = event->x;
	ctx->curr_y = event->y;

	canvas_coord(ctx, event->x, event->y, &x, &y);

	aoi_hover(curr_sheet->aois, x + curr_sheet->xmin, y + curr_sheet->ymin);
	pan_update(ctx, event->x, event->y);

	return TRUE;
}


static gboolean button_press_event(GtkWidget *widget, GdkEventButton *event,
    gpointer data)
{
	struct gui_ctx *ctx = data;
	const struct gui_sheet *curr_sheet = ctx->curr_sheet;
	int x, y;

	canvas_coord(ctx, event->x, event->y, &x, &y);

	switch (event->button) {
	case 1:
		if (aoi_click(ctx->aois, event->x, event->y))
			break;
		aoi_click(curr_sheet->aois,
		    x + curr_sheet->xmin, y + curr_sheet->ymin);
		break;
	case 2:
		pan_begin(ctx, event->x, event->y);
		break;
	case 3:
		break;
	}
	return TRUE;
}


static gboolean button_release_event(GtkWidget *widget, GdkEventButton *event,
    gpointer data)
{
	struct gui_ctx *ctx = data;
	int x, y;

	canvas_coord(ctx, event->x, event->y, &x, &y);

	switch (event->button) {
	case 1:
		break;
	case 2:
		pan_end(ctx, event->x, event->y);
		break;
	case 3:
		break;
	}
	return TRUE;
}


static gboolean key_press_event(GtkWidget *widget, GdkEventKey *event,
    gpointer data)
{
	struct gui_ctx *ctx = data;
	struct gui_sheet *sheet = ctx->curr_sheet;
	int x, y;

	canvas_coord(ctx, ctx->curr_x, ctx->curr_y, &x, &y);

	switch (event->keyval) {
	case '+':
	case '=':
		zoom_in(ctx, x, y);
		break;
	case '-':
		zoom_out(ctx, x, y);
		break;
	case '*':
		zoom_to_extents(ctx);
		break;
	case GDK_KEY_Home:
		if (sheet != ctx->sheets)
			go_to_sheet(ctx, ctx->sheets);
		break;
	case GDK_KEY_BackSpace:
	case GDK_KEY_Delete:
		go_up_sheet(ctx);
		break;
	case GDK_KEY_Page_Up:
	case GDK_KEY_KP_Page_Up:
		go_prev_sheet(ctx);
		break;
	case GDK_KEY_Page_Down:
	case GDK_KEY_KP_Page_Down:
		go_next_sheet(ctx);
		break;
	case GDK_KEY_Up:
	case GDK_KEY_KP_Up:
	case GDK_KEY_Down:
	case GDK_KEY_KP_Down:
		show_history(ctx);
		break;
	case GDK_KEY_q:
		gtk_main_quit();
	}
	return TRUE;
}


static gboolean scroll_event(GtkWidget *widget, GdkEventScroll *event,
    gpointer data)
{
	struct gui_ctx *ctx = data;
	int x, y;

	canvas_coord(ctx, event->x, event->y, &x, &y);
	switch (event->direction) {
	case GDK_SCROLL_UP:
		zoom_in(ctx, x, y);
		break;
	case GDK_SCROLL_DOWN:
		zoom_out(ctx, x, y);
		break;
	default:
		/* ignore */;
	}
	return TRUE;
}


static void size_allocate_event(GtkWidget *widget, GdkRectangle *allocation,
    gpointer data)
{
	struct gui_ctx *ctx = data;

	zoom_to_extents(ctx);
}


/* ----- AoI callbacks ----------------------------------------------------- */


struct sheet_aoi_ctx {
	struct gui_ctx *gui_ctx;
	const struct sch_obj *obj;
};


static void select_subsheet(void *user)
{
	const struct sheet_aoi_ctx *aoi_ctx = user;
	struct gui_ctx *ctx = aoi_ctx->gui_ctx;
	const struct sch_obj *obj = aoi_ctx->obj;
	struct gui_sheet *sheet;

	for (sheet = ctx->sheets; sheet; sheet = sheet->next)
		if (sheet->sch == obj->u.sheet.sheet) {
			go_to_sheet(ctx, sheet);
			return;
		}
	abort();
}


/* ----- Initialization ---------------------------------------------------- */


static void add_sheet_aoi(struct gui_ctx *ctx, struct gui_sheet *parent,
    const struct sch_obj *obj)
{
	struct sheet_aoi_ctx *aoi_ctx = alloc_type(struct sheet_aoi_ctx);

	aoi_ctx->gui_ctx = ctx;
	aoi_ctx->obj = obj;

	struct aoi aoi = {
		.x	= obj->x,
		.y	= obj->y,
		.w	= obj->u.sheet.w,
		.h	= obj->u.sheet.h,
		.click	= select_subsheet,
		.user	= aoi_ctx,
	};

	aoi_add(&parent->aois, &aoi);
}


static void mark_aois(struct gui_ctx *ctx, struct gui_sheet *sheet)
{
	const struct sch_obj *obj;

	sheet->aois = NULL;
	for (obj = sheet->sch->objs; obj; obj = obj->next)
		switch (obj->type) {
		case sch_obj_sheet:
			add_sheet_aoi(ctx, sheet, obj);
			break;
		default:
			break;
		}
}


static void get_sheets(struct gui_ctx *ctx, const struct sheet *sheets)
{
	const struct sheet *sheet;
	struct gui_sheet **next = &ctx->sheets;
	struct gui_sheet *gui_sheet;

	for (sheet = sheets; sheet; sheet = sheet->next) {
		gui_sheet = alloc_type(struct gui_sheet);
		gui_sheet->sch = sheet;

		render(ctx, gui_sheet);
		mark_aois(ctx, gui_sheet);

		*next = gui_sheet;
		next = &gui_sheet->next;
	}
	*next = NULL;
	ctx->curr_sheet = ctx->sheets;
}



static void add_hist(void *user, struct hist *h)
{
	struct gui_ctx *ctx = user;
	struct gui_hist **anchor;

	for (anchor = &ctx->hist; *anchor; anchor = &(*anchor)->next);
	*anchor = alloc_type(struct gui_hist);
	(*anchor)->hist = h;
	(*anchor)->next = NULL;
}


static void get_git(struct gui_ctx *ctx, const char *sch_name)
{
	if (!vcs_git_try(sch_name))
		return;
	ctx->vcs_hist = vcs_git_hist(sch_name);
	hist_iterate(ctx->vcs_hist, add_hist, ctx);
}


static struct sheet *parse_sheets(int n_args, char **args, bool recurse)
{
	struct lib lib;
	struct sch_ctx sch_ctx;
	struct file sch_file;
	int i;

	sch_init(&sch_ctx, recurse);
	file_open(&sch_file, args[n_args - 1], NULL);

	lib_init(&lib);
	for (i = 0; i != n_args - 1; i++)
		lib_parse(&lib, args[i], &sch_file);
	sch_parse(&sch_ctx, &sch_file, &lib);
	file_close(&sch_file);

	return sch_ctx.sheets;
}


int gui(unsigned n_args, char **args, bool recurse)
{
	GtkWidget *window;
	struct gui_ctx ctx = {
		.zoom		= 4,	/* scale by 1 / 16 */
		.panning	= 0,
		.hist		= NULL,
		.vcs_hist	= NULL,
		.sheet_overlays	= NULL,
		.vcs_overlays	= NULL,
		.aois		= NULL,
	};

	get_sheets(&ctx, parse_sheets(n_args, args, recurse));
	get_git(&ctx, args[n_args - 1]);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	ctx.da = gtk_drawing_area_new();
	gtk_container_add(GTK_CONTAINER(window), ctx.da);

	g_signal_connect(G_OBJECT(ctx.da), "draw",
	    G_CALLBACK(on_draw_event), &ctx);
	g_signal_connect(G_OBJECT(ctx.da), "motion_notify_event",
	    G_CALLBACK(motion_notify_event), &ctx);
	g_signal_connect(G_OBJECT(ctx.da), "button_press_event",
	    G_CALLBACK(button_press_event), &ctx);
	g_signal_connect(G_OBJECT(ctx.da), "button_release_event",
	    G_CALLBACK(button_release_event), &ctx);
	g_signal_connect(G_OBJECT(ctx.da), "scroll_event",
	    G_CALLBACK(scroll_event), &ctx);
	g_signal_connect(G_OBJECT(ctx.da), "key_press_event",
	    G_CALLBACK(key_press_event), &ctx);
	g_signal_connect(G_OBJECT(ctx.da), "size_allocate",
	    G_CALLBACK(size_allocate_event), &ctx);

	g_signal_connect(window, "destroy",
	    G_CALLBACK(gtk_main_quit), NULL);

	gtk_widget_set_can_focus(ctx.da, TRUE);

	gtk_widget_set_events(ctx.da,
	    GDK_EXPOSE | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK |
	    GDK_KEY_PRESS_MASK |
	    GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
	    GDK_SCROLL_MASK |
	    GDK_POINTER_MOTION_MASK);

//	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
//	gtk_window_set_default_size(GTK_WINDOW(window), 400, 90);
	gtk_window_set_title(GTK_WINDOW(window), "eeshow");

	gtk_widget_show_all(window);
	go_to_sheet(&ctx, ctx.sheets);

	gtk_main();

	return 0;
}
