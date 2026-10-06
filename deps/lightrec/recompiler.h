/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019-2021 Paul Cercueil <paul@crapouillou.net>
 */

#ifndef __LIGHTREC_RECOMPILER_H__
#define __LIGHTREC_RECOMPILER_H__

#include <stdbool.h>
#include <stddef.h>

struct block;
struct lightrec_mem_map;
struct lightrec_state;
struct recompiler;

struct recompiler *lightrec_recompiler_init(struct lightrec_state *state,
					    const struct lightrec_mem_map *codebuf);
/* Stops and joins the workers and drops their unconsumed results. Safe
 * to call more than once; lightrec_free_recompiler() calls it. */
void lightrec_recompiler_stop(struct recompiler *rec);
void lightrec_free_recompiler(struct recompiler *rec);
int lightrec_recompiler_add(struct recompiler *rec, struct block *block);
void lightrec_recompiler_remove(struct recompiler *rec, struct block *block);
/* True while a worker is emitting code for the block. */
bool lightrec_recompiler_block_busy(struct recompiler *rec,
				    const struct block *block);
/* Emulation thread: install the results the workers published. */
void lightrec_recompiler_install(struct recompiler *rec);

void * lightrec_recompiler_run_first_pass(struct lightrec_state *state,
					  struct block *block, u32 *pc);

void lightrec_recompiler_pause(struct recompiler *rec);
void lightrec_recompiler_unpause(struct recompiler *rec);

/* Code buffer arenas. alloc/realloc run on the arena's owner (a worker,
 * or the emulation thread before the first job and after the join);
 * free runs on the emulation thread and is routed to the owner. */
bool lightrec_rec_has_code_buffer(const struct recompiler *rec);
void * lightrec_rec_code_alloc(struct recompiler *rec, unsigned int arena,
			       size_t size);
void lightrec_rec_code_realloc(struct recompiler *rec, unsigned int arena,
			       void *ptr, size_t size);
void lightrec_rec_code_free(struct recompiler *rec, void *ptr);
/* Owner side: free code the arena's own worker gives up. */
void lightrec_rec_code_free_own(struct recompiler *rec, unsigned int arena,
				void *ptr);
void lightrec_rec_code_free_flush(struct recompiler *rec);

#endif /* __LIGHTREC_RECOMPILER_H__ */
