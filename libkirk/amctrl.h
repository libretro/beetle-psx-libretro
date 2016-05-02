// Copyright (C) 2013       tpu
// Copyright (C) 2015       Hykem <hykem@hotmail.com>
// Licensed under the terms of the GNU GPL, version 3
// http://www.gnu.org/licenses/gpl-3.0.txt

#ifndef AMCTRL_H
#define AMCTRL_H

typedef struct
{
	int type;
	u8 key[16];
	u8 pad[16];
	int pad_size;
} MAC_KEY;

typedef struct
{
	u32 type;
	u32 seed;
	u8 key[16];
} CIPHER_KEY;

int sceDrmBBMacInit(MAC_KEY *mkey, int type);
int sceDrmBBMacUpdate(MAC_KEY *mkey, u8 *buf, int size);
int sceDrmBBMacFinal(MAC_KEY *mkey, u8 *buf, u8 *vkey);
int sceDrmBBMacFinal2(MAC_KEY *mkey, u8 *out, u8 *vkey);

int bbmac_build_final2(int type, u8 *mac);
int bbmac_getkey(MAC_KEY *mkey, u8 *bbmac, u8 *vkey);
int bbmac_forge(MAC_KEY *mkey, u8 *bbmac, u8 *vkey, u8 *buf);

int sceDrmBBCipherInit(CIPHER_KEY *ckey, int type, int mode, u8 *header_key, u8 *version_key, u32 seed);
int sceDrmBBCipherUpdate(CIPHER_KEY *ckey, u8 *data, int size);
int sceDrmBBCipherFinal(CIPHER_KEY *ckey);

int sceNpDrmGetFixedKey(u8 *key, char *npstr, int type);

#endif
