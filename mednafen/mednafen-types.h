#ifndef __MDFN_TYPES
#define __MDFN_TYPES

#include <assert.h>
#include <stdint.h>

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32; 
typedef int64_t int64;

typedef uint8_t uint8;  
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

#ifdef __GNUC__

  #define INLINE inline __attribute__((always_inline))
  #define NO_INLINE __attribute__((noinline))

  #if defined(__386__) || defined(__i386__) || defined(__i386) || defined(_M_IX86) || defined(_M_I386)
    #define MDFN_FASTCALL __attribute__((fastcall))
  #else
    #define MDFN_FASTCALL
  #endif

  #define MDFN_ALIGN(n)	__attribute__ ((aligned (n)))
  #define MDFN_FORMATSTR(a,b,c) __attribute__ ((format (a, b, c)));
  #define MDFN_WARN_UNUSED_RESULT __attribute__ ((warn_unused_result))

#elif defined(_MSC_VER)
  #define INLINE inline __forceinline
  #define NO_INLINE

  #define MDFN_FASTCALL

  #define MDFN_ALIGN(n) __declspec(align(n))

  #define MDFN_FORMATSTR(a,b,c)

  #define MDFN_WARN_UNUSED_RESULT

#else
  #error "Not compiling with GCC nor MSVC"
  #define INLINE inline
  #define NO_INLINE

  #define MDFN_FASTCALL

  #define MDFN_ALIGN(n)	// hence the #error.

  #define MDFN_FORMATSTR(a,b,c)

  #define MDFN_WARN_UNUSED_RESULT

#endif


typedef struct
{
 union
 {
  struct
  {
   #ifdef MSB_FIRST
   uint8   High;
   uint8   Low;
   #else
   uint8   Low;
   uint8   High;
   #endif
  } Union8;
  uint16 Val16;
 };
} Uuint16;

typedef struct
{
 union
 {
  struct
  {
   #ifdef MSB_FIRST
   Uuint16   High;
   Uuint16   Low;
   #else
   Uuint16   Low;
   Uuint16   High;
   #endif
  } Union16;
  uint32  Val32;
 };
} Uuint32;


#if PSS_STYLE==2

#define PSS "\\"
#define MDFN_PS '\\'

#elif PSS_STYLE==1

#define PSS "/"
#define MDFN_PS '/'

#elif PSS_STYLE==3

#define PSS "\\"
#define MDFN_PS '\\'

#elif PSS_STYLE==4

#define PSS ":" 
#define MDFN_PS ':'

#endif

typedef uint32   UTF32;  /* at least 32 bits */
typedef uint16  UTF16;  /* at least 16 bits */
typedef uint8   UTF8;   /* typically 8 bits */
typedef unsigned char   Boolean; /* 0 or 1 */

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#undef require
#define require( expr ) assert( expr )

#if !defined(MSB_FIRST) && !defined(LSB_FIRST)
 #error "Define MSB_FIRST or LSB_FIRST!"
#endif

#include "error.h"

#endif
