#ifndef __MDFN_CDROM_CDUTILITY_H
#define __MDFN_CDROM_CDUTILITY_H

#include <stdint.h>
#include <string.h>

#include <boolean.h>
#include <retro_inline.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
   ADR_NOQINFO = 0x00,
   ADR_CURPOS  = 0x01,
   ADR_MCN     = 0x02,
   ADR_ISRC    = 0x03
};

struct TOC_Track
{
   uint8_t adr;
   uint8_t control;
   uint32_t lba;
  bool valid;	/* valid/present; oh CD-i... */
};

// SubQ control field flags.
enum
{
   SUBQ_CTRLF_PRE  = 0x01,	// With 50/15us pre-emphasis.
   SUBQ_CTRLF_DCP  = 0x02,	// Digital copy permitted.
   SUBQ_CTRLF_DATA = 0x04,	// Data track.
   SUBQ_CTRLF_4CH  = 0x08	// 4-channel CD-DA.
};

enum
{
   DISC_TYPE_CDDA_OR_M1 = 0x00,
   DISC_TYPE_CD_I       = 0x10,
   DISC_TYPE_CD_XA      = 0x20
};

struct TOC
{
   uint8_t first_track;
   uint8_t last_track;
   uint8_t disc_type;
   struct TOC_Track tracks[100 + 1];
};

// Call once at app startup before creating any threads that could potentially cause re-entrancy to these functions.
// It will also be called automatically if needed for the first time a function in this namespace that requires
// the initialization function to be called is called, for potential
// usage in constructors of statically-declared objects.
void CDUtility_Init(void);

// Quick definitions here:
//
// ABA - Absolute block address, synonymous to absolute MSF
//  aba = (m_a * 60 * 75) + (s_a * 75) + f_a
//
// LBA - Logical block address(related: data CDs are required to have a pregap of 2 seconds, IE 150 frames/sectors)
//  lba = aba - 150

static INLINE void TOC_Clear(struct TOC *toc)
{
   if (!toc)
      return;

   toc->first_track = 0;
   toc->last_track  = 0;
   toc->disc_type   = 0;

   memset(toc->tracks, 0, sizeof(toc->tracks));
}

static INLINE int TOC_FindTrackByLBA(struct TOC *toc, uint32_t LBA)
{
   int32_t track;

   for(track = toc->first_track; track <= (toc->last_track + 1); track++)
   {
      if(track == (toc->last_track + 1))
      {
         if(LBA < toc->tracks[100].lba)
            return(track - 1);
      }
      else
      {
         if(LBA < toc->tracks[track].lba)
            return(track - 1);
      }
   }

   return 0;
}

// Address conversion functions.
static INLINE uint32_t AMSF_to_ABA(int32_t m_a, int32_t s_a, int32_t f_a)
{
   return(f_a + 75 * s_a + 75 * 60 * m_a);
}

static INLINE void ABA_to_AMSF(uint32_t aba, uint8_t *m_a, uint8_t *s_a, uint8_t *f_a)
{
   *m_a = aba / 75 / 60;
   *s_a = (aba - *m_a * 75 * 60) / 75;
   *f_a = aba - (*m_a * 75 * 60) - (*s_a * 75);
}

static INLINE int32_t ABA_to_LBA(uint32_t aba)
{
   return(aba - 150);
}

static INLINE uint32_t LBA_to_ABA(int32_t lba)
{
   return(lba + 150);
}

static INLINE int32_t AMSF_to_LBA(uint8_t m_a, uint8_t s_a, uint8_t f_a)
{
   return(ABA_to_LBA(AMSF_to_ABA(m_a, s_a, f_a)));
}

static INLINE void LBA_to_AMSF(int32_t lba, uint8_t *m_a, uint8_t *s_a, uint8_t *f_a)
{
   ABA_to_AMSF(LBA_to_ABA(lba), m_a, s_a, f_a);
}

//
// BCD conversion functions
//
static INLINE bool BCD_is_valid(uint8_t bcd_number)
{
   if((bcd_number & 0xF0) >= 0xA0)
      return(false);

   if((bcd_number & 0x0F) >= 0x0A)
      return(false);

   return(true);
}

static INLINE uint8_t BCD_to_U8(uint8_t bcd_number)
{
   return( ((bcd_number >> 4) * 10) + (bcd_number & 0x0F) );
}

static INLINE uint8_t U8_to_BCD(uint8_t num)
{
   return( ((num / 10) << 4) + (num % 10) );
}

// should always perform the conversion, even if the bcd number is invalid.
static INLINE bool BCD_to_U8_check(uint8_t bcd_number, uint8_t *out_number)
{
   *out_number = BCD_to_U8(bcd_number);

   if(!BCD_is_valid(bcd_number))
      return(false);

   return(true);
}

//
// Sector data encoding functions(to full 2352 bytes raw sector).
//
//  sector_data must be able to contain at least 2352 bytes.
void encode_mode0_sector(uint32_t aba, uint8_t *sector_data);
void encode_mode1_sector(uint32_t aba, uint8_t *sector_data);	// 2048 bytes of user data at offset 16
void encode_mode2_sector(uint32_t aba, uint8_t *sector_data);	// 2336 bytes of user data at offset 16 
void encode_mode2_form1_sector(uint32_t aba, uint8_t *sector_data);	// 2048+8 bytes of user data at offset 16
void encode_mode2_form2_sector(uint32_t aba, uint8_t *sector_data);	// 2324+8 bytes of user data at offset 16


// out_buf must be able to contain 2352+96 bytes.
// "mode" is only used if(toc.tracks[100].control & 0x4)
void synth_leadout_sector_lba(uint8_t mode, const struct TOC *toc, const int32_t lba, uint8_t* out_buf);

//
// User data error detection and correction
//

// Check EDC of a mode 1 or mode 2 form 1 sector.
//  Returns "true" if checksum is ok(matches).
//  Returns "false" if checksum mismatch.
//  sector_data should contain 2352 bytes of raw sector data.
bool edc_check(const uint8_t *sector_data, bool xa);

// Check EDC and L-EC data of a mode 1 or mode 2 form 1 sector, and correct bit errors if any exist.
//  Returns "true" if errors weren't detected, or they were corrected succesfully.
//  Returns "false" if errors couldn't be corrected.
//  sector_data should contain 2352 bytes of raw sector data.
bool edc_lec_check_and_correct(uint8_t *sector_data, bool xa);

//
// Subchannel(Q in particular) functions
//

// Returns false on checksum mismatch, true on match.
bool subq_check_checksum(const uint8_t *subq_buf);

// Calculates the checksum of Q subchannel data(not including the checksum bytes of course ;)) from subq_buf, and stores it into the appropriate position
// in subq_buf.
void subq_generate_checksum(uint8_t *subq_buf);

// Deinterleaves 12 bytes of subchannel Q data from 96 bytes of interleaved subchannel PW data.
void subq_deinterleave(const uint8_t *subpw_buf, uint8_t *subq_buf);

// Deinterleaves 96 bytes of subchannel P-W data from 96 bytes of interleaved subchannel PW data.
void subpw_deinterleave(const uint8_t *in_buf, uint8_t *out_buf);

// Interleaves 96 bytes of subchannel P-W data from 96 bytes of uninterleaved subchannel PW data.
void subpw_interleave(const uint8_t *in_buf, uint8_t *out_buf);

// Extrapolates Q subchannel current position data from subq_input, with frame/sector delta position_delta, and writes to subq_output.
// Only valid for ADR_CURPOS.
// subq_input must pass subq_check_checksum().
// TODO
//void subq_extrapolate(const uint8_t *subq_input, int32_t position_delta, uint8_t *subq_output);

// (De)Scrambles data sector.
void scrambleize_data_sector(uint8_t *sector_data);

#ifdef __cplusplus
}
#endif

#endif
