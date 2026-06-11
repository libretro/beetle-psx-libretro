// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * Copyright (C) 2015-2021 Paul Cercueil <paul@crapouillou.net>
 */

#include "blockcache.h"
#include "debug.h"
#include "lightrec-private.h"
#include "memmanager.h"
#include "reaper.h"
#include "recompiler.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Must be power of two */
#define LUT_SIZE 0x4000

struct blockcache {
	struct lightrec_state *state;
	u32 max_block_ops;
	struct block * lut[LUT_SIZE];
};

u16 lightrec_get_lut_entry(const struct block *block)
{
	return (kunseg(block->pc) >> 2) & (LUT_SIZE - 1);
}

struct block * lightrec_find_block(struct blockcache *cache, u32 pc)
{
	struct block *block;

	pc = kunseg(pc);

	for (block = cache->lut[(pc >> 2) & (LUT_SIZE - 1)];
	     block; block = block->next)
		if (kunseg(block->pc) == pc)
			return block;

	return NULL;
}

struct block * lightrec_find_block_from_lut(struct blockcache *cache,
					    u16 lut_entry, u32 addr_in_block)
{
	struct block *block;
	u32 pc;

	addr_in_block = kunseg(addr_in_block);

	for (block = cache->lut[lut_entry]; block; block = block->next) {
		pc = kunseg(block->pc);
		if (addr_in_block >= pc &&
		    addr_in_block < pc + (block->nb_ops << 2))
			return block;
	}

	return NULL;
}

void remove_from_code_lut(struct blockcache *cache, struct block *block)
{
	struct lightrec_state *state = cache->state;
	u32 offset = lut_offset(block->pc);

	if (block->function) {
		memset(lut_address(state, offset), 0,
		       block->nb_ops * lut_elm_size(state));
	}
}

static void lightrec_account_block_pages(struct blockcache *cache,
					 const struct block *block, s32 inc)
{
	u32 start, end, page;

	/* Only RAM (and its mirrors) can host self-modified code blocks
	 * that we track here; skip BIOS and other maps. */
	if (kunseg(block->pc) & ~0x7FFFFF)
		return;

	start = (kunseg(block->pc) & 0x1FFFFF) >> CODE_PAGE_SHIFT;
	end = ((kunseg(block->pc) & 0x1FFFFF)
	       + (block->nb_ops << 2) - 1) >> CODE_PAGE_SHIFT;

	for (page = start; page <= end && page < CODE_NB_PAGES; page++)
		cache->state->nb_blocks_in_page[page] += inc;

	if (inc > 0)
		lightrec_mark_block_pages(cache->state, block);
}

/* Set the per-word bits over the range of this block, so that stores
 * into it will trigger a re-validation walk. Must be called whenever the
 * block is (re-)installed in the code LUT. */
void lightrec_mark_block_pages(struct lightrec_state *state,
			       const struct block *block)
{
	u32 start, end, w;

	if (kunseg(block->pc) & ~0x7FFFFF)
		return;

	start = ((kunseg(block->pc) & 0x1FFFFF) >> 2);
	end = start + block->nb_ops;

	for (w = start; w < end; w++)
		state->code_walk_map[w >> 5] |= BIT(w & 0x1f);
}

void lightrec_register_block(struct blockcache *cache, struct block *block)
{
	lightrec_account_block_pages(cache, block, 1);
	u32 pc = kunseg(block->pc);

	/* High-water mark of the longest block ever registered; bounds the
	 * lookback of the range walk in
	 * lightrec_invalidate_blocks_in_range(). */
	if (block->nb_ops > cache->max_block_ops)
		cache->max_block_ops = block->nb_ops;
	struct block *old;

	old = cache->lut[(pc >> 2) & (LUT_SIZE - 1)];
	if (old)
		block->next = old;

	cache->lut[(pc >> 2) & (LUT_SIZE - 1)] = block;

	remove_from_code_lut(cache, block);
}

void lightrec_unregister_block(struct blockcache *cache, struct block *block)
{
	lightrec_account_block_pages(cache, block, -1);
	u32 pc = kunseg(block->pc);
	struct block *old = cache->lut[(pc >> 2) & (LUT_SIZE - 1)];

	if (old == block) {
		cache->lut[(pc >> 2) & (LUT_SIZE - 1)] = old->next;
		return;
	}

	for (; old; old = old->next) {
		if (old->next == block) {
			old->next = block->next;
			return;
		}
	}

	pr_err("Block at "PC_FMT" is not in cache\n", block->pc);
}

static bool lightrec_block_is_old(const struct lightrec_state *state,
				  const struct block *block)
{
	u32 diff = state->current_cycle - block->precompile_date;

	return diff > (1 << 27); /* About 4 seconds */
}

static void lightrec_free_blocks(struct blockcache *cache,
				 const struct block *except, bool all)
{
	struct lightrec_state *state = cache->state;
	struct block *block, *next;
	bool outdated = all;
	unsigned int i;
	u8 old_flags;

	for (i = 0; i < LUT_SIZE; i++) {
		for (block = cache->lut[i]; block; block = next) {
			next = block->next;

			if (except && block == except)
				continue;

			if (!all) {
				outdated = lightrec_block_is_old(state, block) ||
					lightrec_block_is_outdated(state, block);
			}

			if (!outdated)
				continue;

			old_flags = block_set_flags(block, BLOCK_IS_DEAD);

			if (!(old_flags & BLOCK_IS_DEAD)) {
				if (ENABLE_THREADED_COMPILER)
					lightrec_recompiler_remove(state->rec, block);

				pr_debug("Freeing outdated block at "PC_FMT"\n", block->pc);
				remove_from_code_lut(cache, block);
				lightrec_unregister_block(cache, block);
				lightrec_free_block(state, block);
			}
		}
	}
}

void lightrec_invalidate_blocks_in_range(struct blockcache *cache,
					 u32 start, u32 len)
{
	struct block *block;
	struct lightrec_state *state = cache->state;
	u32 end = start + len;
	u32 block_start, block_end, page, first_page, last_page, w;
	bool has_code = false;
	unsigned int i;

	start &= 0x1FFFFF;
	end = (end - 1) & 0x1FFFFF;

	first_page = start >> CODE_PAGE_SHIFT;
	last_page = end >> CODE_PAGE_SHIFT;

	for (page = first_page;
	     page <= last_page && page < CODE_NB_PAGES; page++)
		has_code |= cache->state->nb_blocks_in_page[page] != 0;

	/* No cached block overlaps the written pages. This is the common
	 * case for data writes; clear only the per-word bits covering the
	 * written range itself (bits can be left set by freed blocks, and
	 * would otherwise keep tripping the emitted store check) and skip
	 * the page expansion, the code LUT clear and the block walk. */
	if (!has_code) {
		for (w = (start >> 2) & ~31u; w <= (end >> 2); w += 32)
			state->code_walk_map[w >> 5] = 0;
		return;
	}

	/* Clear the code LUT entries of the written words, so that block
	 * heads inside the range stop dispatching into stale code. (Moved
	 * from lightrec_invalidate(); only needed when code is present.) */
	memset(lut_address(state, lut_offset(start)), 0,
	       (((end - start) >> 2) + 1) * lut_elm_size(state));

	/* Expand to whole pages: every block head overlapping the window
	 * is cleared below, so the per-word bits of the whole window can
	 * be cleared as well. This makes bulk overwrites of cached code
	 * (e.g. an executable load) take this path once per page instead
	 * of once per word. */
	start = first_page << CODE_PAGE_SHIFT;
	end = ((last_page + 1) << CODE_PAGE_SHIFT);

	for (w = start >> 2; w < (end >> 2); w += 32)
		state->code_walk_map[w >> 5] = 0;

	/* A block whose entry point precedes the written range will not be
	 * caught by clearing the LUT entries of the written words; its
	 * staleness check only looks at the entry-point LUT slot. Clear the
	 * code LUT over the whole range of every block overlapping the
	 * window, so that the next fetch re-validates the block against
	 * the current code in memory. Clearing the full range (and not
	 * just the entry slot) also drops the inner entry points that
	 * compilation may have installed for covered blocks; those would
	 * otherwise keep dispatching straight into the stale code.
	 *
	 * The hash of the block cache is address-local ((pc >> 2) masked),
	 * so every block whose start address lies in
	 * [start - longest_block, end] has its bucket inside a contiguous
	 * (modulo wrap-around) index range; blocks starting earlier cannot
	 * reach the written range. Walking that bucket range instead of
	 * the whole table makes invalidation cost proportional to the
	 * written size, not to the number of cached blocks. */
	{
		u32 lookback = cache->max_block_ops << 2;
		u32 wstart = start - lookback;
		u32 nb_buckets = ((end - wstart) >> 2) + 1;
		u32 base = (wstart >> 2) & (LUT_SIZE - 1);
		u32 j;

		if (nb_buckets > LUT_SIZE)
			nb_buckets = LUT_SIZE;

		for (j = 0; j < nb_buckets; j++) {
			i = (base + j) & (LUT_SIZE - 1);

			for (block = cache->lut[i]; block; block = block->next) {
				block_start = kunseg(block->pc) & 0x1FFFFF;
				block_end = block_start + (block->nb_ops << 2);

				if (block_start < end && start < block_end)
					memset(lut_address(state,
							   lut_offset(block->pc)), 0,
					       block->nb_ops * lut_elm_size(state));
			}
		}
	}
}

void lightrec_remove_outdated_blocks(struct blockcache *cache,
				     const struct block *except)
{
	pr_info("Running out of code space. Cleaning block cache...\n");

	lightrec_free_blocks(cache, except, false);
}

void lightrec_free_all_blocks(struct blockcache *cache)
{
	lightrec_free_blocks(cache, NULL, true);
}

void lightrec_free_block_cache(struct blockcache *cache)
{
	lightrec_free_all_blocks(cache);
	lightrec_free(cache->state, MEM_FOR_LIGHTREC, sizeof(*cache), cache);
}

struct blockcache * lightrec_blockcache_init(struct lightrec_state *state)
{
	struct blockcache *cache;

	cache = lightrec_calloc(state, MEM_FOR_LIGHTREC, sizeof(*cache));
	if (!cache)
		return NULL;

	cache->state = state;

	return cache;
}

u32 lightrec_calculate_block_hash(const struct block *block)
{
	const u32 *code = block->code;
	u32 hash = 0xffffffff;
	unsigned int i;

	/* Jenkins one-at-a-time hash algorithm */
	for (i = 0; i < block->nb_ops; i++) {
		hash += *code++;
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	return hash;
}

static void lightrec_reset_lut_offset(struct lightrec_state *state, void *d)
{
	u32 pc = (u32)(uintptr_t) d;
	struct block *block;
	void *addr;

	block = lightrec_find_block(state->block_cache, pc);
	if (!block || block_has_flag(block, BLOCK_IS_DEAD))
		return;

	/* This callback runs some time after the staleness check that
	 * scheduled it; the code may have been overwritten in between.
	 * Re-validate before reinstalling - blindly installing the
	 * compiled function here can resurrect stale code, and leaving
	 * the entry empty forces a full re-validation through the C
	 * path on every subsequent dispatch of the block. */
	if (block->hash != lightrec_calculate_block_hash(block))
		return;

	addr = block->function ?: state->get_next_block;
	lut_write(state, lut_offset(pc), addr);
	lightrec_mark_block_pages(state, block);
}

bool lightrec_block_is_outdated(struct lightrec_state *state, struct block *block)
{
	u32 offset = lut_offset(block->pc);
	bool outdated;

	if (lut_read(state, offset))
		return false;

	outdated = block->hash != lightrec_calculate_block_hash(block);
	if (likely(!outdated)) {
		/* The block was marked as outdated, but the content is still
		 * the same */

		if (ENABLE_THREADED_COMPILER) {
			/*
			 * When compiling a block that covers ours, the threaded
			 * compiler will set the LUT entries of the various
			 * entry points. Therefore we cannot write the LUT here,
			 * as we would risk overwriting the new entry points.
			 * Leave it to the reaper to re-install the LUT entries.
			 */

			lightrec_reaper_add(state->reaper,
					    lightrec_reset_lut_offset,
					    (void *)(uintptr_t) block->pc);
		} else if (block->function) {
			lut_write(state, offset, block->function);
			lightrec_mark_block_pages(state, block);
		} else {
			lut_write(state, offset, state->get_next_block);
			lightrec_mark_block_pages(state, block);
		}
	}

	return outdated;
}
