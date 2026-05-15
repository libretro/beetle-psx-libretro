#ifndef __MDFN_SIMPLEFIFO_H
#define __MDFN_SIMPLEFIFO_H

#include "../math_ops.h"

class SimpleFIFO
{
 public:

 // Constructor
 SimpleFIFO(uint32_t the_size)
 {
    /* Size should be a power of 2! */
    data      = (uint8_t*)malloc(the_size * sizeof(uint8_t));
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

 INLINE uint32_t CanWrite(void)
 {
  return(size - in_count);
 }

 INLINE uint8_t ReadByte(void)
 {
    uint8_t ret = data[read_pos];
    read_pos  = (read_pos + 1) & (size - 1);
    in_count--;
    return(ret);
 }

 INLINE void Write(const uint8_t *happy_data, uint32_t happy_count)
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

 INLINE void WriteByte(const uint8_t wr_data)
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

 uint8_t* data;
 uint32_t size;
 uint32_t read_pos; // Read position
 uint32_t write_pos; // Write position
 uint32_t in_count; // Number of units in the FIFO
};


#endif
