#include <string.h>

#include "pgxp_mem.h"
#include <stdlib.h>
#include "pgxp_state.h"
#include "pgxp_cpu.h"
#include "pgxp_gte.h"
#include "pgxp_value.h"

/* The PGXP shadow of PSX memory.  Previously a single
 * `Mem[3 * 2048 * 1024 / 4]` array of PGXP_value (42 MB) carved into
 * three equal 524288-entry slots, of which only:
 *   - the 524288-entry User RAM slot was actually filled,
 *   - the second slot needed 256 entries (1 KB scratch / 4 bytes),
 *   - the third slot needed 2048 entries (8 KB hardware regs / 4 bytes).
 * That left ~27 MB of unused PGXP_value in the middle and tail of
 * the buffer.  Splitting into three right-sized arrays drops the
 * static BSS from 42 MB to about 14.4 MB without changing the
 * external (u32) contract of PGXP_ConvertAddress.  The offset
 * constants are kept so callers that print or compare with
 * InvalidAddress continue to work, but the offsets are now packed
 * back-to-back instead of on 524288-entry strides. */
#define USER_MEM_COUNT		(2048 * 1024 / 4)	/* 524288 entries: 2 MB / 4 */
#define SCRATCH_MEM_COUNT	(1024 / 4)		/* 256 entries: 1 KB / 4 */
#define REGISTER_MEM_COUNT	(0x2000 / 4)		/* 2048 entries: 8 KB / 4 */

static PGXP_value UserMem[USER_MEM_COUNT];
static PGXP_value ScratchMem[SCRATCH_MEM_COUNT];
static PGXP_value RegisterMem[REGISTER_MEM_COUNT];

#define PGXP_MEM_TOTAL (USER_MEM_COUNT + SCRATCH_MEM_COUNT + REGISTER_MEM_COUNT)

/* One-way "ever PGXP-written" bitmap over the packed address space,
 * one bit per shadow word (~64 KB total, L2-resident).
 *
 * The shadow arrays above total ~14.4 MB of 28-byte entries walked in
 * lockstep with guest loads, but for most games the overwhelming
 * majority of guest words are never stored through a PGXP-tracked
 * path: their entries stay exactly as PGXP_InitMem's memset left them
 * (all zero), and the 28-byte read on every guest load is pure L2/L3
 * traffic for a predetermined answer.  A clear bit proves the entry
 * is that memset zero, so the load path can synthesize the identical
 * result without touching the entry's cache line at all.
 *
 * Invariants that make this bit-exact rather than approximate:
 *   - The ONLY mutation paths into the arrays are WriteMem and
 *     WriteMem16 (InvalidStore funnels through WriteMem; ReadMem /
 *     GetPtr consumers in pgxp_cpu.c are read-only), and both set
 *     the bit before writing.
 *   - Validate()/MaskValidate() on an all-zero entry are no-ops
 *     (flags &= x on flags == 0), so the skipped array access could
 *     never have changed the entry either.
 *   - The bit is never cleared except by PGXP_InitMem, matching the
 *     lifetime of the memset.  Entries that were written once keep
 *     taking the full path even after invalidation - conservative,
 *     but the touched set (stack, vertex scratch) stays small.
 * Ordinary savestates leave these shadows out. Managed states keep the
 * bitmap with the shadow pages so it describes the same execution point. */
static uint32_t MemEverWritten[(PGXP_MEM_TOTAL + 31) / 32];

#define PGXP_MEMORY_WORDS 128
#define PGXP_MEMORY_PAGES (PGXP_MEM_TOTAL / PGXP_MEMORY_WORDS)

struct pgxp_memory_page
{
   unsigned refs;
   PGXP_value words[PGXP_MEMORY_WORDS];
};

struct pgxp_memory_state
{
   struct pgxp_memory_page *pages[PGXP_MEMORY_PAGES];
   uint32_t written[(PGXP_MEM_TOTAL + 31) / 32];
};

#define PGXP_MEMORY_PAGE_LIMIT (((size_t)128 << 20) / sizeof(struct pgxp_memory_page))
static struct pgxp_memory_page *memory_pages[PGXP_MEMORY_PAGES];
static uint8_t memory_dirty[PGXP_MEMORY_PAGES];
static size_t memory_page_count;
static bool memory_tracking;

const uint32_t UserMemOffset  = 0;
const uint32_t ScratchOffset  = USER_MEM_COUNT;
const uint32_t RegisterOffset = USER_MEM_COUNT + SCRATCH_MEM_COUNT;
const uint32_t InvalidAddress = USER_MEM_COUNT + SCRATCH_MEM_COUNT + REGISTER_MEM_COUNT;

static inline int MemBitTest(uint32_t paddr)
{
	return (MemEverWritten[paddr >> 5] >> (paddr & 31)) & 1;
}

static inline void MemBitSet(uint32_t paddr)
{
	MemEverWritten[paddr >> 5] |= (uint32_t)1 << (paddr & 31);
	if (memory_tracking)
		memory_dirty[paddr / PGXP_MEMORY_WORDS] = 1;
}

/* Resolve an already-converted (packed) address to its entry.
 * Caller guarantees paddr < InvalidAddress. */
static inline PGXP_value* PtrFromPacked(uint32_t paddr)
{
	if (paddr < ScratchOffset)
		return &UserMem[paddr - UserMemOffset];
	if (paddr < RegisterOffset)
		return &ScratchMem[paddr - ScratchOffset];
	return &RegisterMem[paddr - RegisterOffset];
}

static PGXP_value *pgxp_memory_page_address(unsigned page)
{
	return PtrFromPacked(page * PGXP_MEMORY_WORDS);
}

static void pgxp_memory_page_release(struct pgxp_memory_page *page)
{
	if (page && !--page->refs)
	{
		memory_page_count--;
		free(page);
	}
}

void PGXP_MemoryStopTracking(void)
{
	unsigned i;
	if (!memory_tracking)
		return;
	for (i = 0; i < PGXP_MEMORY_PAGES; i++)
	{
		pgxp_memory_page_release(memory_pages[i]);
		memory_pages[i] = NULL;
	}
	memory_tracking = false;
}

void PGXP_MemoryStateFree(struct pgxp_memory_state *state)
{
	unsigned i;
	if (!state)
		return;
	for (i = 0; i < PGXP_MEMORY_PAGES; i++)
		pgxp_memory_page_release(state->pages[i]);
	free(state);
}

struct pgxp_memory_state *PGXP_MemoryStateSave(void)
{
	static const PGXP_value zero[PGXP_MEMORY_WORDS];
	struct pgxp_memory_state *state;
	unsigned i;

	state = (struct pgxp_memory_state *)calloc(1, sizeof(*state));
	if (!state)
		return NULL;
	if (!memory_tracking)
	{
		memset(memory_dirty, 1, sizeof(memory_dirty));
		memory_tracking = true;
	}

	/* Reuse unchanged pages across retained states. Each dirty page gets a
	 * private copy if any state still needs the previous contents. */
	for (i = 0; i < PGXP_MEMORY_PAGES; i++)
	{
		struct pgxp_memory_page *page = memory_pages[i];
		if (memory_dirty[i])
		{
			const PGXP_value *src = pgxp_memory_page_address(i);
			if (!memcmp(src, zero, sizeof(zero)))
			{
				pgxp_memory_page_release(page);
				page = NULL;
			}
			else
			{
				if (!page || page->refs != 1)
				{
					struct pgxp_memory_page *next =
						memory_page_count < PGXP_MEMORY_PAGE_LIMIT ?
						(struct pgxp_memory_page *)malloc(sizeof(*next)) : NULL;
					if (!next)
					{
						PGXP_MemoryStateFree(state);
						return NULL;
					}
					memory_page_count++;
					next->refs = 1;
					pgxp_memory_page_release(page);
					page = next;
				}
				memcpy(page->words, src, sizeof(page->words));
			}
			memory_pages[i] = page;
			memory_dirty[i] = 0;
		}
		if (page)
			page->refs++;
		state->pages[i] = page;
	}
	memcpy(state->written, MemEverWritten, sizeof(state->written));
	return state;
}

void PGXP_MemoryStateLoad(const struct pgxp_memory_state *state)
{
	unsigned i;
	for (i = 0; i < PGXP_MEMORY_PAGES; i++)
	{
		struct pgxp_memory_page *page = state->pages[i];
		if (!memory_tracking || memory_dirty[i] || memory_pages[i] != page)
		{
			PGXP_value *dst = pgxp_memory_page_address(i);
			if (page)
				memcpy(dst, page->words, sizeof(page->words));
			else
				memset(dst, 0, PGXP_MEMORY_WORDS * sizeof(*dst));
		}
		if (memory_pages[i] != page)
		{
			if (page)
				page->refs++;
			pgxp_memory_page_release(memory_pages[i]);
			memory_pages[i] = page;
		}
		memory_dirty[i] = 0;
	}
	memcpy(MemEverWritten, state->written, sizeof(MemEverWritten));
	memory_tracking = true;
}

void PGXP_InitMem()
{
	PGXP_MemoryStopTracking();
	memset(UserMem,     0, sizeof(UserMem));
	memset(ScratchMem,  0, sizeof(ScratchMem));
	memset(RegisterMem, 0, sizeof(RegisterMem));
	memset(MemEverWritten, 0, sizeof(MemEverWritten));
}

/*  Playstation Memory Map (from Playstation doc by Joshua Walker)
0x0000_0000-0x0000_ffff		Kernel (64K)
0x0001_0000-0x001f_ffff		User Memory (1.9 Meg)

0x1f00_0000-0x1f00_ffff		Parallel Port (64K)

0x1f80_0000-0x1f80_03ff		Scratch Pad (1024 bytes)

0x1f80_1000-0x1f80_2fff		Hardware Registers (8K)

0x1fc0_0000-0x1fc7_ffff		BIOS (512K)

0x8000_0000-0x801f_ffff		Kernel and User Memory Mirror (2 Meg) Cached
0x9fc0_0000-0x9fc7_ffff		BIOS Mirror (512K) Cached

0xa000_0000-0xa01f_ffff		Kernel and User Memory Mirror (2 Meg) Uncached
0xbfc0_0000-0xbfc7_ffff		BIOS Mirror (512K) Uncached
*/
void ValidateAddress(uint32_t addr)
{
	if ((addr >= 0x00000000) && (addr <= 0x007fffff)) {}	/* Kernel + User Memory x 8 */
	else if ((addr >= 0x1f000000) && (addr <= 0x1f00ffff)) {}	/* Parallel Port */
	else if ((addr >= 0x1f800000) && (addr <= 0x1f8003ff)) {}	/* Scratch Pad */
	else if ((addr >= 0x1f801000) && (addr <= 0x1f802fff)) {}	/* Hardware Registers */
	else if ((addr >= 0x1fc00000) && (addr <= 0x1fc7ffff)) {}	/* Bios */
	else if ((addr >= 0x80000000) && (addr <= 0x807fffff)) {}	/* Kernel + User Memory x 8 Cached mirror */
	else if ((addr >= 0x9fc00000) && (addr <= 0x9fc7ffff)) {}	/* Bios Cached Mirror */
	else if ((addr >= 0xa0000000) && (addr <= 0xa07fffff)) {}	/* Kernel + User Memory x 8 Uncached mirror */
	else if ((addr >= 0xbfc00000) && (addr <= 0xbfc7ffff)) {}	/* Bios Uncached Mirror */
	else if (addr == 0xfffe0130) {}								/* Used for cache flushing */
	else
	{
		/* Unrecognized address - debug breakpoint hook would go here */
	}

}

uint32_t PGXP_ConvertAddress(uint32_t addr)
{
	uint32_t paddr = addr;

	switch (paddr >> 24)
	{
	case 0x80:
	case 0xa0:
	case 0x00:
		/* RAM further mirrored over 8MB */
		paddr = ((paddr & 0x7FFFFF) % 0x200000) >> 2;
		paddr = UserMemOffset + paddr;
		break;
	default:
		if ((paddr >> 20) == 0x1f8)
		{
			if (paddr >= 0x1f801000)
			{
				paddr = ((paddr & 0xFFFF) - 0x1000) >> 2;
				paddr = RegisterOffset + paddr;
				break;
			}
			else
			{
				paddr = (paddr & 0x3FF) >> 2;
				paddr = ScratchOffset + paddr;
				break;
			}
		}

		paddr = InvalidAddress;
		break;
	}

	return paddr;
}

PGXP_value* GetPtr(uint32_t addr)
{
	addr = PGXP_ConvertAddress(addr);

	if (addr < InvalidAddress)
		return PtrFromPacked(addr);
	return NULL;
}

PGXP_value* ReadMem(uint32_t addr)
{
	return GetPtr(addr);
}

void ValidateAndCopyMem(PGXP_value* dest, uint32_t addr, uint32_t value)
{
	uint32_t paddr = PGXP_ConvertAddress(addr);
	if (paddr < InvalidAddress)
	{
		PGXP_value* pMem;
		uint32_t flags;
		if (!MemBitTest(paddr))
		{
			/* Entry is provably the InitMem memset zero: Validate()
			 * on a zero entry is a no-op and the copy would yield
			 * all-zero.  Synthesize that without touching the
			 * entry's cache line. */
			memset(dest, 0, sizeof(*dest));
			return;
		}

		pMem = PtrFromPacked(paddr);
		flags = pMem->flags;
		Validate(pMem, value);
		if (memory_tracking && flags != pMem->flags)
			memory_dirty[paddr / PGXP_MEMORY_WORDS] = 1;
		*dest = *pMem;
		return;
	}

	*dest = PGXP_value_invalid_address;
}

void ValidateAndCopyMem16(PGXP_value* dest, uint32_t addr, uint32_t value, int sign)
{
	uint32_t validMask = 0;
	psx_value val, mask;
	PGXP_value zero_local;
	PGXP_value* pMem = NULL;
	uint32_t paddr = PGXP_ConvertAddress(addr);

	if (paddr < InvalidAddress)
	{
		if (!MemBitTest(paddr))
		{
			/* Never-written entry is the memset zero; run the
			 * identical merge/truncate body below against a local
			 * zero so the result is bit-for-bit what reading the
			 * array would produce, without touching its cache
			 * line.  MaskValidate's writeback lands in the
			 * throwaway. */
			memset(&zero_local, 0, sizeof(zero_local));
			pMem = &zero_local;
		}
		else
			pMem = PtrFromPacked(paddr);
	}

	if (pMem != NULL)
	{
		mask.d = val.d = 0;
		/* determine if high or low word */
		if ((addr % 4) == 2)
		{
			val.w.h = value;
			mask.w.h = 0xFFFF;
			validMask = VALID_1;
		}
		else
		{
			val.w.l = value;
			mask.w.l = 0xFFFF;
			validMask = VALID_0;
		}

		/* validate and copy whole value */
		{
			uint32_t flags = pMem->flags;
			MaskValidate(pMem, val.d, mask.d, validMask);
			if (memory_tracking && pMem != &zero_local && flags != pMem->flags)
				memory_dirty[paddr / PGXP_MEMORY_WORDS] = 1;
		}
		*dest = *pMem;

		/* if high word then shift */
		if ((addr % 4) == 2)
		{
			dest->x = dest->y;
			dest->lFlags = dest->hFlags;
			dest->compFlags[0] = dest->compFlags[1];
		}

		/* truncate value */
		dest->y = (dest->x < 0) ? -1.f * sign : 0.f;/* 0.f; */
		dest->hFlags = 0;
		dest->value = value;
		dest->compFlags[1] = VALID;	/* iCB: High word is valid, just 0 */
		return;
	}

	*dest = PGXP_value_invalid_address;
}

void WriteMem(PGXP_value* value, uint32_t addr)
{
	uint32_t paddr = PGXP_ConvertAddress(addr);

	if (paddr < InvalidAddress)
	{
		MemBitSet(paddr);
		*PtrFromPacked(paddr) = *value;
	}
}

void WriteMem16(PGXP_value* src, uint32_t addr)
{
	PGXP_value* dest = NULL;
	uint32_t paddr = PGXP_ConvertAddress(addr);

	if (paddr < InvalidAddress)
	{
		/* The halfword merge below reads the entry's other half, so
		 * the bit must be set even though a never-written entry is
		 * still all-zero at this point - reading zeros from the
		 * array here is exactly the pre-bitmap behaviour. */
		MemBitSet(paddr);
		dest = PtrFromPacked(paddr);
	}

	if (dest)
	{
		/* Load dest->value into a local psx_value, manipulate, store
		 * back.  The previous code aliased dest->value (a u32) through
		 * a psx_value pointer, which is a strict-aliasing violation
		 * (u32 and psx_value are not compatible types in C's effective
		 * type rules).  The round-trip through a local is bit-exact,
		 * defined behaviour, and the compiler optimises it identically
		 * on every backend we ship. */
		psx_value pVal;
		pVal.d = dest->value;

		/* determine if high or low word */
		if ((addr % 4) == 2)
		{
			dest->y = src->x;
			dest->hFlags = src->lFlags;
			dest->compFlags[1] = src->compFlags[0];
			pVal.w.h = (uint16_t)src->value;
		}
		else
		{
			dest->x = src->x;
			dest->lFlags = src->lFlags;
			dest->compFlags[0] = src->compFlags[0];
			pVal.w.l = (uint16_t)src->value;
		}

		dest->value = pVal.d;

		/* overwrite z/w if valid */
		if (src->compFlags[2] == VALID)
		{
			dest->z = src->z;
			dest->compFlags[2] = src->compFlags[2];
		}


		/*dest->valid = dest->valid && src->valid; */
		dest->gFlags |= src->gFlags;				/* inherit flags from both values (?) */
	}
}
