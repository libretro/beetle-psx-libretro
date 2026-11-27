#include <stdlib.h>

#include "pgxp_main.h"
#include "pgxp_state.h"
#include "pgxp_cpu.h"
#include "pgxp_gpu.h"
#include "pgxp_mem.h"
#include "pgxp_gte.h"

/* Hot-path: gMode is read on every CPU instruction with a PGXP
 * branch (see the 50+ PGXP_GetModes() call sites in mednafen/psx/
 * cpu.c).  Make it externally visible so PGXP_GetModes can live
 * in the header as a static inline - skipping the cross-TU call
 * setup at every site - while keeping all writes routed through
 * apply_modes() here so the vertex-cache free still happens. */
uint32_t gMode = 0;

void PGXP_Init(void)
{
	PGXP_InitMem();
	PGXP_InitCPU();
	PGXP_InitGTE();
}

void PGXP_Shutdown(void)
{
	gMode = 0;
	PGXP_StateStopTracking();
	PGXP_FreeVertexCache();
}

/* Apply a mode transition.  If the PGXP_VERTEX_CACHE bit is being
 * cleared (user toggled the vertex-cache core option off at runtime),
 * the 448 MB heap allocation backing the cache is freed immediately
 * rather than waiting for retro_deinit. */
static void apply_modes(uint32_t new_modes)
{
	uint32_t old_modes = gMode;
	gMode = new_modes;
	if (old_modes && !new_modes)
		PGXP_StateStopTracking();
	if ((old_modes & PGXP_VERTEX_CACHE) && !(new_modes & PGXP_VERTEX_CACHE))
		PGXP_FreeVertexCache();
	/* Off to on: TransformXY skips the vertex push entirely while gMode is
	 * zero, so the screen-XY FIFO holds entries from before PGXP was last
	 * disabled. Retire them rather than let a stale one match by chance. */
	if (!old_modes && new_modes)
		PGXP_InvalidateVertexFIFO();
}

void PGXP_SetModes(uint32_t modes)
{
	apply_modes(modes);
}

void PGXP_EnableModes(uint32_t modes)
{
	apply_modes(gMode | modes);
}

void PGXP_DisableModes(uint32_t modes)
{
	apply_modes(gMode & ~modes);
}

struct pgxp_saved_state
{
   struct pgxp_memory_state *memory;
   struct pgxp_vertex_state *vertices;
   void *vertex_reserve;
   uint32_t modes;
};

static void pgxp_state_transfer(struct pgxp_saved_state *state, bool load)
{
   uint8_t *data = (uint8_t *)state + sizeof(*state);
   PGXP_CPUState(data, load);
   data += PGXP_CPUStateSize();
   PGXP_GTEState(data, load);
   data += PGXP_GTEStateSize();
   PGXP_GPUState(data, load);
}

struct pgxp_saved_state *PGXP_StateSave(void)
{
   size_t bytes = PGXP_CPUStateSize() + PGXP_GTEStateSize() + PGXP_GPUStateSize();
   struct pgxp_saved_state *state =
         (struct pgxp_saved_state *)calloc(1, sizeof(*state) + bytes);
   if (!state)
      return NULL;
   state->modes = gMode;
   state->memory = PGXP_MemoryStateSave();
   if (!state->memory || ((gMode & PGXP_VERTEX_CACHE) &&
         !PGXP_VertexStateSave(&state->vertices)))
   {
      PGXP_StateFree(state);
      return NULL;
   }
   pgxp_state_transfer(state, false);
   return state;
}

bool PGXP_StatePrepare(struct pgxp_saved_state *state)
{
   return state->modes == gMode &&
         PGXP_VertexStatePrepare(state->vertices, &state->vertex_reserve);
}

void PGXP_StateLoad(struct pgxp_saved_state *state)
{
   PGXP_MemoryStateLoad(state->memory);
   PGXP_VertexStateLoad(state->vertices, &state->vertex_reserve);
   pgxp_state_transfer(state, true);
}

void PGXP_StateFree(struct pgxp_saved_state *state)
{
   if (state)
   {
      PGXP_MemoryStateFree(state->memory);
      PGXP_VertexStateFree(state->vertices);
      free(state->vertex_reserve);
      free(state);
   }
}

void PGXP_StateStopTracking(void)
{
   PGXP_MemoryStopTracking();
}
