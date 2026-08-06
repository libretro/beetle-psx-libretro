/* VCD_ProbeDisc against synthesised control sectors.
 *
 * This path decides whether a disc is a Video CD at all and, if so, what its
 * chapter list is -- so everything downstream is gated on it. It had never
 * been executed: the pipeline test bypasses it and the core only reaches it
 * with a real disc mounted.
 *
 * The sectors are built here to the letter of the spec: PVD at LBA 16 with
 * the CD-BRIDGE system identifier, INFO.VCD at LBA 150 and ENTRIES.VCD at
 * 151, all multi-byte fields big-endian and all MSF values BCD.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../mednafen/psx/vcd.h"

static uint8_t pvd[2048], info[2048], entries[2048];
static int     serve_pvd = 1, serve_info = 1, serve_entries = 1;

struct CDIF;
int CDIF_ReadSector(struct CDIF *c, uint8_t *b, uint32_t lba, uint32_t n)
{
   (void)c; (void)n;
   if (lba == 16  && serve_pvd)     { memcpy(b, pvd, 2048);     return 1; }
   if (lba == 150 && serve_info)    { memcpy(b, info, 2048);    return 1; }
   if (lba == 151 && serve_entries) { memcpy(b, entries, 2048); return 1; }
   return 0;
}

static void put_be16(uint8_t *p, unsigned v){ p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
static void put_be32(uint8_t *p, unsigned v)
{ p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }
static uint8_t bcd(unsigned v){ return (uint8_t)(((v/10)%10)<<4 | (v%10)); }

static void build(const char *id, unsigned ver, unsigned n_entries,
                  int pal, unsigned psd_size)
{
   unsigned i;

   memset(pvd, 0, sizeof(pvd));
   pvd[0] = 0x01;
   memcpy(pvd + 1, "CD001", 5);
   memcpy(pvd + 8, "CD-RTOS CD-BRIDGE", 17);

   memset(info, 0, sizeof(info));
   memcpy(info, id, 8);
   info[8] = (uint8_t)ver;
   info[9] = 0x00;
   memcpy(info + 0x0A, "TEST ALBUM      ", 16);
   info[0x1E] = pal ? 0x40 : 0x00;
   put_be32(info + 0x2C, psd_size);

   memset(entries, 0, sizeof(entries));
   memcpy(entries, "ENTRYVCD", 8);
   entries[8] = (uint8_t)ver;
   entries[9] = 0x00;
   put_be16(entries + 0x0A, n_entries);
   for (i = 0; i < n_entries; i++)
   {
      uint8_t *e = entries + 0x0C + i * 4;
      unsigned mm = 2 + i * 3, ss = (i * 17) % 60, ff = (i * 7) % 75;
      e[0] = bcd(2 + i);      /* track  */
      e[1] = bcd(mm);
      e[2] = bcd(ss);
      e[3] = bcd(ff);
   }
}

static uint32_t msf2lba(unsigned m, unsigned s, unsigned f)
{ return (uint32_t)(((m*60u+s)*75u+f) - 150u); }

static int check(const char *what, int cond)
{ printf("  %-42s %s\n", what, cond ? "ok" : "FAIL"); return cond ? 0 : 1; }

int main(void)
{
   VCD_DiscInfo di;
   int fails = 0;

   printf("VCD_ProbeDisc:\n");

   build("VIDEO_CD", 2, 5, 0, 512);
   fails += check("VCD 2.0 detected",
                  VCD_ProbeDisc((struct CDIF *)1, &di) == VCD_DISC_VCD20);
   fails += check("entry count", di.num_entries == 5);
   fails += check("NTSC flag", di.pal == false);
   fails += check("PBC seen", di.has_pbc == true);
   fails += check("entry 0 track is 2", di.entries[0].track == 2);
   fails += check("entry 0 LBA from BCD MSF",
                  di.entries[0].lba == msf2lba(2, 0, 0));
   fails += check("entry 3 LBA from BCD MSF",
                  di.entries[3].lba == msf2lba(11, 51, 21));
   fails += check("album id decoded",
                  strncmp(di.album_id, "TEST ALBUM", 10) == 0);

   build("VIDEO_CD", 1, 2, 1, 0);
   fails += check("VCD 1.1 detected",
                  VCD_ProbeDisc((struct CDIF *)1, &di) == VCD_DISC_VCD11);
   fails += check("PAL flag", di.pal == true);
   fails += check("no PBC", di.has_pbc == false);

   build("SUPERVCD", 1, 3, 0, 0);
   fails += check("SVCD detected",
                  VCD_ProbeDisc((struct CDIF *)1, &di) == VCD_DISC_SVCD);
   build("HQ-VCD  ", 1, 3, 0, 0);
   fails += check("HQ-VCD detected",
                  VCD_ProbeDisc((struct CDIF *)1, &di) == VCD_DISC_HQVCD);

   printf("rejection:\n");
   build("VIDEO_CD", 2, 4, 0, 0);
   memcpy(pvd + 8, "PLAYSTATION      ", 17);
   fails += check("ordinary PSX disc rejected at the PVD",
                  VCD_ProbeDisc((struct CDIF *)1, &di) == VCD_DISC_NONE);

   build("VIDEO_CD", 2, 4, 0, 0);
   pvd[1] = 'X';
   fails += check("non-ISO9660 rejected",
                  VCD_ProbeDisc((struct CDIF *)1, &di) == VCD_DISC_NONE);

   build("VIDEO_CD", 2, 4, 0, 0);
   serve_info = 0;
   fails += check("unreadable INFO.VCD rejected",
                  VCD_ProbeDisc((struct CDIF *)1, &di) == VCD_DISC_NONE);
   serve_info = 1;

   /* A bad chapter list does not make the disc something other than a Video
    * CD: the type comes from INFO. Expect the type to survive and the list
    * to come back empty, which is what the caller has to handle anyway. */
   build("VIDEO_CD", 2, 4, 0, 0);
   memcpy(entries, "GARBAGE!", 8);
   fails += check("bad ENTRIES keeps the type",
                  VCD_ProbeDisc((struct CDIF *)1, &di) == VCD_DISC_VCD20);
   fails += check("bad ENTRIES yields an empty list", di.num_entries == 0);

   fails += check("NULL cdif safe",
                  VCD_ProbeDisc(NULL, &di) == VCD_DISC_NONE);
   fails += check("NULL out safe",
                  VCD_ProbeDisc(NULL, NULL) == VCD_DISC_NONE);

   printf("malformed entries:\n");
   build("VIDEO_CD", 2, 3, 0, 0);
   entries[0x0C + 4 + 2] = 0x99;         /* seconds 99, out of range */
   VCD_ProbeDisc((struct CDIF *)1, &di);
   fails += check("out-of-range MSF entry skipped, others kept",
                  di.num_entries == 2);

   build("VIDEO_CD", 2, 4, 0, 0);
   put_be16(entries + 0x0A, 60000);      /* absurd count */
   VCD_ProbeDisc((struct CDIF *)1, &di);
   fails += check("entry count clamped to the array",
                  di.num_entries <= 500);

   printf(fails ? "RESULT: FAIL\n" : "RESULT: PASS\n");
   return fails ? 1 : 0;
}
