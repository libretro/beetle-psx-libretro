/***************************************************************************
*   Copyright (C) 2016 by iCatButler                                      *
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
*   This program is distributed in the hope that it will be useful,       *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
*   GNU General Public License for more details.                          *
*                                                                         *
*   You should have received a copy of the GNU General Public License     *
*   along with this program; if not, write to the                         *
*   Free Software Foundation, Inc.,                                       *
*   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.           *
***************************************************************************/

/**************************************************************************
*	pgxp_mem.h
*	PGXP - Parallel/Precision Geometry Xform Pipeline
*
*	Created on: 07 Jun 2016
*      Author: iCatButler
***************************************************************************/

#ifndef _PGXP_MEM_H_
#define _PGXP_MEM_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "pgxp_types.h"

   void PGXP_InitMem(void);

   uint32_t		PGXP_ConvertAddress(uint32_t addr);

   PGXP_value* GetPtr(uint32_t addr);
   PGXP_value* ReadMem(uint32_t addr);

   void ValidateAndCopyMem(PGXP_value* dest, uint32_t addr, uint32_t value);
   void ValidateAndCopyMem16(PGXP_value* dest, uint32_t addr, uint32_t value, int sign);

   void WriteMem(PGXP_value* value, uint32_t addr);
   void WriteMem16(PGXP_value* src, uint32_t addr);

#ifdef __cplusplus
}
#endif

#endif//_PGXP_MEM_H_
