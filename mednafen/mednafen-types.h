#ifndef __MDFN_TYPES_H
#define __MDFN_TYPES_H

#ifdef NOT_LIBRETRO
// Make sure this file is included BEFORE a few common standard C header files(stdio.h, errno.h, math.h, AND OTHERS, but this is not an exhaustive check, nor
// should it be), so that any defines in config.h that change header file behavior will work properly.
#if defined(EOF) || defined(EACCES) || defined(F_LOCK) || defined(NULL) || defined(O_APPEND) || defined(M_LOG2E)
 #error "Wrong include order for types.h"
#endif

#endif /* NOT_LIBRETRO */

#ifdef HAVE_CONFIG_H
 #include <config.h>
#endif

//
//
//

#ifdef NOT_LIBRETRO
#if defined(__PIC__) || defined(__pic__) || defined(__PIE__) || defined(__pie__)
 #if defined(__386__) || defined(__i386__) || defined(__i386) || defined(_M_IX86) || defined(_M_I386) //|| (SIZEOF_VOID_P <= 4)
  #error "Compiling with position-independent code generation enabled is not recommended, for performance reasons."
 #else
  #warning "Compiling with position-independent code generation enabled is not recommended, for performance reasons."
 #endif
#endif
#endif /* NOT_LIBRETRO */

#if defined(__x86_64__) && defined(__code_model_large__)
 #error "Compiling with large memory model is not recommended, for performance reasons."
#endif
//
//
//

#include <stddef.h>
#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#ifdef __cplusplus
#include <limits>
#include <exception>
#include <stdexcept>
#include <utility>
#include <memory>
#include <algorithm>
#include <string>
#include <vector>
#include <list>
#endif

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;


#if !defined(HAVE_NATIVE64BIT) && (SIZEOF_VOID_P >= 8 || defined(__x86_64__))
#define HAVE_NATIVE64BIT 1
#endif

#if defined(__GNUC__) || defined(__clang__) || defined(__ICC) || defined(__INTEL_COMPILER)
 #define HAVE_COMPUTED_GOTO 1
#endif

#if defined(__clang__)
  //
  // Begin clang
  //
  #define MDFN_MAKE_CLANGV(maj,min,pl) (((maj)*100*100) + ((min) * 100) + (pl))
  #define MDFN_CLANG_VERSION	MDFN_MAKE_CLANGV(__clang_major__, __clang_minor__, __clang_patchlevel__)

  #ifndef INLINE
  #define INLINE inline __attribute__((always_inline))
  #endif
  #define NO_INLINE __attribute__((noinline))
  #define NO_CLONE

  #if defined(__386__) || defined(__i386__) || defined(__i386) || defined(_M_IX86) || defined(_M_I386)
    #define MDFN_FASTCALL __attribute__((fastcall))
  #else
    #define MDFN_FASTCALL
  #endif

  #define MDFN_FORMATSTR(a,b,c)
  #define MDFN_WARN_UNUSED_RESULT __attribute__ ((warn_unused_result))
  #define MDFN_NOWARN_UNUSED __attribute__((unused))

  #define MDFN_UNLIKELY(n) __builtin_expect((n) != 0, 0)
  #define MDFN_LIKELY(n) __builtin_expect((n) != 0, 1)

  #define MDFN_COLD __attribute__((cold))
  #define MDFN_HOT __attribute__((hot))

  #if MDFN_CLANG_VERSION >= MDFN_MAKE_CLANGV(3,6,0) && defined(HAS_CXX11)
   #define MDFN_ASSUME_ALIGNED(p, align) ((decltype(p))__builtin_assume_aligned((p), (align)))
  #else
   #define MDFN_ASSUME_ALIGNED(p, align) (p)
  #endif
#elif defined(__GNUC__)
  //
  // Begin gcc
  //
  #define MDFN_MAKE_GCCV(maj,min,pl) (((maj)*100*100) + ((min) * 100) + (pl))
  #define MDFN_GCC_VERSION	MDFN_MAKE_GCCV(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__)

  #ifndef INLINE
  #define INLINE inline __attribute__((always_inline))
  #endif

  #define NO_INLINE __attribute__((noinline))

  #if MDFN_GCC_VERSION >= MDFN_MAKE_GCCV(4,5,0)
   #define NO_CLONE __attribute__((noclone))
  #else
   #define NO_CLONE
  #endif

  #if MDFN_GCC_VERSION < MDFN_MAKE_GCCV(4,8,0)
   #define alignas(n) __attribute__ ((aligned (n)))	// Kludge for 4.7.x, remove eventually when 4.8+ are not so new.
  #endif

  //
  // Just avoid using fastcall with gcc before 4.1.0, as it(and similar regparm)
  // tend to generate bad code on the older versions(between about 3.1.x and 4.0.x, at least)
  //
  // http://gcc.gnu.org/bugzilla/show_bug.cgi?id=12236
  // http://gcc.gnu.org/bugzilla/show_bug.cgi?id=7574
  // http://gcc.gnu.org/bugzilla/show_bug.cgi?id=17025
  //
  #if MDFN_GCC_VERSION >= MDFN_MAKE_GCCV(4,1,0)
   #if defined(__386__) || defined(__i386__) || defined(__i386) || defined(_M_IX86) || defined(_M_I386)
     #define MDFN_FASTCALL __attribute__((fastcall))
   #else
     #define MDFN_FASTCALL
   #endif
  #else
   #define MDFN_FASTCALL
  #endif

  #define MDFN_FORMATSTR(a,b,c) __attribute__ ((format (a, b, c)))
  #define MDFN_WARN_UNUSED_RESULT __attribute__ ((warn_unused_result))
  #define MDFN_NOWARN_UNUSED __attribute__((unused))

  #define MDFN_UNLIKELY(n) __builtin_expect((n) != 0, 0)
  #define MDFN_LIKELY(n) __builtin_expect((n) != 0, 1)

  #if MDFN_GCC_VERSION >= MDFN_MAKE_GCCV(4,3,0)
   #define MDFN_COLD __attribute__((cold))
   #define MDFN_HOT __attribute__((hot))
  #else
   #define MDFN_COLD
   #define MDFN_HOT
  #endif

  #if MDFN_GCC_VERSION >= MDFN_MAKE_GCCV(4,7,0) && defined(HAS_CXX11)
   #define MDFN_ASSUME_ALIGNED(p, align) ((decltype(p))__builtin_assume_aligned((p), (align)))
  #else
   #define MDFN_ASSUME_ALIGNED(p, align) (p)
  #endif
#elif defined(_MSC_VER)
  //
  // Begin MSVC
  //

  #if !defined(INLINE)
  #define INLINE __forceinline
  #endif

  #define NO_INLINE __declspec(noinline)
  #define NO_CLONE

  #define MDFN_FASTCALL __fastcall

  #define MDFN_FORMATSTR(a,b,c)

  #define MDFN_WARN_UNUSED_RESULT

  #define MDFN_NOWARN_UNUSED

  #define MDFN_UNLIKELY(n) ((n) != 0)
  #define MDFN_LIKELY(n) ((n) != 0)

  #define MDFN_COLD
  #define MDFN_HOT

  #define MDFN_ASSUME_ALIGNED(p, align) (p)
#else
  #define INLINE inline
  #define NO_INLINE
  #define NO_CLONE

  #define MDFN_FASTCALL

  #define MDFN_FORMATSTR(a,b,c)

  #define MDFN_WARN_UNUSED_RESULT

  #define MDFN_NOWARN_UNUSED

  #define MDFN_UNLIKELY(n) ((n) != 0)
  #define MDFN_LIKELY(n) ((n) != 0)

  #define MDFN_COLD
  #define MDFN_HOT

  #define MDFN_ASSUME_ALIGNED(p, align) (p)
#endif

#ifndef FALSE
 #define FALSE 0
#endif

#ifndef TRUE
 #define TRUE 1
#endif

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define MSB_FIRST
#else
#define LSB_FIRST
#endif

#if !defined(MSB_FIRST) && !defined(LSB_FIRST)
 #error "Define MSB_FIRST or LSB_FIRST!"
#elif defined(MSB_FIRST) && defined(LSB_FIRST)
 #error "Define only one of MSB_FIRST or LSB_FIRST, not both!"
#endif

#ifdef LSB_FIRST
 #define MDFN_IS_BIGENDIAN false
#else
 #define MDFN_IS_BIGENDIAN true
#endif

#if NOT_LIBRETRO

#ifdef __cplusplus
template<typename T> typename std::remove_all_extents<T>::type* MDAP(T* v) { return (typename std::remove_all_extents<T>::type*)v; }
#include "error.h"
#include "math_ops.h"
#include "endian.h"
#endif
#endif /* NOT_LIBRETRO */

#ifndef _MSC_VER
#define MDFN_ALIGN(n)        __attribute__ ((aligned (n)))
#else
#define MDFN_ALIGN(n)        __declspec(align(n))
#endif

#endif
