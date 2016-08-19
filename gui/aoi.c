/*
 * gui/aoi.c - GUI: areas of interest
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

#include <stddef.h>
#include <math.h>
#include <assert.h>

#include "misc/util.h"
#include "gui/aoi.h"


static const struct aoi *hovering = NULL;


struct aoi *aoi_add(struct aoi **aois, const struct aoi *cfg)
{
	struct aoi *new;

	new = alloc_type(struct aoi);
	*new = *cfg;
	new->next = *aois;
	*aois = new;

	return new;
}


void aoi_update(struct aoi *aoi, const struct aoi *cfg)
{
	struct aoi *next = aoi->next;

	*aoi = *cfg;
	aoi->next = next;
}


static const struct aoi *find_aoi(const struct aoi *aois, int x, int y)
{
	const struct aoi *aoi;

	for (aoi = aois; aoi; aoi = aoi->next)
		if (x >= aoi->x && x < aoi->x + aoi->w &&
		    y >= aoi->y && y < aoi->y + aoi->h)
			break;
	return aoi;
}


bool aoi_hover(const struct aoi *aois, int x, int y)
{
	const struct aoi *aoi;

	if (hovering) {
		if (x >= hovering->x && x < hovering->x + hovering->w &&
		    y >= hovering->y && y < hovering->y + hovering->h)
			return 1;
		hovering->hover(hovering->user, 0);
		hovering = NULL;
	}

	aoi = find_aoi(aois, x, y);
	if (aoi && aoi->hover && aoi->hover(aoi->user, 1)) {
		hovering = aoi;
		return 1;
	}
	return 0;
}


static bool need_dehover(const struct aoi *aois)
{
	const struct aoi *aoi;

	if (!hovering)
		return 0;
	if (hovering->click)
		return 0;
	for (aoi = aois; aoi; aoi = aoi->next)
		if (aoi->related == hovering && aoi->click)
			return 0;
	return 1;
}


bool aoi_click(const struct aoi *aois, int x, int y)
{
	const struct aoi *aoi;

	if (need_dehover(aois))
		aoi_dehover();

	aoi = find_aoi(aois, x, y);
	if (!aoi || !aoi->click)
		return 0;
	aoi->click(aoi->user);
	return 1;
}


void aoi_set_related(struct aoi *aoi, const struct aoi *related)
{
	assert(!aoi->related);
	aoi->related = related;
}

void aoi_remove(struct aoi **aois, const struct aoi *aoi)
{
	if (hovering == aoi) {
		aoi->hover(aoi->user, 0);
		hovering = NULL;
	}
	while (*aois != aoi)
		aois = &(*aois)->next;
	*aois = aoi->next;
	free((void *) aoi);
}


void aoi_dehover(void)
{
	if (hovering)
		hovering->hover(hovering->user, 0);
	hovering = NULL;
}

