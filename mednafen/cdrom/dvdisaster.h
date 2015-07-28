/*  dvdisaster: Additional error correction for optical media.
 *  Copyright (C) 2004-2007 Carsten Gnoerlich.
 *  Project home page: http://www.dvdisaster.com
 *  Email: carsten@dvdisaster.com  -or-  cgnoerlich@fsfe.org
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA,
 *  or direct your browser at http://www.gnu.org.
 */

#ifndef DVDISASTER_H
#define DVDISASTER_H

/* "Dare to be gorgeous and unique. 
 *  But don't ever be cryptic or otherwise unfathomable.
 *  Make it unforgettably great."
 *
 *  From "A Final Note on Style", 
 *  Amiga Intuition Reference Manual, 1986, p. 231
 */

/***
 *** I'm too lazy to mess with #include dependencies.
 *** Everything #includeable is rolled up herein...
 */

#include "../mednafen-types.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

/***
 *** dvdisaster.c
 ***/

void PrepareDeadSector(void);

void CreateEcc(void);
void FixEcc(void);
void Verify(void);

#include "edc_crc32.h"
#include "galois.h"
#include "l-ec.h"

/***
 *** misc.c 
 ***/

char* sgettext(char*);
char* sgettext_utf8(char*);

int64 uchar_to_int64(unsigned char*);
void int64_to_uchar(unsigned char*, int64);

void CalcSectors(int64, int64*, int*);

#include "recover-raw.h"



#endif				/* DVDISASTER_H */
