/*
 * git-hist.c - Retrieve revision history from GIT repo
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
#include <stdlib.h>
#include <stdio.h>
#include <alloca.h>

#include "util.h"
#include "main.h"
#include "git-file.h"
#include "git-hist.h"


/*
 * @@@ we assume to have a single head. That isn't necessarily true, since
 * each open branch has its own head. Getting this right is for further study.
 */


static struct hist *new_commit(unsigned branch)
{
	struct hist *h;

	h = alloc_type(struct hist);
	h->commit = NULL;
	h->branch = branch;
	h->newer = NULL;
	h->n_newer = 0;
	h->older = NULL;
	h->n_older = 0;
	return h;
}


static void uplink(struct hist *down, struct hist *up)
{
	down->newer = realloc(down->newer,
	    sizeof(struct hist *) * (down->n_newer + 1));
	if (!down->newer) {
		perror("realloc");
		exit(1);
	}
	down->newer[down->n_newer++] = up;
}


static struct hist *find_commit(struct hist *h, const git_commit *commit)
{
	unsigned i;
	struct hist *found;

	/*
	 * @@@ should probably use
	 * git_oid_equal(git_object_id(a), git_object_id(b))
	 */
	if (h->commit == commit)
		return h;
	for (i = 0; i != h->n_older; i++) {
		if (h->older[i]->newer[0] != h)
			continue;
		found = find_commit(h->older[i], commit);
		if (found)
			return found;
	}
	return NULL;
}


static void recurse(struct hist *h,
    unsigned n_branches, struct hist **branches)
{
	unsigned n, i, j;
	struct hist **b;
	const git_error *e;

	n = git_commit_parentcount(h->commit);
	if (verbose > 2)
		fprintf(stderr, "commit %p: %u + %u\n",
		    h->commit, n_branches, n);

	b = alloca(sizeof(struct hist) * (n_branches - 1 + n));
	n_branches--;
	memcpy(b, branches, sizeof(struct hist *) * n_branches);

	h->older = alloc_size(sizeof(struct hist *) * n);
	h->n_older = n;

	for (i = 0; i != n; i++) {
		git_commit *commit;
		struct hist *found = NULL;

		if (git_commit_parent(&commit, h->commit, i)) {
			e = giterr_last();
			fprintf(stderr, "git_commit_parent: %s\n", e->message);
			exit(1);
		}
		for (j = 0; j != n_branches; j++) {
			found = find_commit(b[j], commit);
			if (found)
				break;
		}
		if (found) {
			uplink(found, h);
			h->older[i] = found;
		} else {
			struct hist *new;

			new = new_commit(n_branches);
			new->commit = commit;
			h->older[i] = new;
			b[n_branches++] = new;
			uplink(new, h);
			recurse(new, n_branches, b);
		}
	}
}


struct hist *vcs_git_hist(const char *path)
{
	struct hist *head;
	git_repository *repo;
	git_oid oid;
	const git_error *e;

	head = new_commit(0);

	vcs_git_init();

	if (git_repository_open_ext(&repo, path,
	    GIT_REPOSITORY_OPEN_CROSS_FS, NULL)) {
		e = giterr_last();
		fprintf(stderr, "%s: %s\n", path, e->message);
		exit(1);
	}

	if (git_reference_name_to_id(&oid, repo, "HEAD")) {
		e = giterr_last();
		fprintf(stderr, "%s: %s\n",
		    git_repository_path(repo), e->message);
		exit(1);
	}

	if (git_commit_lookup(&head->commit, repo, &oid)) {
		e = giterr_last();
		fprintf(stderr, "%s: %s\n",
		    git_repository_path(repo), e->message);
		exit(1);
	}

	recurse(head, 1, &head);
	return head;
}


const char *vcs_git_summary(struct hist *h)
{
	const char *summary;
	const git_error *e;

	summary = git_commit_summary(h->commit);
	if (summary)
		return summary;

	e = giterr_last();
	fprintf(stderr, "git_commit_summary: %s\n", e->message);
	exit(1);
}


void dump_hist(struct hist *h)
{
	git_buf buf = { 0 };
	const git_error *e;
	unsigned i;

	if (git_object_short_id(&buf, (git_object *) h->commit)) {
		e = giterr_last();
		fprintf(stderr, "git_object_short_id: %s\n", e->message);
		exit(1);
	}
	printf("%*s%s  %s\n", 2 * h->branch, "", buf.ptr, vcs_git_summary(h));
	git_buf_free(&buf);

	for (i = 0; i != h->n_older; i++)
		if (h->older[i]->newer[h->older[i]->n_newer - 1] == h)
			dump_hist(h->older[i]);
}
