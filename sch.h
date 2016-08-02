/*
 * sch.h - Parse Eeschema .sch file
 *
 * Written 2016 by Werner Almesberger
 * Copyright 2016 by Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#ifndef SCH_H
#define SCH_H

#include <stdbool.h>

#include "dwg.h"
#include "text.h"
#include "lib.h"


enum sch_state {
	sch_basic,	/* basic state */
	sch_descr,	/* prelude and description */
	sch_comp,	/* component */
	sch_sheet,	/* sub-sheet */
	sch_text,	/* text or label */
	sch_wire,	/* wire */
};

struct sch_obj {
	enum sch_obj_type {
		sch_obj_wire,
		sch_obj_junction,
		sch_obj_noconn,
		sch_obj_text,
		sch_obj_comp,
		sch_obj_sheet,
	} type;

	int x, y;

	union {
		struct sch_wire {
			void (*fn)(int sx, int sy, int ex, int ey);
			int ex, ey;
		} wire;
		struct sch_text {
			void (*fn)(int x, int y, const char *s,
			    int dir, int dim, enum dwg_shape shape);
			const char *s;
			int dir;	/* orientation */
			int dim;	/* dimension */
			enum dwg_shape shape;
		} text;
		struct sch_comp {
			const struct comp *comp; /* current component */
			unsigned unit;	/* unit of current component */
			struct comp_field {
				struct text txt;
				struct comp_field *next;
			} *fields;
			int m[6];
		} comp;
		struct sch_sheet {
			unsigned h, w;
			const char *sheet;
			unsigned sheet_dim;
			const char *file;
			unsigned file_dim;
			bool rotated;

			struct sheet_field {
				char *s;
				int x, y;
				unsigned dim;
				enum dwg_shape shape;
				unsigned side;
				struct sheet_field *next;
			} *fields;
		} sheet;
	} u;

	struct sch_obj *next;
};

struct sheet {
	struct sch_obj *objs;
	struct sheet *next;
};

struct sch_ctx {
	enum sch_state state;

	bool recurse;

	struct sch_obj obj;
	struct sch_obj **next_obj;

	struct sheet *sheets;
	struct sheet **next_sheet;

	const struct lib *lib;
};


void decode_alignment(struct text *txt, char hor, char vert);

void sch_render(const struct sheet *sheet);
void sch_parse(struct sch_ctx *ctx, const char *name, const struct lib *lib);
void sch_init(struct sch_ctx *ctx, bool recurse);

#endif /* !SCH_H */
