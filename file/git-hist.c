/*
 * file/git-hist.c - Retrieve revision history from GIT repo
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
#include <time.h>	/* for vcs_long_for_pango */
#include <alloca.h>

#include <git2.h>

#include "misc/util.h"
#include "misc/diag.h"
#include "file/git-util.h"
#include "file/git-file.h"
#include "file/git-hist.h"


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
	down->newer = realloc_type_n(down->newer, struct hist *,
	    down->n_newer + 1);
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

	n = git_commit_parentcount(h->commit);
	if (verbose > 2)
		progress(3, "commit %p: %u + %u", h->commit, n_branches, n);

	b = alloca(sizeof(struct hist) * (n_branches - 1 + n));
	n_branches--;
	memcpy(b, branches, sizeof(struct hist *) * n_branches);

	h->older = alloc_type_n(struct hist *, n);
	h->n_older = n;

	for (i = 0; i != n; i++) {
		git_commit *commit;
		struct hist *found = NULL;

		if (git_commit_parent(&commit, h->commit, i))
			pfatal_git("git_commit_parent");
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


bool vcs_git_try(const char *path)
{
	git_repository *repo;

	git_init_once();

	if (git_repository_open_ext(&repo, path,
	    GIT_REPOSITORY_OPEN_CROSS_FS, NULL))
		return 0;
	return !git_repository_is_empty(repo);
}


struct hist *vcs_git_hist(const char *path)
{
	struct hist *head, *dirty;
	git_repository *repo;
	git_oid oid;

	head = new_commit(0);

	git_init_once();

	if (git_repository_open_ext(&repo, path,
	    GIT_REPOSITORY_OPEN_CROSS_FS, NULL))
		pfatal_git(path);

	if (git_reference_name_to_id(&oid, repo, "HEAD"))
		pfatal_git(git_repository_path(repo));

	if (git_commit_lookup(&head->commit, repo, &oid))
		pfatal_git(git_repository_path(repo));

	recurse(head, 1, &head);

	if (!git_repo_is_dirty(repo))
		return head;

	dirty = new_commit(0);
	dirty->older = alloc_type(struct hist *);
	dirty->older[0] = head;
	dirty->n_older = 1;
	uplink(head, dirty);

	return dirty;
}


char *vcs_git_get_rev(struct hist *h)
{
	const git_oid *oid = git_commit_id(h->commit);
	char *s = alloc_size(GIT_OID_HEXSZ + 1);

	return git_oid_tostr(s, GIT_OID_HEXSZ + 1, oid);
}


const char *vcs_git_summary(struct hist *h)
{
	const char *summary;

	if (!h->commit)
		return "Uncommitted changes";
	summary = git_commit_summary(h->commit);
	if (summary)
		return summary;

	pfatal_git("git_commit_summary");
}


/*
 * @@@ This one is a bit inconvenient. It depends both on the information the
 * VCS provides, some of which is fairly generic, but some may not be, and
 * the very specific constraints imposed by the markup format of Pango. 
 */

char *vcs_git_long_for_pango(struct hist *h,
    char *(*formatter)(const char *fmt, ...))
{
	git_buf buf = { 0 };
	time_t commit_time;
	const git_signature *sig;
	char *s;

	if (!h->commit)
		return stralloc("Uncommitted changes");
	if (git_object_short_id(&buf, (git_object *) h->commit))
		goto fail;
	commit_time = git_commit_time(h->commit);
	sig = git_commit_committer(h->commit);
	s = formatter("<b>%s</b> %s%s &lt;%s&gt;<small>\n%s</small>",
	    buf.ptr, ctime(&commit_time), sig->name, sig->email,
	    git_commit_summary(h->commit));
	git_buf_free(&buf);
	return s;

fail:
	pfatal_git("vcs_git_long_for_pango");
}


void hist_iterate(struct hist *h,
    void (*fn)(void *user, struct hist *h), void *user)
{
	unsigned i;

	fn(user, h);
	for (i = 0; i != h->n_older; i++)
		if (h->older[i]->newer[h->older[i]->n_newer - 1] == h)
			hist_iterate(h->older[i], fn, user);
}


static void dump_one(void *user, struct hist *h)
{
	git_buf buf = { 0 };

	if (h->commit) {
		if (git_object_short_id(&buf, (git_object *) h->commit))
			pfatal_git("git_object_short_id");
		printf("%*s%s  %s\n",
		    2 * h->branch, "", buf.ptr, vcs_git_summary(h));
		git_buf_free(&buf);
	} else {
		printf("dirty\n");
	}
}

	
void dump_hist(struct hist *h)
{
	hist_iterate(h, dump_one, NULL);
}
