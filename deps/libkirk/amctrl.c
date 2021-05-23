// Copyright (C) 2013       tpu
// Copyright (C) 2015       Hykem <hykem@hotmail.com>
// Licensed under the terms of the GNU GPL, version 3
// http://www.gnu.org/licenses/gpl-3.0.txt

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kirk_engine.h"
#include "amctrl.h"
#include "aes.h"

// KIRK buffer.
static u8 kirk_buf[0x0814];

// AMCTRL keys.
static u8 amctrl_key1[0x10] = {0xE3, 0x50, 0xED, 0x1D, 0x91, 0x0A, 0x1F, 0xD0, 0x29, 0xBB, 0x1C, 0x3E, 0xF3, 0x40, 0x77, 0xFB};
static u8 amctrl_key2[0x10] = {0x13, 0x5F, 0xA4, 0x7C, 0xAB, 0x39, 0x5B, 0xA4, 0x76, 0xB8, 0xCC, 0xA9, 0x8F, 0x3A, 0x04, 0x45};
static u8 amctrl_key3[0x10] = {0x67, 0x8D, 0x7F, 0xA3, 0x2A, 0x9C, 0xA0, 0xD1, 0x50, 0x8A, 0xD8, 0x38, 0x5E, 0x4B, 0x01, 0x7E};

// sceNpDrmGetFixedKey keys.
static u8 npdrm_enc_keys[0x30] = {
	0x07, 0x3D, 0x9E, 0x9D, 0xA8, 0xFD, 0x3B, 0x2F, 0x63, 0x18, 0x93, 0x2E, 0xF8, 0x57, 0xA6, 0x64,
	0x37, 0x49, 0xB7, 0x01, 0xCA, 0xE2, 0xE0, 0xC5, 0x44, 0x2E, 0x06, 0xB6, 0x1E, 0xFF, 0x84, 0xF2,
	0x9D, 0x31, 0xB8, 0x5A, 0xC8, 0xFA, 0x16, 0x80, 0x73, 0x60, 0x18, 0x82, 0x18, 0x77, 0x91, 0x9D,
};
static u8 npdrm_fixed_key[0x10] = {
	0x38, 0x20, 0xD0, 0x11, 0x07, 0xA3, 0xFF, 0x3E, 0x0A, 0x4C, 0x20, 0x85, 0x39, 0x10, 0xB5, 0x54,
};

/*
	KIRK wrapper functions.
*/
static int kirk4(u8 *buf, int size, int type)
{
	int retv;
	u32 *header = (u32*)buf;

	header[0] = 4;
	header[1] = 0;
	header[2] = 0;
	header[3] = type;
	header[4] = size;

	retv = sceUtilsBufferCopyWithRange(buf, size + 0x14, buf, size, 4);

	if (retv)
		return 0x80510311;

	return 0;
}

static int kirk7(u8 *buf, int size, int type)
{
	int retv;
	u32 *header = (u32*)buf;

	header[0] = 5;
	header[1] = 0;
	header[2] = 0;
	header[3] = type;
	header[4] = size;

	retv = sceUtilsBufferCopyWithRange(buf, size + 0x14, buf, size, 7);
	
	if (retv)
		return 0x80510311;

	return 0;
}

static int kirk5(u8 *buf, int size)
{
	int retv;
	u32 *header = (u32*)buf;

	header[0] = 4;
	header[1] = 0;
	header[2] = 0;
	header[3] = 0x0100;
	header[4] = size;

	retv = sceUtilsBufferCopyWithRange(buf, size + 0x14, buf, size, 5);
	
	if (retv)
		return 0x80510312;

	return 0;
}

static int kirk8(u8 *buf, int size)
{
	int retv;
	u32 *header = (u32*)buf;

	header[0] = 5;
	header[1] = 0;
	header[2] = 0;
	header[3] = 0x0100;
	header[4] = size;

	retv = sceUtilsBufferCopyWithRange(buf, size+0x14, buf, size, 8);
	
	if (retv)
		return 0x80510312;

	return 0;
}

static int kirk14(u8 *buf)
{
	int retv;

	retv = sceUtilsBufferCopyWithRange(buf, 0x14, 0, 0, 14);
	
	if (retv)
		return 0x80510315;

	return 0;
}

/*
	Internal functions.
*/
static int encrypt_buf(u8 *buf, int size, u8 *key, int key_type)
{
	int i, retv;

	for (i = 0; i < 16; i++) {
		buf[0x14+i] ^= key[i];
	}

	retv = kirk4(buf, size, key_type);
	
	if (retv)
		return retv;

	memcpy(key, buf + size + 4, 16);

	return 0;
}

static int decrypt_buf(u8 *buf, int size, u8 *key, int key_type)
{
	int i, retv;
	u8 tmp[16];

	memcpy(tmp, buf + size + 0x14 - 16, 16);

	retv = kirk7(buf, size, key_type);
	
	if (retv)
		return retv;

	for (i = 0; i < 16; i++) {
		buf[i] ^= key[i];
	}

	memcpy(key, tmp, 16);

	return 0;
}

static int cipher_buf(u8 *kbuf, u8 *dbuf, int size, CIPHER_KEY *ckey)
{
	int i, retv;
	u8 tmp1[16], tmp2[16];

	memcpy(kbuf + 0x14, ckey->key, 16);

	for (i = 0; i < 16; i++) {
		kbuf[0x14 + i] ^= amctrl_key3[i];
	}

	if (ckey->type == 2)
		retv = kirk8(kbuf, 16);
	else
		retv = kirk7(kbuf, 16, 0x39);
	
	if (retv)
		return retv;

	for (i = 0; i < 16; i++) {
		kbuf[i] ^= amctrl_key2[i];
	}

	memcpy(tmp2, kbuf, 0x10);

	if (ckey->seed == 1) {
		memset(tmp1, 0, 0x10);
	} else {
		memcpy(tmp1, tmp2, 0x10);
		*(u32*)(tmp1 + 0x0c) = ckey->seed - 1;
	}

	for (i = 0; i < size; i += 16) {
		memcpy(kbuf + 0x14 + i, tmp2, 12);
		*(u32*)(kbuf + 0x14 + i + 12) = ckey->seed;
		ckey->seed += 1;
	}

	retv = decrypt_buf(kbuf, size, tmp1, 0x63);
	
	if (retv)
		return retv;

	for (i = 0; i < size; i++) {
		dbuf[i] ^= kbuf[i];
	}

	return 0;
}

/*
	BBMac functions.
*/
int sceDrmBBMacInit(MAC_KEY *mkey, int type)
{
	mkey->type = type;
	mkey->pad_size = 0;

	memset(mkey->key, 0, 16);
	memset(mkey->pad, 0, 16);

	return 0;
}

int sceDrmBBMacUpdate(MAC_KEY *mkey, u8 *buf, int size)
{
	int retv = 0, ksize, p, type;
	u8 *kbuf;

	if (mkey->pad_size > 16) {
		retv = 0x80510302;
		goto _exit;
	}

	if (mkey->pad_size + size <= 16) {
		memcpy(mkey->pad + mkey->pad_size, buf, size);
		mkey->pad_size += size;
		retv = 0;
	} else {
		kbuf = kirk_buf + 0x14;
		memcpy(kbuf, mkey->pad, mkey->pad_size);

		p = mkey->pad_size;

		mkey->pad_size += size;
		mkey->pad_size &= 0x0f;
		if (mkey->pad_size == 0)
			mkey->pad_size = 16;

		size -= mkey->pad_size;
		memcpy(mkey->pad, buf + size, mkey->pad_size);

		type = (mkey->type == 2) ? 0x3A : 0x38;

		while (size)
		{
			ksize = (size + p >= 0x0800) ? 0x0800 : size + p;
			memcpy(kbuf + p, buf, ksize - p);
			retv = encrypt_buf(kirk_buf, ksize, mkey->key, type);
			
			if (retv)
				goto _exit;
			
			size -= (ksize - p);
			buf += ksize - p;
			p = 0;
		}
	}

_exit:
	return retv;

}

int sceDrmBBMacFinal(MAC_KEY *mkey, u8 *buf, u8 *vkey)
{
	int i, retv, code;
	u8 *kbuf, tmp[16], tmp1[16];
	u32 t0, v0, v1;

	if (mkey->pad_size > 16)
		return 0x80510302;

	code = (mkey->type == 2) ? 0x3A : 0x38;
	kbuf = kirk_buf + 0x14;

	memset(kbuf, 0, 16);
	retv = kirk4(kirk_buf, 16, code);
	
	if (retv)
		goto _exit;
	
	memcpy(tmp, kbuf, 16);

	t0 = (tmp[0] & 0x80) ? 0x87 : 0;
	for (i = 0; i < 15; i++)
	{
		v1 = tmp[i + 0];
		v0 = tmp[i + 1];
		v1 <<= 1;
		v0 >>= 7;
		v0 |= v1;
		tmp[i + 0] = v0;
	}
	v0 = tmp[15];
	v0 <<= 1;
	v0 ^= t0;
	tmp[15] = v0;

	if (mkey->pad_size < 16)
	{
		t0 = (tmp[0] & 0x80) ? 0x87 : 0;
		for (i = 0; i < 15; i++)
		{
			v1 = tmp[i + 0];
			v0 = tmp[i + 1];
			v1 <<= 1;
			v0 >>= 7;
			v0 |= v1;
			tmp[i + 0] = v0;
		}
		v0 = tmp[15];
		v0 <<= 1;
		v0 ^= t0;
		tmp[15] = v0;

		mkey->pad[mkey->pad_size] = 0x80;
		if (mkey->pad_size + 1 < 16)
			memset(mkey->pad + mkey->pad_size + 1, 0, 16 - mkey->pad_size - 1);
	}

	for (i = 0; i < 16; i++) {
		mkey->pad[i] ^= tmp[i];
	}

	memcpy(kbuf, mkey->pad, 16);
	memcpy(tmp1, mkey->key, 16);

	retv = encrypt_buf(kirk_buf, 0x10, tmp1, code);
	
	if (retv)
		return retv;

	for (i = 0; i < 0x10; i++) {
		tmp1[i] ^= amctrl_key1[i];
	}

	if (mkey->type == 2)
	{
		memcpy(kbuf, tmp1, 16);

		retv = kirk5(kirk_buf, 0x10);

		if (retv)
			goto _exit;

		retv = kirk4(kirk_buf, 0x10, code);

		if (retv)
			goto _exit;

		memcpy(tmp1, kbuf, 16);
	}

	if (vkey)
	{
		for (i = 0; i < 0x10; i++) {
			tmp1[i] ^= vkey[i];
		}
		memcpy(kbuf, tmp1, 16);

		retv = kirk4(kirk_buf, 0x10, code);
		
		if (retv)
			goto _exit;

		memcpy(tmp1, kbuf, 16);
	}

	memcpy(buf, tmp1, 16);

	memset(mkey->key, 0, 16);
	memset(mkey->pad, 0, 16);

	mkey->pad_size = 0;
	mkey->type = 0;
	retv = 0;

_exit:
	return retv;
}

int sceDrmBBMacFinal2(MAC_KEY *mkey, u8 *out, u8 *vkey)
{
	int i, retv, type;
	u8 *kbuf, tmp[16];

	type = mkey->type;
	retv = sceDrmBBMacFinal(mkey, tmp, vkey);
	if (retv)
		return retv;

	kbuf = kirk_buf+0x14;

	if (type == 3) {
		memcpy(kbuf, out, 0x10);
		kirk7(kirk_buf, 0x10, 0x63);
	} else {
		memcpy(kirk_buf, out, 0x10);
	}

	retv = 0;
	for (i = 0; i < 0x10; i++) {
		if (kirk_buf[i] != tmp[i]) {
			retv = 0x80510300;
			break;
		}
	}

	return retv;
}

/*
	BBCipher functions.
*/
int sceDrmBBCipherInit(CIPHER_KEY *ckey, int type, int mode, u8 *header_key, u8 *version_key, u32 seed)
{
	int i, retv;
	u8 *kbuf;

	kbuf = kirk_buf + 0x14;
	ckey->type = type;
	if (mode == 2)
	{
		ckey->seed = seed + 1;
		for (i = 0; i < 16; i++) {
			ckey->key[i] = header_key[i];
		}
		if (version_key) {
			for (i = 0; i < 16; i++) {
				ckey->key[i] ^= version_key[i];
			}
		}
		retv = 0;
	}
	else if (mode == 1)
	{
		ckey->seed = 1;
		retv = kirk14(kirk_buf);
		
		if (retv)
			return retv;

		memcpy(kbuf, kirk_buf, 0x10);
		memset(kbuf + 0x0c, 0, 4);

		if (ckey->type == 2)
		{
			for (i = 0; i < 16; i++) {
				kbuf[i] ^= amctrl_key2[i];
			}
			retv = kirk5(kirk_buf, 0x10);
			for (i = 0; i < 16; i++) {
				kbuf[i] ^= amctrl_key3[i];
			}
		}
		else
		{
			for (i = 0; i < 16; i++) {
				kbuf[i] ^= amctrl_key2[i];
			}
			retv = kirk4(kirk_buf, 0x10, 0x39);
			for(i = 0; i < 16; i++) {
				kbuf[i] ^= amctrl_key3[i];
			}
		}
		
		if (retv)
			return retv;

		memcpy(ckey->key, kbuf, 0x10);
		memcpy(header_key, kbuf, 0x10);

		if (version_key) 
		{
			for (i = 0; i < 16; i++) {
				ckey->key[i] ^= version_key[i];
			}
		}
	}
	else
	{
		retv = 0;
	}

	return retv;
}

int sceDrmBBCipherUpdate(CIPHER_KEY *ckey, u8 *data, int size)
{
	int p, retv, dsize;

	retv = 0;
	p = 0;

	while (size > 0)
	{
		dsize = (size >= 0x0800) ? 0x0800 : size;
		retv = cipher_buf(kirk_buf, data + p, dsize, ckey);
		
		if (retv)
			break;
		
		size -= dsize;
		p += dsize;
	}

	return retv;
}

int sceDrmBBCipherFinal(CIPHER_KEY *ckey)
{
	memset(ckey->key, 0, 16);
	ckey->type = 0;
	ckey->seed = 0;

	return 0;
}

/*
	Extra functions.
*/
int bbmac_build_final2(int type, u8 *mac)
{
	u8 *kbuf = kirk_buf + 0x14;

	if (type == 3)
	{
		memcpy(kbuf, mac, 16);
		kirk4(kirk_buf, 0x10, 0x63);
		memcpy(mac, kbuf, 16);
	}

	return 0;
}

int bbmac_getkey(MAC_KEY *mkey, u8 *bbmac, u8 *vkey)
{
	int i, retv, type, code;
	u8 *kbuf, tmp[16], tmp1[16];

	type = mkey->type;
	retv = sceDrmBBMacFinal(mkey, tmp, NULL);
	
	if (retv)
		return retv;

	kbuf = kirk_buf + 0x14;

	if (type == 3) {
		memcpy(kbuf, bbmac, 0x10);
		kirk7(kirk_buf, 0x10, 0x63);
	} else {
		memcpy(kirk_buf, bbmac, 0x10);
	}

	memcpy(tmp1, kirk_buf, 16);
	memcpy(kbuf, tmp1, 16);

	code = (type == 2) ? 0x3A : 0x38;
	kirk7(kirk_buf, 0x10, code);

	for (i = 0; i < 0x10; i++) {
		vkey[i] = tmp[i] ^ kirk_buf[i];
	}

	return 0;
}

int bbmac_forge(MAC_KEY *mkey, u8 *bbmac, u8 *vkey, u8 *buf)
{
	int i, retv, type;
	u8 *kbuf, tmp[16], tmp1[16];
	u32 t0, v0, v1;

	if (mkey->pad_size > 16)
		return 0x80510302;

	type = (mkey->type == 2) ? 0x3A : 0x38;
	kbuf = kirk_buf + 0x14;

	memset(kbuf, 0, 16);
	retv = kirk4(kirk_buf, 16, type);
	
	if (retv)
		return retv;
	
	memcpy(tmp, kbuf, 16);

	t0 = (tmp[0] & 0x80) ? 0x87 : 0;
	for (i = 0; i < 15; i++)
	{
		v1 = tmp[i + 0];
		v0 = tmp[i + 1];
		v1 <<= 1;
		v0 >>= 7;
		v0 |= v1;
		tmp[i + 0] = v0;
	}
	v0 = tmp[15];
	v0 <<= 1;
	v0 ^= t0;
	tmp[15] = v0;

	if (mkey->pad_size < 16)
	{
		t0 = (tmp[0] & 0x80) ? 0x87 : 0;
		for (i = 0; i < 15; i++)
		{
			v1 = tmp[i + 0];
			v0 = tmp[i + 1];
			v1 <<= 1;
			v0 >>= 7;
			v0 |= v1;
			tmp[i + 0] = v0;
		}
		v0 = tmp[15];
		v0 <<= 1;
		v0 ^= t0;
		tmp[15] = t0;

		mkey->pad[mkey->pad_size] = 0x80;
		if (mkey->pad_size + 1 < 16)
			memset(mkey->pad+mkey->pad_size + 1, 0, 16 - mkey->pad_size - 1);
	}

	for (i = 0; i < 16; i++) {
		mkey->pad[i] ^= tmp[i];
	}
	for (i = 0; i < 0x10; i++) {
		mkey->pad[i] ^= mkey->key[i];
	}

	memcpy(kbuf, bbmac, 0x10);
	kirk7(kirk_buf, 0x10, 0x63);

	memcpy(kbuf, kirk_buf, 0x10);
	kirk7(kirk_buf, 0x10, type);

	memcpy(tmp1, kirk_buf, 0x10);
	for (i = 0; i < 0x10; i++) {
		tmp1[i] ^= vkey[i];
	}
	for (i = 0; i < 0x10; i++) {
		tmp1[i] ^= amctrl_key1[i];
	}

	memcpy(kbuf, tmp1, 0x10);
	kirk7(kirk_buf, 0x10, type);

	memcpy(tmp1, kirk_buf, 0x10);
	for (i = 0; i < 16; i++) {
		mkey->pad[i] ^= tmp1[i];
	}

	for (i = 0; i < 16; i++) {
		buf[i] ^= mkey->pad[i];
	}

	return 0;
}

/*
	sceNpDrm functions.
*/
int sceNpDrmGetFixedKey(u8 *key, char *npstr, int type)
{
	AES_ctx akey;
	MAC_KEY mkey;
	char strbuf[0x30];
	int retv;

	if ((type & 0x01000000) == 0)
		return 0x80550901;
	
	type &= 0x000000ff;

	memset(strbuf, 0, 0x30);
	strncpy(strbuf, npstr, 0x30);

	retv = sceDrmBBMacInit(&mkey, 1);
	
	if (retv)
		return retv;

	retv = sceDrmBBMacUpdate(&mkey, (u8*)strbuf, 0x30);
	
	if (retv)
		return retv;

	retv = sceDrmBBMacFinal(&mkey, key, npdrm_fixed_key);
	
	if (retv)
		return 0x80550902;

	if (type == 0)
		return 0;
	if (type > 3)
		return 0x80550901;
	
	type = (type - 1) * 16;

	AES_set_key(&akey, &npdrm_enc_keys[type], 128);
	AES_encrypt(&akey, key, key);

	return 0;
}