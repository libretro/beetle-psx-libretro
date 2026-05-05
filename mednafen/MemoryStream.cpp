#include <stdint.h>
#include "MemoryStream.h"
#include "error.h"
#include <compat/msvc.h>
#include "math_ops.h"

/* All TODOs from the original Mednafen import have been resolved by
 * the audit pass:
 *   - Write/Seek expansion that fails no longer corrupts state:
 *     grow_if_necessary keeps the original buffer on realloc failure
 *     and write() clamps to whatever's actually available.
 *   - The unused pointer-copy ctor and the declared-but-undefined
 *     operator= overload were removed - neither was ever called and
 *     both had broken signatures (pointer args instead of references). */
MemoryStream::MemoryStream() : data_buffer(NULL), data_buffer_size(0), data_buffer_alloced(0), position(0)
{
   data_buffer_alloced = 64;
   data_buffer         = (uint8*)malloc((size_t)data_buffer_alloced);
   if (!data_buffer)
   {
      MDFN_Error(0, "MemoryStream: out of memory (default ctor, %llu bytes)",
            (unsigned long long)data_buffer_alloced);
      data_buffer_alloced = 0;
   }
}

MemoryStream::MemoryStream(uint64 size_hint) : data_buffer(NULL), data_buffer_size(0), data_buffer_alloced(0), position(0)
{
   data_buffer_alloced = (size_hint > SIZE_MAX) ? SIZE_MAX : size_hint;
   if (data_buffer_alloced == 0)
      return;

   data_buffer = (uint8*)malloc((size_t)data_buffer_alloced);
   if (!data_buffer)
   {
      MDFN_Error(0, "MemoryStream: out of memory (size_hint ctor, %llu bytes)",
            (unsigned long long)data_buffer_alloced);
      data_buffer_alloced = 0;
   }
}

/* Read all contents of `stream` into a new in-memory buffer, then close
 * and delete `stream` regardless of success. is_valid() returns false
 * if the input was empty, the allocation failed, or the underlying read
 * was short - in any of those cases callers should treat construction
 * as having failed. */
MemoryStream::MemoryStream(Stream *stream) : data_buffer(NULL), data_buffer_size(0), data_buffer_alloced(0), position(0)
{
   uint64 want;

   if (!stream)
      return;

   if((position = stream->tell()) != 0)
      stream->seek(0, SEEK_SET);

   data_buffer_size    = stream->size();
   data_buffer_alloced = data_buffer_size;

   if (data_buffer_alloced > 0)
   {
      data_buffer = (uint8*)malloc((size_t)data_buffer_alloced);
      if (!data_buffer)
      {
         MDFN_Error(0, "MemoryStream: out of memory (Stream ctor, %llu bytes)",
               (unsigned long long)data_buffer_alloced);
         data_buffer_alloced = 0;
         data_buffer_size    = 0;
      }
      else
      {
         want = data_buffer_size;
         if (stream->read(data_buffer, want) != want)
         {
            MDFN_Error(0, "MemoryStream: short read on Stream ctor");
            free(data_buffer);
            data_buffer        = NULL;
            data_buffer_size   = 0;
            data_buffer_alloced = 0;
         }
      }
   }

   stream->close();
   delete stream;
}

MemoryStream::~MemoryStream()
{
 if(data_buffer)
 {
  free(data_buffer);
  data_buffer = NULL;
 }
}

uint8 *MemoryStream::map(void)
{
 return data_buffer;
}

void MemoryStream::unmap(void)
{

}


/* Grow the buffer to at least new_required_size bytes. On failure
 * (allocation failed) the buffer state is left untouched and write()
 * callers will short-write. There's no exception path - errors are
 * absorbed into a short-write that the caller must detect. */
INLINE void MemoryStream::grow_if_necessary(uint64 new_required_size)
{
   if (new_required_size <= data_buffer_size)
      return;

   if (new_required_size > data_buffer_alloced)
   {
      uint64 new_required_alloced = round_up_pow2(new_required_size);
      uint8 *new_data_buffer;

      /* First condition will happen at new_required_size > (UINT64_C(1) << 63)
       * due to round_up_pow2() "wrapping". Second can occur on 32-bit. */
      if (new_required_alloced < new_required_size || new_required_alloced > SIZE_MAX)
         new_required_alloced = SIZE_MAX;

      new_data_buffer = (uint8*)realloc(data_buffer, (size_t)new_required_alloced);
      if (!new_data_buffer)
      {
         /* realloc leaves the original pointer valid on failure - keep it,
          * log the issue, and bail. data_buffer_size and _alloced stay
          * consistent with the still-live buffer. */
         MDFN_Error(0, "MemoryStream: realloc failed (%llu bytes), retaining old buffer",
               (unsigned long long)new_required_alloced);
         return;
      }

      data_buffer        = new_data_buffer;
      data_buffer_alloced = new_required_alloced;
   }

   data_buffer_size = new_required_size;
}

uint64 MemoryStream::read(void *data, uint64 count)
{
   uint64 avail;

   if (!data_buffer || position < 0 || (uint64)position >= data_buffer_size)
      return 0;

   avail = data_buffer_size - (uint64)position;
   if (count > avail)
      count = avail;

   memcpy(data, &data_buffer[position], (size_t)count);
   position += count;

   return count;
}

void MemoryStream::write(const void *data, uint64 count)
{
   uint64 nrs;

   if (position < 0)
      return;

   nrs = (uint64)position + count;
   grow_if_necessary(nrs);

   /* If grow_if_necessary failed, clamp to whatever space we have. */
   if ((uint64)position >= data_buffer_size)
      return;
   if (count > data_buffer_size - (uint64)position)
      count = data_buffer_size - (uint64)position;

   memcpy(&data_buffer[position], data, (size_t)count);
   position += count;
}

void MemoryStream::seek(int64 offset, int whence)
{
   int64 new_position;

   switch(whence)
   {
      case SEEK_SET:
         new_position = offset;
         break;

      case SEEK_CUR:
         new_position = position + offset;
         break;

      case SEEK_END:
         new_position = (int64)data_buffer_size + offset;
         break;

      default:
         /* Unknown whence - leave position alone. */
         return;
   }

   if (new_position < 0)
      return;

   grow_if_necessary((uint64)new_position);
   position = new_position;
}

uint64_t MemoryStream::tell(void)
{
 return position;
}

uint64_t MemoryStream::size(void)
{
 return data_buffer_size;
}

void MemoryStream::close(void)
{

}

int MemoryStream::get_line(std::string &str)
{
   str.clear();

   if (position < 0)
      return -1;

   while ((uint64)position < data_buffer_size)
   {
      uint8 c = data_buffer[position++];

      if(c == '\r' || c == '\n' || c == 0)
         return(c);

      str.push_back(c);
   }

   return(-1);
}

