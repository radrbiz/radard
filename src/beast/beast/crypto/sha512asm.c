/*
 * This file is a part of Pcompress, a chunked parallel multi-
 * algorithm lossless compression and decompression program.
 *
 * Copyright (C) 2012-2013 Moinak Ghosh. All rights reserved.
 * Use is subject to license terms.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *
 * moinakg@belenix.org, http://moinakg.wordpress.com/
 *      
 */

/*-
 * Copyright (c) 2001-2003 Allan Saddi <allan@saddi.com>
 * Copyright (c) 2012 Moinak Ghosh moinakg <at1> gm0il <dot> com
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Define WORDS_BIGENDIAN if compiling on a big-endian architecture.
 */
 
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#include <pthread.h>
#include <string.h>
#include "sha512asm.h"

//LITTLE ENDIAN ONLY

#if defined(__MINGW32__) || defined(__MINGW64__)

static __inline unsigned short
bswap_16 (unsigned short __x)
{
  return (__x >> 8) | (__x << 8);
}

static __inline unsigned int
bswap_32 (unsigned int __x)
{
  return (bswap_16 (__x & 0xffff) << 16) | (bswap_16 (__x >> 16));
}

static __inline unsigned long long
bswap_64 (unsigned long long __x)
{
  return (((unsigned long long) bswap_32 (__x & 0xffffffffull)) << 32) | (bswap_32 (__x >> 32));
}

#define BYTESWAP(x) bswap_32(x)
#define BYTESWAP64(x) bswap_64(x)

#elif defined(__APPLE__)

#include <libkern/OSByteOrder.h>

#define BYTESWAP(x) OSSwapBigToHostInt32(x)
#define BYTESWAP64(x) OSSwapBigToHostInt64(x)

#else

#include <endian.h> //glibc

#define BYTESWAP(x) be32toh(x)
#define BYTESWAP64(x) be64toh(x)

#endif /* defined(__MINGW32__) || defined(__MINGW64__) */

typedef void (*update_func_ptr)(const void *input_data, void *digest, uint64_t num_blks);

static const uint8_t padding[SHA512ASM_BLOCK_SIZE] = {
  0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint64_t iv512[SHA512ASM_HASH_WORDS] = {
  0x6a09e667f3bcc908LL,
  0xbb67ae8584caa73bLL,
  0x3c6ef372fe94f82bLL,
  0xa54ff53a5f1d36f1LL,
  0x510e527fade682d1LL,
  0x9b05688c2b3e6c1fLL,
  0x1f83d9abfb41bd6bLL,
  0x5be0cd19137e2179LL
};

static const uint64_t iv256[SHA512ASM_HASH_WORDS] = {
  0x22312194fc2bf72cLL,
  0x9f555fa3c84c64c2LL,
  0x2393b86b6f53b151LL,
  0x963877195940eabdLL,
  0x96283ee2a88effe3LL,
  0xbe5e1e2553863992LL,
  0x2b0199fc2c85b8aaLL,
  0x0eb72ddc81c52ca2LL
};

static update_func_ptr sha512_update_func;

void
Init_SHA512ASM_avx2 ()
{
	sha512_update_func = sha512_rorx;
}

void
Init_SHA512ASM_avx ()
{
	sha512_update_func = sha512_avx;
}

void
Init_SHA512ASM_sse4 ()
{
	sha512_update_func = sha512_sse4;
}

static void
_init (SHA512ASM_Context *sc, const uint64_t iv[SHA512ASM_HASH_WORDS])
{
	int i;

	sc->totalLength[0] = 0LL;
	sc->totalLength[1] = 0LL;
	for (i = 0; i < SHA512ASM_HASH_WORDS; i++)
		sc->hash[i] = iv[i];
	sc->bufferLength = 0L;
}

void
SHA512ASM_Init (SHA512ASM_Context *sc)
{
	_init (sc, iv512);
}

void
SHA512ASM_Update (SHA512ASM_Context *sc, const void *vdata, size_t len)
{
	const uint8_t *data = (const uint8_t *)vdata;
	uint32_t bufferBytesLeft;
	size_t bytesToCopy;
	int rem;
	uint64_t carryCheck;

	if (sc->bufferLength) {
		do {
			bufferBytesLeft = SHA512ASM_BLOCK_SIZE - sc->bufferLength;
			bytesToCopy = bufferBytesLeft;
			if (bytesToCopy > len)
				bytesToCopy = len;

			memcpy (&sc->buffer.bytes[sc->bufferLength], data, bytesToCopy);
			carryCheck = sc->totalLength[1];
			sc->totalLength[1] += bytesToCopy * 8L;
			if (sc->totalLength[1] < carryCheck)
				sc->totalLength[0]++;

			sc->bufferLength += bytesToCopy;
			data += bytesToCopy;
			len -= bytesToCopy;

			if (sc->bufferLength == SHA512ASM_BLOCK_SIZE) {
				sc->blocks = 1;
				sha512_update_func(sc->buffer.words, sc->hash, sc->blocks);
				sc->bufferLength = 0L;
			} else {
				return;
			}
		} while (len > 0 && len <= SHA512ASM_BLOCK_SIZE);
		if (!len) return;
	}
	sc->blocks = len >> 7;
	rem = len - (sc->blocks << 7);
	len = sc->blocks << 7;
	carryCheck = sc->totalLength[1];
	sc->totalLength[1] += rem * 8L;
	if (sc->totalLength[1] < carryCheck)
		sc->totalLength[0]++;

	if (len) {
		carryCheck = sc->totalLength[1];
		sc->totalLength[1] += len * 8L;
		if (sc->totalLength[1] < carryCheck)
			sc->totalLength[0]++;
		sha512_update_func((uint32_t *)data, sc->hash, sc->blocks);
	}
	if (rem) {
		memcpy (&sc->buffer.bytes[0], data + len, rem);
		sc->bufferLength = rem;
	}
}

static void
_final (SHA512ASM_Context *sc, uint8_t *hash, int hashWords, int halfWord)
{
	uint32_t bytesToPad;
	uint64_t lengthPad[2];
	int i;
	
	bytesToPad = 240L - sc->bufferLength;
	if (bytesToPad > SHA512ASM_BLOCK_SIZE)
		bytesToPad -= SHA512ASM_BLOCK_SIZE;
	
	lengthPad[0] = BYTESWAP64(sc->totalLength[0]);
	lengthPad[1] = BYTESWAP64(sc->totalLength[1]);
	
	SHA512ASM_Update (sc, padding, bytesToPad);
	SHA512ASM_Update (sc, lengthPad, 16L);
	
	if (hash) {
		for (i = 0; i < hashWords; i++) {
			*((uint64_t *) hash) = BYTESWAP64(sc->hash[i]);
			hash += 8;
		}
		if (halfWord) {
			hash[0] = (uint8_t) (sc->hash[i] >> 56);
			hash[1] = (uint8_t) (sc->hash[i] >> 48);
			hash[2] = (uint8_t) (sc->hash[i] >> 40);
			hash[3] = (uint8_t) (sc->hash[i] >> 32);
		}
	}
}

void
SHA512ASM_Final (SHA512ASM_Context *sc, uint8_t hash[SHA512ASM_HASH_SIZE])
{
	_final (sc, hash, SHA512ASM_HASH_WORDS, 0);
}

unsigned char *SHA512ASM(const unsigned char *d, size_t n,unsigned char *md)
{
    SHA512ASM_Context sc;
    SHA512ASM_Init(&sc);
    SHA512ASM_Update (&sc, d, n);
    SHA512ASM_Final (&sc, md);
    return md;
}