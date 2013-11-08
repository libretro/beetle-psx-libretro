#ifndef __MDFN_CLAMP_H
#define __MDFN_CLAMP_H

static INLINE int32 clamp_to_u8(int32 i)
{
 if(i & 0xFFFFFF00)
  i = (((~i) >> 30) & 0xFF);

 return(i);
}


static INLINE int32 clamp_to_u16(int32 i)
{
 if(i & 0xFFFF0000)
  i = (((~i) >> 31) & 0xFFFF);

 return(i);
}

static INLINE void clamp(int32_t *val, size_t, size_t)
{
   if ( (int16_t) *val != *val )
      *val = (*val >> 31) ^ 0x7FFF;
}

#define clamp_simple(val) \
   if ( (int16_t)val != val ) \
      val = (val >> 31) ^ 0x7FFF;

#ifdef WANT_PCFX_EMU
static INLINE void clamp(int64_t *val, size_t, size_t)
{
   if ( (int16_t) *val != *val )
      *val = (*val >> 31) ^ 0x7FFF;
}
#endif

#endif
