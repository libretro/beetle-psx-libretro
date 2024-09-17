/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <vector>

#ifdef _WIN32
#include <compat/msvc.h>
#endif

#include <boolean.h>

#include "mednafen.h"

#include "general.h"
#include "settings.h"
#include "mempatcher.h"

#include <libretro.h>
#include "psx/psx.h"

extern retro_log_printf_t log_cb;

static uint8 **RAMPtrs = NULL;
static uint32 PageSize;
static uint32 NumPages;

typedef MemoryPatch CHEATF;
#if 0
typedef struct __CHEATF
{
           char *name;
           char *conditions;

           uint32 addr;
           uint64 val;
           uint64 compare;

           unsigned int length;
           bool bigendian;
           unsigned int icount; // Instance count
           char type;   /* 'R' for replace, 'S' for substitute(GG), 'C' for substitute with compare */
           int status;
} CHEATF;
#endif

static std::vector<CHEATF> cheats;
static bool CheatsActive = true;

static std::vector<SUBCHEAT> SubCheats[8];

MemoryPatch::MemoryPatch() : addr(0), val(0), compare(0), 
			     mltpl_count(1), mltpl_addr_inc(0), mltpl_val_inc(0), copy_src_addr(0), copy_src_addr_inc(0),
			     length(0), bigendian(false), status(false), icount(0), type(0)
{

}

MemoryPatch::~MemoryPatch()
{

}

static void RebuildSubCheats(void)
{
 std::vector<CHEATF>::iterator chit;

 for(int x = 0; x < 8; x++)
  SubCheats[x].clear();

 if(!CheatsActive) return;

 for(chit = cheats.begin(); chit != cheats.end(); chit++)
 {
  if(chit->status && chit->type != 'R')
  {
   for(unsigned int x = 0; x < chit->length; x++)
   {
    SUBCHEAT tmpsub;
    unsigned int shiftie;

    if(chit->bigendian)
     shiftie = (chit->length - 1 - x) * 8;
    else
     shiftie = x * 8;
    
    tmpsub.addr = chit->addr + x;
    tmpsub.value = (chit->val >> shiftie) & 0xFF;
    if(chit->type == 'C')
     tmpsub.compare = (chit->compare >> shiftie) & 0xFF;
    else
     tmpsub.compare = -1;
    SubCheats[(chit->addr + x) & 0x7].push_back(tmpsub);
   }
  }
 }
}

bool MDFNMP_Init(uint32 ps, uint32 numpages)
{
 PageSize = ps;
 NumPages = numpages;

 RAMPtrs = (uint8 **)calloc(numpages, sizeof(uint8 *));

 CheatsActive = MDFN_GetSettingB("cheats");
 return(1);
}

void MDFNMP_Kill(void)
{
   if(RAMPtrs)
   {
      free(RAMPtrs);
      RAMPtrs = NULL;
   }
}


void MDFNMP_AddRAM(uint32 size, uint32 A, uint8 *RAM)
{
 uint32 AB = A / PageSize;
 
 size /= PageSize;

 for(unsigned int x = 0; x < size; x++)
 {
  RAMPtrs[AB + x] = RAM;
  if(RAM != INVALID_PTR) // Don't increment the RAM pointer if we're passed an invalid pointer
   RAM += PageSize;
 }
}

void MDFNMP_RegSearchable(uint32 addr, uint32 size)
{
 MDFNMP_AddRAM(size, addr, NULL);
}

void MDFNMP_InstallReadPatches(void)
{
   unsigned x;
   std::vector<SUBCHEAT>::iterator chit;
   if(!CheatsActive) return;

   for(x = 0; x < 8; x++)
      for(chit = SubCheats[x].begin(); chit != SubCheats[x].end(); chit++)
      {
#if 0
         if(EmulatedPSX.InstallReadPatch)
            EmulatedPSX.InstallReadPatch(chit->addr);
#endif
      }
}

void MDFNMP_RemoveReadPatches(void)
{
#if 0
   if(EmulatedPSX.RemoveReadPatches)
      EmulatedPSX.RemoveReadPatches();
#endif
}

/* This function doesn't allocate any memory for "name" */
static int AddCheatEntry(char *name, char *conditions, uint32 addr, uint64 val, uint64 compare, int status, char type, unsigned int length, bool bigendian)
{
 CHEATF temp = CHEATF();

 temp.name=name;
 temp.conditions = conditions;
 temp.addr=addr;
 temp.val=val;
 temp.status=status;
 temp.compare=compare;
 temp.length = length;
 temp.bigendian = bigendian;
 temp.type=type;

 cheats.push_back(temp);
 return(1);
}

void MDFN_LoadGameCheats(void *override_ptr)
{
 RebuildSubCheats();
}

void MDFN_FlushGameCheats(int nosave)
{
   cheats.clear();

   RebuildSubCheats();
}

void MDFNI_AddCheat(const MemoryPatch& patch)
{
 cheats.push_back(patch);

 MDFNMP_RemoveReadPatches();
 RebuildSubCheats();
 MDFNMP_InstallReadPatches();
}

int MDFNI_DelCheat(uint32 which)
{
 cheats.erase(cheats.begin() + which);

 MDFNMP_RemoveReadPatches();
 RebuildSubCheats();
 MDFNMP_InstallReadPatches();

 return(1);
}

/*
 Condition format(ws = white space):
 
  <variable size><ws><endian><ws><address><ws><operation><ws><value>
	  [,second condition...etc.]

  Value should be unsigned integer, hex(with a 0x prefix) or
  base-10.  

  Operations:
   >=
   <=
   >
   <
   ==
   !=
   &	// Result of AND between two values is nonzero
   !&   // Result of AND between two values is zero
   ^    // same, XOR
   !^
   |	// same, OR
   !|

  Full example:

  2 L 0xADDE == 0xDEAD, 1 L 0xC000 == 0xA0

*/

static bool TestConditions(const char *string)
{
 char address[64];
 char operation[64];
 char value[64];
 char endian;
 unsigned int bytelen;
 bool passed = 1;

 //printf("TR: %s\n", string);
 while(sscanf(string, "%u %c %63s %63s %63s", &bytelen, &endian, address, operation, value) == 5 && passed)
 {
  uint32 v_address;
  uint64 v_value;
  uint64 value_at_address;

  if(address[0] == '0' && address[1] == 'x')
   v_address = strtoul(address + 2, NULL, 16);
  else
   v_address = strtoul(address, NULL, 10);

  if(value[0] == '0' && value[1] == 'x')
   v_value = strtoull(value + 2, NULL, 16);
  else
   v_value = strtoull(value, NULL, 0);

  value_at_address = 0;
  for(unsigned int x = 0; x < bytelen; x++)
  {
   unsigned int shiftie;

   if(endian == 'B')
    shiftie = (bytelen - 1 - x) * 8;
   else
    shiftie = x * 8;
   value_at_address |= PSX_MemPeek8(v_address + x) << shiftie;
  }

  //printf("A: %08x, V: %08llx, VA: %08llx, OP: %s\n", v_address, v_value, value_at_address, operation);
  if(!strcmp(operation, ">="))
  {
   if(!(value_at_address >= v_value))
    passed = 0;
  }
  else if(!strcmp(operation, "<="))
  {
   if(!(value_at_address <= v_value))
    passed = 0;
  }
  else if(!strcmp(operation, ">"))
  {
   if(!(value_at_address > v_value))
    passed = 0;
  }
  else if(!strcmp(operation, "<"))
  {
   if(!(value_at_address < v_value))
    passed = 0;
  }
  else if(!strcmp(operation, "==")) 
  {
   if(!(value_at_address == v_value))
    passed = 0;
  }
  else if(!strcmp(operation, "!="))
  {
   if(!(value_at_address != v_value))
    passed = 0;
  }
  else if(!strcmp(operation, "&"))
  {
   if(!(value_at_address & v_value))
    passed = 0;
  }
  else if(!strcmp(operation, "!&"))
  {
   if(value_at_address & v_value)
    passed = 0;
  }
  else if(!strcmp(operation, "^"))
  {
   if(!(value_at_address ^ v_value))
    passed = 0;
  }
  else if(!strcmp(operation, "!^"))
  {
   if(value_at_address ^ v_value)
    passed = 0;
  }
  else if(!strcmp(operation, "|"))
  {
   if(!(value_at_address | v_value))
    passed = 0;
  }
  else if(!strcmp(operation, "!|"))
  {
   if(value_at_address | v_value)
    passed = 0;
  }
  string = strchr(string, ',');
  if(string == NULL)
   break;
  else
   string++;
  //printf("Foo: %s\n", string);
 }

 return(passed);
}

void MDFNMP_ApplyPeriodicCheats(void)
{
 if(!CheatsActive)
  return;

 //TestConditions("2 L 0x1F00F5 == 0xDEAD");
 //if(TestConditions("1 L 0x1F0058 > 0")) //, 1 L 0xC000 == 0x01"));
 for(std::vector<CHEATF>::iterator chit = cheats.begin(); chit != cheats.end(); chit++)
 {
  if(chit->status && (chit->type == 'R' || chit->type == 'A' || chit->type == 'T'))
  {
   if(chit->conditions.size() == 0 || TestConditions(chit->conditions.c_str()))
   {
    uint32 mltpl_count = chit->mltpl_count;
    uint32 mltpl_addr = chit->addr;
    uint64 mltpl_val = chit->val;
    uint32 copy_src_addr = chit->copy_src_addr;

    while(mltpl_count--)
    {
     uint8 carry = 0;

     for(unsigned int x = 0; x < chit->length; x++)
     {
      const uint32 tmpaddr = chit->bigendian ? (mltpl_addr + chit->length - 1 - x) : (mltpl_addr + x);
      const uint8 tmpval = mltpl_val >> (x * 8);

      if(chit->type == 'A')
      {
       const unsigned t = PSX_MemPeek8(tmpaddr) + tmpval + carry;

       carry = t >> 8;

       PSX_MemPoke8(tmpaddr, t);
      }
      else if(chit->type == 'T')
      {
       const uint8 cv = PSX_MemPeek8(chit->bigendian ? (copy_src_addr + chit->length - 1 - x) : (copy_src_addr + x));

       PSX_MemPoke8(tmpaddr, cv);
      }
      else
       PSX_MemPoke8(tmpaddr, tmpval);
     }
     mltpl_addr += chit->mltpl_addr_inc;
     mltpl_val += chit->mltpl_val_inc;
     copy_src_addr += chit->copy_src_addr_inc;
    }
   } // end if(chit->conditions.size() == 0 || TestConditions(chit->conditions.c_str()))
  }
 }
}


void MDFNI_ListCheats(int (*callb)(const MemoryPatch& patch, void *data), void *data)
{
 std::vector<CHEATF>::iterator chit;

 for(chit = cheats.begin(); chit != cheats.end(); chit++)
 {
  if(!callb(*chit, data))
   break;
 }
}

MemoryPatch MDFNI_GetCheat(uint32 which)
{
 return cheats[which];
}

static uint8 CharToNibble(char thechar)
{
 const char lut[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

 thechar = toupper(thechar);

 for(int x = 0; x < 16; x++)
  if(lut[x] == thechar)
   return(x);

 return(0xFF);
}

bool MDFNI_DecodeGBGG(const char *instr, uint32 *a, uint8 *v, uint8 *c, char *type)
{
 char str[10];
 size_t len;

 for(int x = 0; x < 9; x++)
 {
  while(*instr && CharToNibble(*instr) == 255)
   instr++;
  if(!(str[x] = *instr)) break;
  instr++;
 }
 str[9] = 0;

 len = strlen(str);

 if(len != 9 && len != 6)
  return(0);

 uint32 tmp_address;
 uint8 tmp_value;
 uint8 tmp_compare = 0;

 tmp_address =  (CharToNibble(str[5]) << 12) | (CharToNibble(str[2]) << 8) | (CharToNibble(str[3]) << 4) | (CharToNibble(str[4]) << 0);
 tmp_address ^= 0xF000;
 tmp_value = (CharToNibble(str[0]) << 4) | (CharToNibble(str[1]) << 0);

 if(len == 9)
 {
  tmp_compare = (CharToNibble(str[6]) << 4) | (CharToNibble(str[8]) << 0);
  tmp_compare = (tmp_compare >> 2) | ((tmp_compare << 6) & 0xC0);
  tmp_compare ^= 0xBA;
 }

 *a = tmp_address;
 *v = tmp_value;

 if(len == 9)
 {
  *c = tmp_compare;
  *type = 'C';
 }
 else
 {
  *c = 0;
  *type = 'S';
 }

 return(1);
}

static int GGtobin(char c)
{
 static char lets[16]={'A','P','Z','L','G','I','T','Y','E','O','X','U','K','S','V','N'};
 int x;

 for(x=0;x<16;x++)
  if(lets[x] == toupper(c)) return(x);
 return(0);
}

/* Returns 1 on success, 0 on failure. Sets *a,*v,*c. */
int MDFNI_DecodeGG(const char *str, uint32 *a, uint8 *v, uint8 *c, char *type)
{
   uint8 t;
   uint16 A=0x8000;
   uint8 V=0;
   uint8 C=0;
   size_t s=strlen(str);
   if(s!=6 && s!=8) return(0);

   t=GGtobin(*str++);
   V|=(t&0x07);
   V|=(t&0x08)<<4;

   t=GGtobin(*str++);
   V|=(t&0x07)<<4;
   A|=(t&0x08)<<4;

   t=GGtobin(*str++);
   A|=(t&0x07)<<4;
   //if(t&0x08) return(0);	/* 8-character code?! */

   t=GGtobin(*str++);
   A|=(t&0x07)<<12;
   A|=(t&0x08);

   t=GGtobin(*str++);
   A|=(t&0x07);
   A|=(t&0x08)<<8;

   if(s==6)
   {
      t=GGtobin(*str++);
      A|=(t&0x07)<<8;
      V|=(t&0x08);

      *a=A;
      *v=V;
      *type = 'S';
      *c = 0;
   }
   else
   {
      t=GGtobin(*str++);
      A|=(t&0x07)<<8;
      C|=(t&0x08);

      t=GGtobin(*str++);
      C|=(t&0x07);
      C|=(t&0x08)<<4;

      t=GGtobin(*str++);
      C|=(t&0x07)<<4;
      V|=(t&0x08);
      *a=A;
      *v=V;
      *c=C;
      *type = 'C';
   }

   return(1);
}

int MDFNI_DecodePAR(const char *str, uint32 *a, uint8 *v, uint8 *c, char *type)
{
 int boo[4];
 if(strlen(str)!=8) return(0);

 sscanf(str,"%02x%02x%02x%02x",boo,boo+1,boo+2,boo+3);

 *c = 0;

 if(1)
 {
  *a=(boo[3]<<8)|(boo[2]+0x7F);
  *v=0;
 }
 else
 {
  *v=boo[3];
  *a=boo[2]|(boo[1]<<8);
 }

 *type = 'S';
 return(1);
}

void MDFNI_SetCheat(uint32 which, const MemoryPatch& patch)
{
 cheats[which] = patch;

 MDFNMP_RemoveReadPatches();
 RebuildSubCheats();
 MDFNMP_InstallReadPatches();
}

/* Convenience function. */
int MDFNI_ToggleCheat(uint32 which)
{
 cheats[which].status = !cheats[which].status;
 RebuildSubCheats();

 return(cheats[which].status);
}
