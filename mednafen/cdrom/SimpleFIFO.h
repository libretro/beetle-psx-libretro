#ifndef __MDFN_SIMPLEFIFO_H
#define __MDFN_SIMPLEFIFO_H

#include "../math_ops.h"

class SimpleFIFO
{
 public:

 // Constructor
 SimpleFIFO(uint32 the_size)
 {
    /* Size should be a power of 2! */
    data      = (uint8*)malloc(the_size * sizeof(uint8));
    size      = the_size;
    read_pos  = 0;
    write_pos = 0;
    in_count  = 0;
 }

 // Destructor
 INLINE ~SimpleFIFO()
 {
    if (data)
       free(data);
 }

 INLINE uint32 CanWrite(void)
 {
  return(size - in_count);
 }

 INLINE uint8 ReadByte(void)
 {
    uint8 ret = data[read_pos];
    read_pos  = (read_pos + 1) & (size - 1);
    in_count--;
    return(ret);
 }

 INLINE void Write(const uint8 *happy_data, uint32 happy_count)
 {
    while(happy_count)
    {
       data[write_pos] = *happy_data;

       write_pos = (write_pos + 1) & (size - 1);
       in_count++;
       happy_data++;
       happy_count--;
    }
 }

 INLINE void WriteByte(const uint8 wr_data)
 {
    data[write_pos] = wr_data;
    write_pos       = (write_pos + 1) & (size - 1);
    in_count++;
 }


 INLINE void Flush(void)
 {
  read_pos = 0;
  write_pos = 0;
  in_count = 0;
 }

 INLINE void SaveStatePostLoad(void)
 {
    read_pos  %= size;
    write_pos %= size;
    in_count  %= (size + 1);
 }

 uint8* data;
 uint32 size;
 uint32 read_pos; // Read position
 uint32 write_pos; // Write position
 uint32 in_count; // Number of units in the FIFO
};


#endif
