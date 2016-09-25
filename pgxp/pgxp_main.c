#include "pgxp_main.h"
#include "pgxp_cpu.h"
#include "pgxp_mem.h"
#include "pgxp_gte.h"

u32 static gMode = 0;

void PGXP_Init()
{
	PGXP_InitMem();
	PGXP_InitCPU();
	PGXP_InitGTE();
}

void PGXP_SetModes(u32 modes)
{
	gMode = modes;
}

u32	PGXP_GetModes()
{
	return gMode;
}

void PGXP_EnableModes(u32 modes)
{
	gMode |= modes;
}

void PGXP_DisableModes(u32 modes)
{
	gMode = gMode & ~modes;
}
