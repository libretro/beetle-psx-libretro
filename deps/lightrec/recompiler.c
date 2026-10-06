/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019-2021 Paul Cercueil <paul@crapouillou.net>
 */

/* libretro: lock-free threaded recompiler.
 *
 * No mutex, no condition variable, and the emulation thread never waits
 * for a worker. The three things the old design serialised under one
 * lock are now three lock-free structures:
 *
 *   - The job queue is a fixed array of slots. The emulation thread is
 *     the only writer of a slot's block pointer and the only side that
 *     turns a FREE slot into a PENDING one or a PENDING one back to FREE
 *     (a cancel). Workers claim with a PENDING -> COMPILING CAS and put
 *     the slot back to FREE once the result is published. A slot in the
 *     COMPILING state is also the hazard pointer: a block is only freed
 *     once no slot holds it, and lightrec_free_block() defers the free
 *     through the reaper instead of waiting when one does.
 *
 *   - Workers only emit machine code. Everything that touches shared
 *     state after a compilation (the code LUT, the block cache, the
 *     reaper) moved to the emulation thread: a worker pushes a result
 *     node onto an MPSC stack and the emulation thread installs it from
 *     the dispatcher loop. The reaper therefore never races a worker and
 *     needs no lock either.
 *
 *   - Code memory comes from one TLSF arena per worker, grown from a
 *     shared pool of chunks taken with a CAS on a bitmask. Frees travel
 *     back to the owning arena in batches through a per-arena stack; a
 *     chunk with nothing live in it goes back to the pool, so memory
 *     freed by one worker's blocks can serve another.
 *
 * These come first, and ARRAY_SIZE is undefined between them and the
 * lightrec headers: retro_miscellaneous.h (via rthreads.h) and
 * lightrec-private.h both define it unguarded, and this order plus the
 * undef keeps lightrec's own definition in force for lightrec code. */
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include <retro_atomic.h>
#include <features/features_cpu.h>
#undef ARRAY_SIZE

#include "blockcache.h"
#include "debug.h"
#include "interpreter.h"
#include "lightrec-private.h"
#include "memmanager.h"
#include "reaper.h"
#include "recompiler.h"
#include "tlsf/tlsf.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Pending compile requests the emulation thread can hold. A request that
 * finds no free slot is dropped; the block is re-requested the next time
 * it is jumped to, exactly as during a code-buffer flush. */
#define REC_SLOTS	128

/* The code buffer is cut into this many chunks; the free set is one
 * 32-bit mask so a run of chunks is taken with a single CAS. */
#define REC_CHUNKS	32

/* Freed code pointers travel to the owning arena this many at a time. */
#define REC_FREE_BATCH	32

enum {
	SLOT_FREE = 0,
	SLOT_PENDING,
	SLOT_COMPILING,
};

struct rec_slot {
	retro_atomic_int_t state;
	retro_atomic_int_t requests;
	/* Written by the emulation thread while the slot is FREE, read by
	 * the worker that claimed it. */
	struct block *block;
};

struct rec_free_batch {
	struct rec_free_batch *next;
	unsigned int n;
	void *ptr[REC_FREE_BATCH];
};

struct rec_arena {
	tlsf_t tlsf;
	void *control;
	/* Batches of code pointers freed by the emulation thread, waiting
	 * for the owner to hand them to its TLSF. */
	retro_atomic_ptr_t inbox;
	/* Emulation-thread private: the batch being filled. */
	struct rec_free_batch *pending;
};

struct recompiler_thd {
	struct lightrec_cstate *cstate;
	unsigned int tid;
	sthread_t *thd;
};

struct recompiler {
	struct lightrec_state *state;
	retro_eventcount_t work;	/* workers sleep: a slot went PENDING */
	retro_atomic_int_t stop;
	retro_atomic_int_t pause;
	retro_atomic_int_t must_flush;
	retro_atomic_ptr_t results;	/* struct lightrec_compiled stack */
	bool joined;

	struct rec_slot slots[REC_SLOTS];

	/* Chunk pool. owner[] is written by the taker before it allocates
	 * from the chunk; the allocation reaches the emulation thread only
	 * through a published result, which orders the write. The other
	 * per-chunk tables are private to the current owner. */
	uint8_t *code_base;
	size_t chunk_size;
	unsigned int nb_chunks;
	retro_atomic_int_t free_mask;
	retro_atomic_int_t owner[REC_CHUNKS];
	pool_t pool[REC_CHUNKS];
	unsigned int run_head[REC_CHUNKS];
	unsigned int run_len[REC_CHUNKS];
	unsigned int live[REC_CHUNKS];

	/* nb_recs is 0 on a single-core host: there is nothing for a worker
	 * to run on while the emulation thread runs, so the second request
	 * for a block compiles it inline with inline_cstate instead. */
	unsigned int nb_recs, nb_arenas;
	struct lightrec_cstate *inline_cstate;
	struct rec_arena *arenas;
	struct recompiler_thd thds[];
};

static unsigned int get_processors_count(void)
{
	/* One call covering every platform the per-OS ifdefs used to. */
	unsigned nb = cpu_features_get_core_amount();

	return nb < 1 ? 1 : nb;
}

/* ------------------------------------------------------------------ */
/* Code arenas                                                         */
/* ------------------------------------------------------------------ */

static unsigned int rec_chunk_of(const struct recompiler *rec, const void *p)
{
	return (unsigned int)(((const uint8_t *)p - rec->code_base) / rec->chunk_size);
}

static int rec_chunk_bits(unsigned int head, unsigned int k)
{
	if (k >= 32)
		return -1;
	return (int)(((1u << k) - 1u) << head);
}

static int rec_take_chunks(struct recompiler *rec, unsigned int k)
{
	int mask, bits;
	unsigned int i, run;

	for (;;) {
		mask = retro_atomic_load_acquire_int(&rec->free_mask);
		run = 0;

		for (i = 0; i < rec->nb_chunks; i++) {
			if (!((unsigned int)mask & (1u << i))) {
				run = 0;
				continue;
			}

			if (++run < k)
				continue;

			bits = rec_chunk_bits(i + 1 - k, k);
			if (retro_atomic_cas_int(&rec->free_mask, mask,
						 (int)((unsigned int)mask & ~(unsigned int)bits)))
				return (int)(i + 1 - k);

			/* Lost the race, rescan from the new mask. */
			break;
		}

		if (i == rec->nb_chunks)
			return -1;
	}
}

static void rec_release_chunks(struct recompiler *rec, unsigned int head,
			       unsigned int k)
{
	retro_atomic_fetch_or_int(&rec->free_mask, rec_chunk_bits(head, k));
}

static void rec_arena_drop_empty_run(struct recompiler *rec, unsigned int idx,
				     unsigned int head);

/* Owner side: hand the pointers the emulation thread freed to the TLSF. */
static void rec_arena_free_now(struct recompiler *rec, unsigned int idx,
			       void *ptr)
{
	struct rec_arena *arena = &rec->arenas[idx];
	unsigned int c = rec->run_head[rec_chunk_of(rec, ptr)];

	tlsf_free(arena->tlsf, ptr);

	/* Nothing of ours left in this run: give it back so any arena can
	 * take it. */
	rec->live[c]--;
	rec_arena_drop_empty_run(rec, idx, c);
}

/* Owner side, for code the owner itself gives up (a failed compile). */
void lightrec_rec_code_free_own(struct recompiler *rec, unsigned int idx,
				void *ptr)
{
	if (ptr)
		rec_arena_free_now(rec, idx, ptr);
}

static void rec_arena_drain(struct recompiler *rec, unsigned int idx)
{
	struct rec_arena *arena = &rec->arenas[idx];
	struct rec_free_batch *batch, *next;
	unsigned int i;

	batch = (struct rec_free_batch *)retro_atomic_exchange_ptr(&arena->inbox, NULL);

	for (; batch; batch = next) {
		next = batch->next;

		for (i = 0; i < batch->n; i++)
			rec_arena_free_now(rec, idx, batch->ptr[i]);

		lightrec_free(rec->state, MEM_FOR_LIGHTREC, sizeof(*batch), batch);
	}
}

/* Returns the head chunk of the run added to the arena, or -1. */
static int rec_arena_grow(struct recompiler *rec, unsigned int idx,
			  size_t size)
{
	struct rec_arena *arena = &rec->arenas[idx];
	size_t need = size + tlsf_pool_overhead() + tlsf_alloc_overhead()
		+ tlsf_block_size_min() + 2 * tlsf_align_size();
	unsigned int k = (unsigned int)((need + rec->chunk_size - 1) / rec->chunk_size);
	unsigned int i;
	int head;

	if (k == 0 || k > rec->nb_chunks)
		return -1;

	head = rec_take_chunks(rec, k);
	if (head < 0)
		return -1;

	for (i = 0; i < k; i++) {
		retro_atomic_store_relaxed_int(&rec->owner[head + i], (int)idx);
		rec->run_head[head + i] = (unsigned int)head;
	}

	rec->run_len[head] = k;
	rec->live[head] = 0;
	rec->pool[head] = tlsf_add_pool(arena->tlsf,
					rec->code_base + (size_t)head * rec->chunk_size,
					k * rec->chunk_size);
	if (!rec->pool[head]) {
		rec_release_chunks(rec, (unsigned int)head, k);
		return -1;
	}

	return head;
}

static void rec_arena_drop_empty_run(struct recompiler *rec, unsigned int idx,
				     unsigned int head)
{
	if (rec->live[head])
		return;

	tlsf_remove_pool(rec->arenas[idx].tlsf, rec->pool[head]);
	rec_release_chunks(rec, head, rec->run_len[head]);
}

void * lightrec_rec_code_alloc(struct recompiler *rec, unsigned int idx,
			       size_t size)
{
	struct rec_arena *arena = &rec->arenas[idx];
	void *ptr;

	rec_arena_drain(rec, idx);

	ptr = tlsf_malloc(arena->tlsf, size);
	if (!ptr) {
		int head = rec_arena_grow(rec, idx, size);

		if (head < 0)
			return NULL;

		ptr = tlsf_malloc(arena->tlsf, size);
		if (!ptr) {
			rec_arena_drop_empty_run(rec, idx, (unsigned int)head);
			return NULL;
		}
	}

	rec->live[rec->run_head[rec_chunk_of(rec, ptr)]]++;

	return ptr;
}

void lightrec_rec_code_realloc(struct recompiler *rec, unsigned int idx,
			       void *ptr, size_t size)
{
	/* Shrink in place; the pointer and its chunk do not change. */
	tlsf_realloc(rec->arenas[idx].tlsf, ptr, size);
}

/* Emulation-thread side. */
static void rec_publish_pending(struct recompiler *rec, unsigned int idx)
{
	struct rec_arena *arena = &rec->arenas[idx];
	struct rec_free_batch *batch = arena->pending;
	void *head;

	if (!batch)
		return;

	arena->pending = NULL;

	do {
		head = retro_atomic_load_acquire_ptr(&arena->inbox);
		batch->next = (struct rec_free_batch *)head;
	} while (!retro_atomic_cas_ptr(&arena->inbox, head, batch));
}

void lightrec_rec_code_free(struct recompiler *rec, void *ptr)
{
	struct rec_arena *arena;
	struct rec_free_batch *batch;
	unsigned int idx;

	if (!ptr)
		return;

	idx = (unsigned int)retro_atomic_load_relaxed_int(
			&rec->owner[rec_chunk_of(rec, ptr)]);
	arena = &rec->arenas[idx];

	if (rec->joined || !rec->nb_recs) {
		/* No worker owns it: the arena is ours. */
		rec_arena_drain(rec, idx);
		rec_arena_free_now(rec, idx, ptr);
		return;
	}

	batch = arena->pending;
	if (!batch) {
		batch = lightrec_malloc(rec->state, MEM_FOR_LIGHTREC, sizeof(*batch));
		if (!batch) {
			/* Leak the code block rather than corrupt the arena;
			 * the chunk is reclaimed once its other occupants go. */
			pr_err("Cannot queue code free: Out of memory\n");
			return;
		}
		batch->n = 0;
		arena->pending = batch;
	}

	batch->ptr[batch->n++] = ptr;

	if (batch->n == REC_FREE_BATCH)
		rec_publish_pending(rec, idx);
}

bool lightrec_rec_has_code_buffer(const struct recompiler *rec)
{
	return rec->arenas != NULL;
}

void lightrec_rec_code_free_flush(struct recompiler *rec)
{
	unsigned int i;

	for (i = 0; i < rec->nb_arenas; i++)
		rec_publish_pending(rec, i);
}

static bool rec_arenas_init(struct recompiler *rec,
			    const struct lightrec_mem_map *codebuf)
{
	size_t chunk, align = tlsf_align_size();
	unsigned int i;

	rec->arenas = lightrec_calloc(rec->state, MEM_FOR_LIGHTREC,
				      rec->nb_arenas * sizeof(*rec->arenas));
	if (!rec->arenas)
		return false;

	rec->code_base = codebuf->address;
	chunk = codebuf->length / REC_CHUNKS;
	chunk -= chunk % align;
	rec->chunk_size = chunk;
	rec->nb_chunks = chunk ? REC_CHUNKS : 0;
	retro_atomic_int_init(&rec->free_mask,
			      rec->nb_chunks == 32 ? -1 : (int)((1u << rec->nb_chunks) - 1u));

	for (i = 0; i < REC_CHUNKS; i++)
		retro_atomic_int_init(&rec->owner[i], 0);

	for (i = 0; i < rec->nb_arenas; i++) {
		struct rec_arena *arena = &rec->arenas[i];

		arena->control = lightrec_malloc(rec->state, MEM_FOR_LIGHTREC,
						 tlsf_size());
		if (!arena->control)
			return false;

		arena->tlsf = tlsf_create(arena->control);
		if (!arena->tlsf)
			return false;

		retro_atomic_ptr_init(&arena->inbox, NULL);
		arena->pending = NULL;
	}

	return true;
}

static void rec_arenas_free(struct recompiler *rec)
{
	struct rec_free_batch *batch, *next;
	unsigned int i;

	if (!rec->arenas)
		return;

	for (i = 0; i < rec->nb_arenas; i++) {
		struct rec_arena *arena = &rec->arenas[i];

		batch = (struct rec_free_batch *)retro_atomic_exchange_ptr(&arena->inbox, NULL);
		for (; batch; batch = next) {
			next = batch->next;
			lightrec_free(rec->state, MEM_FOR_LIGHTREC, sizeof(*batch), batch);
		}

		if (arena->pending)
			lightrec_free(rec->state, MEM_FOR_LIGHTREC,
				      sizeof(*arena->pending), arena->pending);

		if (arena->tlsf)
			tlsf_destroy(arena->tlsf);
		if (arena->control)
			lightrec_free(rec->state, MEM_FOR_LIGHTREC,
				      tlsf_size(), arena->control);
	}

	lightrec_free(rec->state, MEM_FOR_LIGHTREC,
		      rec->nb_arenas * sizeof(*rec->arenas), rec->arenas);
	rec->arenas = NULL;
}

/* ------------------------------------------------------------------ */
/* Results: worker -> emulation thread                                 */
/* ------------------------------------------------------------------ */

static void rec_push_result(struct recompiler *rec,
			    struct lightrec_compiled *c)
{
	void *head;

	do {
		head = retro_atomic_load_acquire_ptr(&rec->results);
		c->next = (struct lightrec_compiled *)head;
	} while (!retro_atomic_cas_ptr(&rec->results, head, c));
}

static void rec_cancel_all(struct recompiler *rec)
{
	unsigned int i;

	for (i = 0; i < REC_SLOTS; i++)
		retro_atomic_cas_int(&rec->slots[i].state, SLOT_PENDING, SLOT_FREE);
}

static void lightrec_flush_code_buffer(struct lightrec_state *state, void *d)
{
	struct recompiler *rec = d;

	lightrec_remove_outdated_blocks(state->block_cache, NULL);
	retro_atomic_store_release_int(&rec->must_flush, 0);
}

/* Emulation thread: install every result the workers have published. */
void lightrec_recompiler_install(struct recompiler *rec)
{
	struct lightrec_compiled *c, *next, *list = NULL;

	/* Called on every LUT miss: a plain load first, the locked exchange
	 * only when a worker has actually published something. */
	if (!retro_atomic_load_acquire_ptr(&rec->results))
		return;

	c = (struct lightrec_compiled *)retro_atomic_exchange_ptr(&rec->results, NULL);
	if (!c)
		return;

	/* The stack is newest-first; restore submission order. */
	for (; c; c = next) {
		next = c->next;
		c->next = list;
		list = c;
	}

	for (c = list; c; c = next) {
		next = c->next;

		if (!c->block) {
			/* Code buffer full: drop the queue and let the reaper
			 * free what is outdated. Requests arriving before the
			 * flush are refused by lightrec_recompiler_add(). */
			rec_cancel_all(rec);
			lightrec_reaper_add(rec->state->reaper,
					    lightrec_flush_code_buffer, rec);
		} else {
			lightrec_install_block(rec->state, c->block, c);
		}

		lightrec_free(rec->state, MEM_FOR_LIGHTREC,
			      sizeof(*c) + c->nb_targets * sizeof(c->targets[0]), c);
	}
}

/* ------------------------------------------------------------------ */
/* Workers                                                             */
/* ------------------------------------------------------------------ */

static struct rec_slot *rec_claim_best(struct recompiler *rec)
{
	struct rec_slot *slot, *best;
	int requests, best_requests;
	unsigned int i;

	for (;;) {
		best = NULL;
		best_requests = 0;

		for (i = 0; i < REC_SLOTS; i++) {
			slot = &rec->slots[i];

			if (retro_atomic_load_relaxed_int(&slot->state) != SLOT_PENDING)
				continue;

			requests = retro_atomic_load_relaxed_int(&slot->requests);
			if (!best || requests > best_requests) {
				best = slot;
				best_requests = requests;
			}
		}

		if (!best)
			return NULL;

		if (retro_atomic_cas_int(&best->state, SLOT_PENDING, SLOT_COMPILING))
			return best;

		/* Another worker took it, or it was cancelled: look again. */
	}
}

static void lightrec_recompiler_thd(void *d)
{
	struct recompiler_thd *thd = d;
	struct recompiler *rec = container_of(thd, struct recompiler, thds[thd->tid]);
	struct lightrec_compiled *c;
	struct rec_slot *slot;
	struct block *block;
	int key, ret;

	for (;;) {
		key = retro_eventcount_prepare_wait(&rec->work);

		if (retro_atomic_load_acquire_int(&rec->stop)) {
			retro_eventcount_cancel_wait(&rec->work);
			break;
		}

		slot = NULL;
		if (!retro_atomic_load_acquire_int(&rec->pause))
			slot = rec_claim_best(rec);

		if (!slot) {
			retro_eventcount_commit_wait(&rec->work, key);
			continue;
		}

		retro_eventcount_cancel_wait(&rec->work);

		block = slot->block;
		c = NULL;

		if (likely(!block_has_flag(block, BLOCK_IS_DEAD))) {
			ret = lightrec_compile_block_code(thd->cstate, block, &c);
			if (ret == -ENOMEM) {
				/* Code buffer is full. Ask the emulation thread
				 * to flush it, once. */
				if (!retro_atomic_exchange_int(&rec->must_flush, 1)) {
					c = lightrec_malloc(rec->state, MEM_FOR_LIGHTREC,
							    sizeof(*c));
					if (c)
						memset(c, 0, sizeof(*c));
					else
						retro_atomic_store_release_int(&rec->must_flush, 0);
				}
			} else if (ret) {
				pr_err("Unable to compile block at "PC_FMT": %d\n",
				       block->pc, ret);
			}
		}

		/* Publish before releasing the slot: a FREE slot promises the
		 * emulation thread that any result for this block is already
		 * on the stack. */
		if (c)
			rec_push_result(rec, c);

		retro_atomic_store_release_int(&slot->state, SLOT_FREE);
	}
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

struct recompiler *lightrec_recompiler_init(struct lightrec_state *state,
					    const struct lightrec_mem_map *codebuf)
{
	struct recompiler *rec;
	unsigned int i, nb_recs, nb_cpus;

	nb_cpus = get_processors_count();
	nb_recs = nb_cpus < 2 ? 0 : nb_cpus - 1;
#ifdef LIGHTREC_TEST_WORKERS
	/* Regression lane: force a worker count regardless of the host. */
	nb_recs = LIGHTREC_TEST_WORKERS;
#endif

	rec = lightrec_calloc(state, MEM_FOR_LIGHTREC, sizeof(*rec)
			      + nb_recs * sizeof(*rec->thds));
	if (!rec) {
		pr_err("Cannot create recompiler: Out of memory\n");
		return NULL;
	}

	rec->state = state;
	rec->nb_recs = nb_recs;
	rec->nb_arenas = nb_recs ? nb_recs : 1;
	rec->joined = true;
	retro_atomic_int_init(&rec->stop, 0);
	retro_atomic_int_init(&rec->pause, 0);
	retro_atomic_int_init(&rec->must_flush, 0);
	retro_atomic_ptr_init(&rec->results, NULL);

	for (i = 0; i < REC_SLOTS; i++) {
		retro_atomic_int_init(&rec->slots[i].state, SLOT_FREE);
		retro_atomic_int_init(&rec->slots[i].requests, 0);
		rec->slots[i].block = NULL;
	}

	for (i = 0; i < nb_recs; i++) {
		rec->thds[i].tid = i;
		rec->thds[i].cstate = NULL;
		rec->thds[i].thd = NULL;
	}

	if (!retro_eventcount_init(&rec->work)) {
		pr_err("Cannot init eventcount\n");
		goto err_free_rec;
	}

	if (codebuf && codebuf->address && !rec_arenas_init(rec, codebuf)) {
		pr_err("Cannot create recompiler: Out of memory\n");
		goto err_free_arenas;
	}

	for (i = 0; i < nb_recs; i++) {
		rec->thds[i].cstate = lightrec_create_cstate(state);
		if (!rec->thds[i].cstate) {
			pr_err("Cannot create recompiler: Out of memory\n");
			goto err_free_cstates;
		}
		rec->thds[i].cstate->arena = i;
	}

	if (!nb_recs) {
		rec->inline_cstate = lightrec_create_cstate(state);
		if (!rec->inline_cstate) {
			pr_err("Cannot create recompiler: Out of memory\n");
			goto err_free_cstates;
		}
		rec->inline_cstate->arena = 0;
	}

	rec->joined = false;

	for (i = 0; i < nb_recs; i++) {
		rec->thds[i].thd = sthread_create(lightrec_recompiler_thd,
						  &rec->thds[i]);
		if (!rec->thds[i].thd) {
			pr_err("Cannot create recompiler thread\n");
			goto err_join;
		}
	}

	if (nb_recs)
		pr_info("Threaded recompiler started with %u workers.\n", nb_recs);
	else
		pr_info("Single core: blocks compile inline on their second request.\n");

	return rec;

err_join:
	retro_atomic_store_release_int(&rec->stop, 1);
	retro_eventcount_notify(&rec->work);
	for (i = 0; i < nb_recs; i++) {
		if (rec->thds[i].thd)
			sthread_join(rec->thds[i].thd);
	}
	rec->joined = true;
err_free_cstates:
	if (rec->inline_cstate)
		lightrec_free_cstate(rec->inline_cstate);
	for (i = 0; i < nb_recs; i++) {
		if (rec->thds[i].cstate)
			lightrec_free_cstate(rec->thds[i].cstate);
	}
err_free_arenas:
	rec_arenas_free(rec);
	retro_eventcount_free(&rec->work);
err_free_rec:
	lightrec_free(state, MEM_FOR_LIGHTREC,
		      sizeof(*rec) + nb_recs * sizeof(*rec->thds), rec);
	return NULL;
}

void lightrec_recompiler_stop(struct recompiler *rec)
{
	struct lightrec_compiled *c, *next;
	unsigned int i;

	if (rec->joined)
		return;

	retro_atomic_store_release_int(&rec->stop, 1);
	rec_cancel_all(rec);
	retro_eventcount_notify(&rec->work);

	for (i = 0; i < rec->nb_recs; i++)
		sthread_join(rec->thds[i].thd);

	rec->joined = true;

	/* Whatever finished after the last install is not wanted. */
	c = (struct lightrec_compiled *)retro_atomic_exchange_ptr(&rec->results, NULL);
	for (; c; c = next) {
		next = c->next;
		if (c->block)
			lightrec_discard_compiled(rec->state, c->block, c);
		lightrec_free(rec->state, MEM_FOR_LIGHTREC,
			      sizeof(*c) + c->nb_targets * sizeof(c->targets[0]), c);
	}
}

void lightrec_free_recompiler(struct recompiler *rec)
{
	unsigned int i;

	lightrec_recompiler_stop(rec);

	for (i = 0; i < rec->nb_recs; i++)
		lightrec_free_cstate(rec->thds[i].cstate);
	if (rec->inline_cstate)
		lightrec_free_cstate(rec->inline_cstate);

	rec_arenas_free(rec);
	retro_eventcount_free(&rec->work);
	lightrec_free(rec->state, MEM_FOR_LIGHTREC,
		      sizeof(*rec) + rec->nb_recs * sizeof(*rec->thds), rec);
}

/* ------------------------------------------------------------------ */
/* Emulation-thread API                                                */
/* ------------------------------------------------------------------ */

/* Single-core host: a block requested again while still queued is
 * compiled right here, on the emulation thread. The first request
 * stays queued so the interpreter's first pass still tags the block. */
static void rec_compile_inline(struct recompiler *rec, struct rec_slot *slot,
			       struct block *block)
{
	struct lightrec_compiled *c;
	int ret;

	if (retro_atomic_load_acquire_int(&rec->pause))
		return;

	if (!retro_atomic_cas_int(&slot->state, SLOT_PENDING, SLOT_FREE))
		return;

	ret = lightrec_compile_block_code(rec->inline_cstate, block, &c);
	if (ret == -ENOMEM) {
		/* Code buffer is full: same recovery as a worker's, run
		 * directly since this is the thread that reaps. */
		if (!retro_atomic_exchange_int(&rec->must_flush, 1)) {
			rec_cancel_all(rec);
			lightrec_reaper_add(rec->state->reaper,
					    lightrec_flush_code_buffer, rec);
		}
		return;
	}

	if (ret) {
		pr_err("Unable to compile block at "PC_FMT": %d\n",
		       block->pc, ret);
		return;
	}

	lightrec_install_block(rec->state, block, c);
	lightrec_free(rec->state, MEM_FOR_LIGHTREC,
		      sizeof(*c) + c->nb_targets * sizeof(c->targets[0]), c);
}

int lightrec_recompiler_add(struct recompiler *rec, struct block *block)
{
	struct rec_slot *slot, *free_slot = NULL;
	u32 pc1, pc2;
	unsigned int i;
	int state;

	/* If the recompiler must flush the code cache, we can't add the new
	 * job. It will be re-added next time the block's address is jumped to
	 * again. */
	if (retro_atomic_load_acquire_int(&rec->must_flush))
		return 0;

	/* If the block is marked as dead, don't compile it, it will be removed
	 * as soon as it's safe. */
	if (block_has_flag(block, BLOCK_IS_DEAD))
		return 0;

	for (i = 0; i < REC_SLOTS; i++) {
		slot = &rec->slots[i];
		state = retro_atomic_load_acquire_int(&slot->state);

		if (state == SLOT_FREE) {
			if (!free_slot)
				free_slot = slot;
			continue;
		}

		if (slot->block == block) {
			/* The block to compile is already in the queue -
			 * increment its counter to increase its priority */
			retro_atomic_fetch_add_int(&slot->requests, 1);

			if (!rec->nb_recs) {
				/* On single-core CPUs a worker would only
				 * run while we sleep, and the block would be
				 * interpreted until then. Compile it now. */
				rec_compile_inline(rec, slot, block);
			}
			return 0;
		}

		pc1 = kunseg(slot->block->pc);
		pc2 = kunseg(block->pc);
		if (pc2 >= pc1 && pc2 < pc1 + slot->block->nb_ops * 4) {
			/* The block we want to compile is already covered by
			 * another one in the queue - increment its counter to
			 * increase its priority */
			retro_atomic_fetch_add_int(&slot->requests, 1);

			/* Single-core: nobody else will compile the covering
			 * block, and the dispatcher keeps asking for this one
			 * until it or the covering block is installed. */
			if (!rec->nb_recs)
				rec_compile_inline(rec, slot, slot->block);
			return 0;
		}
	}

	/* No slot holds the block, so a worker that just finished it has
	 * already published its result (the result is pushed before the slot
	 * is released): install it now, and the check below sees the new
	 * function instead of queueing the block a second time. */
	lightrec_recompiler_install(rec);

	/* By the time this function was called, the block has been recompiled
	 * and ins't in the wait list anymore. Just return here. */
	if (block->function && !block_has_flag(block, BLOCK_SHOULD_RECOMPILE))
		return 0;

	if (block_has_flag(block, BLOCK_IS_DEAD)
	    || retro_atomic_load_acquire_int(&rec->must_flush))
		return 0;

	if (!free_slot)
		return 0;

	pr_debug("Adding block "PC_FMT" to recompiler\n", block->pc);

	free_slot->block = block;
	retro_atomic_store_relaxed_int(&free_slot->requests, 1);
	retro_atomic_store_release_int(&free_slot->state, SLOT_PENDING);

	retro_eventcount_notify(&rec->work);

	return 0;
}

void lightrec_recompiler_remove(struct recompiler *rec, struct block *block)
{
	struct rec_slot *slot;
	unsigned int i;

	for (i = 0; i < REC_SLOTS; i++) {
		slot = &rec->slots[i];

		if (retro_atomic_load_acquire_int(&slot->state) == SLOT_PENDING
		    && slot->block == block)
			retro_atomic_cas_int(&slot->state, SLOT_PENDING, SLOT_FREE);
	}
}

bool lightrec_recompiler_block_busy(struct recompiler *rec,
				    const struct block *block)
{
	struct rec_slot *slot;
	unsigned int i;

	if (rec->joined)
		return false;

	for (i = 0; i < REC_SLOTS; i++) {
		slot = &rec->slots[i];

		if (retro_atomic_load_acquire_int(&slot->state) == SLOT_COMPILING
		    && slot->block == block)
			return true;
	}

	return false;
}

void * lightrec_recompiler_run_first_pass(struct lightrec_state *state,
					  struct block *block, u32 *pc)
{
	u8 old_flags;

	/* There's no point in running the first pass if the block will never
	 * be compiled. Let the main loop run the interpreter instead. */
	if (block_has_flag(block, BLOCK_NEVER_COMPILE))
		return NULL;

	/* The block is marked as dead, and will be removed the next time the
	 * reaper is run. In the meantime, the old function can still be
	 * executed. */
	if (block_has_flag(block, BLOCK_IS_DEAD))
		return block->function;

	/* If the block is already fully tagged, there is no point in running
	 * the first pass. Request a recompilation of the block, and maybe the
	 * interpreter will run the block in the meantime. */
	if (block_has_flag(block, BLOCK_FULLY_TAGGED))
		lightrec_recompiler_add(state->rec, block);

	if (likely(block->function)) {
		if (block_has_flag(block, BLOCK_FULLY_TAGGED)) {
			old_flags = block_set_flags(block, BLOCK_NO_OPCODE_LIST);

			if (!(old_flags & BLOCK_NO_OPCODE_LIST)) {
				pr_debug("Block "PC_FMT" is fully tagged"
					 " - free opcode list\n", block->pc);

				/* The block was already compiled but the opcode list
				 * didn't get freed yet - do it now */
				lightrec_free_opcode_list(state, block->opcode_list);
			}
		}

		return block->function;
	}

	/* Mark the opcode list as freed, so that the threaded compiler won't
	 * free it while we're using it in the interpreter. */
	old_flags = block_set_flags(block, BLOCK_NO_OPCODE_LIST);

	/* Block wasn't compiled yet - run the interpreter */
	*pc = lightrec_emulate_block(state, block, *pc);

	if (!(old_flags & BLOCK_NO_OPCODE_LIST))
		block_clear_flags(block, BLOCK_NO_OPCODE_LIST);

	/* The block got compiled while the interpreter was running.
	 * We can free the opcode list now. */
	if (block->function && block_has_flag(block, BLOCK_FULLY_TAGGED)) {
		old_flags = block_set_flags(block, BLOCK_NO_OPCODE_LIST);

		if (!(old_flags & BLOCK_NO_OPCODE_LIST)) {
			pr_debug("Block "PC_FMT" is fully tagged"
				 " - free opcode list\n", block->pc);

			lightrec_free_opcode_list(state, block->opcode_list);
		}
	}

	return NULL;
}

void lightrec_recompiler_pause(struct recompiler *rec)
{
	retro_atomic_store_release_int(&rec->pause, 1);
	rec_cancel_all(rec);
}

void lightrec_recompiler_unpause(struct recompiler *rec)
{
	lightrec_rec_code_free_flush(rec);
	retro_atomic_store_release_int(&rec->pause, 0);
	retro_eventcount_notify(&rec->work);
}
