#ifndef _MD5_H
#define _MD5_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct md5_context
{
	uint32_t total[2];
	uint32_t state[4];
	uint8_t buffer[64];
};

void mednafen_md5_starts(struct md5_context *ctx);
void mednafen_md5_update_u32_as_lsb(struct md5_context *ctx, uint32_t input);
void mednafen_md5_update(struct md5_context *ctx, uint8_t *input, uint32_t length);
void mednafen_md5_finish(struct md5_context *ctx, uint8_t digest[16]);

/* Uses a static buffer, so beware of how it's used. */
char *mednafen_md5_asciistr(uint8_t digest[16]);

#ifdef __cplusplus
}
#endif

#endif	/* md5.h */
