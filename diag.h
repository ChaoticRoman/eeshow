/*
 * diag.h - Diagnostics
 *
 * Written 2016 by Werner Almesberger
 * Copyright 2016 by Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef DIAG_H
#define	DIAG_H

/*
 * 0: no progress indications
 * 1: reasonable progress indications
 * 2: verbose output
 * > 2: go wild !
 */

extern unsigned verbose;


/*
 * Terminate immediately. Further execution makes no sense.
 * E.g., out of memory.
 */

void __attribute__((noreturn)) fatal(const char *fmt, ...);

/*
 * Operation has failed, but the program as a whole may still be able to
 * continue. E.g., a schematics component was not found.
 */

void error(const char *fmt, ...);

/*
 * A minor operation has failed or some other issue was detected. This may
 * be (or lead to) a more serious problem, but does not immediately affect
 * operation.
 */

void warning(const char *fmt, ...);

/*
 * Progress message, used mainly for debugging. "level" is the minimum
 * verbosity level required.
 */

void progress(unsigned level, const char *fmt, ...);

#endif /* !DIAG_H */
