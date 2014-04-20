#ifndef _S_CRC32_H
#define _S_CRC32_H

#ifdef __cplusplus
extern "C" {
#endif

unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned int len);

#ifdef __cplusplus
}
#endif

#endif
