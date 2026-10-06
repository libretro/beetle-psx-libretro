/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2020-2021 Paul Cercueil <paul@crapouillou.net>
 */

/* libretro: the reaper is now private to the emulation thread. Workers
 * no longer touch the code LUT, the block cache or the reap list (their
 * results are installed by the emulation thread, see recompiler.c), so
 * there is nothing to lock and nothing to pause. A reap job that cannot
 * run yet - a block still held by a worker - re-adds itself and runs on
 * the next pass instead of waiting. */
#include "blockcache.h"
#include "debug.h"
#include "lightrec-private.h"
#include "memmanager.h"
#include "recompiler.h"
#include "slist.h"
#include "reaper.h"

#include <errno.h>
#include <stdbool.h>

struct reaper_elm {
	reap_func_t func;
	void *data;
	struct slist_elm slist;
};

struct reaper {
	struct lightrec_state *state;
	struct slist_elm reap_list;
};

struct reaper *lightrec_reaper_init(struct lightrec_state *state)
{
	struct reaper *reaper;

	reaper = lightrec_malloc(state, MEM_FOR_LIGHTREC, sizeof(*reaper));
	if (!reaper) {
		pr_err("Cannot create reaper: Out of memory\n");
		return NULL;
	}

	reaper->state = state;
	slist_init(&reaper->reap_list);

	return reaper;
}

void lightrec_reaper_destroy(struct reaper *reaper)
{
	/* Deferred frees re-add themselves while a worker holds the block;
	 * the workers are joined by now, so a pass drains everything. */
	while (!slist_empty(&reaper->reap_list))
		lightrec_reaper_reap(reaper);

	lightrec_free(reaper->state, MEM_FOR_LIGHTREC, sizeof(*reaper), reaper);
}

int lightrec_reaper_add(struct reaper *reaper, reap_func_t f, void *data)
{
	struct reaper_elm *reaper_elm;
	struct slist_elm *elm;

	for (elm = reaper->reap_list.next; elm; elm = elm->next) {
		reaper_elm = container_of(elm, struct reaper_elm, slist);

		if (reaper_elm->data == data)
			return 0;
	}

	reaper_elm = lightrec_malloc(reaper->state, MEM_FOR_LIGHTREC,
				     sizeof(*reaper_elm));
	if (!reaper_elm) {
		pr_err("Cannot add reaper entry: Out of memory\n");
		return -ENOMEM;
	}

	reaper_elm->func = f;
	reaper_elm->data = data;
	slist_append(&reaper->reap_list, &reaper_elm->slist);

	return 0;
}

void lightrec_reaper_reap(struct reaper *reaper)
{
	struct reaper_elm *reaper_elm;
	struct slist_elm *elm, pending;

	/* This function runs on every exit from the execution loop, and
	 * the list is empty the vast majority of the time. */
	if (slist_empty(&reaper->reap_list))
		return;

	/* Take the current list: a job that defers itself lands on the
	 * fresh one and waits for the next pass rather than spinning here. */
	pending.next = reaper->reap_list.next;
	slist_init(&reaper->reap_list);

	while (!!(elm = slist_first(&pending))) {
		slist_remove(&pending, elm);

		reaper_elm = container_of(elm, struct reaper_elm, slist);

		(*reaper_elm->func)(reaper->state, reaper_elm->data);

		lightrec_free(reaper->state, MEM_FOR_LIGHTREC,
			      sizeof(*reaper_elm), reaper_elm);
	}

	/* Hand the code freed above to its owners without waiting for a
	 * batch to fill. */
	if (ENABLE_THREADED_COMPILER && reaper->state->rec)
		lightrec_rec_code_free_flush(reaper->state->rec);
}
