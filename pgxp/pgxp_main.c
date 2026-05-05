#include "pgxp_main.h"
#include "pgxp_cpu.h"
#include "pgxp_gpu.h"
#include "pgxp_mem.h"
#include "pgxp_gte.h"

u32 static gMode = 0;

void PGXP_Init(void)
{
	PGXP_InitMem();
	PGXP_InitCPU();
	PGXP_InitGTE();
}

void PGXP_Shutdown(void)
{
	gMode = 0;
	PGXP_FreeVertexCache();
}

/* Apply a mode transition.  If the PGXP_VERTEX_CACHE bit is being
 * cleared (user toggled the vertex-cache core option off at runtime),
 * the 448 MB heap allocation backing the cache is freed immediately
 * rather than waiting for retro_deinit. */
static void apply_modes(u32 new_modes)
{
	u32 old_modes = gMode;
	gMode = new_modes;
	if ((old_modes & PGXP_VERTEX_CACHE) && !(new_modes & PGXP_VERTEX_CACHE))
		PGXP_FreeVertexCache();
}

void PGXP_SetModes(u32 modes)
{
	apply_modes(modes);
}

u32	PGXP_GetModes()
{
	return gMode;
}

void PGXP_EnableModes(u32 modes)
{
	apply_modes(gMode | modes);
}

void PGXP_DisableModes(u32 modes)
{
	apply_modes(gMode & ~modes);
}
