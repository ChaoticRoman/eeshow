/*
 * file.h - Open and read a file
 *
 * Written 2016 by Werner Almesberger
 * Copyright 2016 by Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef FILE_H
#define	FILE_H

#include <stdbool.h>
#include <stdio.h>


struct file {
	FILE *file;		/* NULL if using a version control system */
	void *vcs;		/* VCS descriptor or NULL */
	const char *name;	/* name/designator given to file_open */
	unsigned lineno;
	const struct file *related; /* NULL if not related to anything */
};


bool file_cat(const struct file *file, void *user, const char *line);

char *file_graft_relative(const char *base, const char *name);

void file_open(struct file *file, const char *name,
    const struct file *related);
void file_read(struct file *file,
    bool (*parse)(const struct file *file, void *user, const char *line),
    void *user);
void file_close(struct file *file);

#endif /* !FILE_H */
