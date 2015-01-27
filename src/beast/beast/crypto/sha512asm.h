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

#ifndef _APS_SHA512ASM_H
#define _APS_SHA512ASM_H

#include <stdint.h>

#define	SHA512ASM_HASH_SIZE		64
#define	SHA512ASM_BLOCK_SIZE		128L

/* Hash size in 64-bit words */
#define SHA512ASM_HASH_WORDS 8

typedef struct _SHA512ASM_Context {
  uint64_t totalLength[2], blocks;
  uint64_t hash[SHA512ASM_HASH_WORDS];
  uint32_t bufferLength;
  union {
    uint64_t words[SHA512ASM_BLOCK_SIZE/8];
    uint8_t bytes[SHA512ASM_BLOCK_SIZE];
  } buffer;
} SHA512ASM_Context;

#ifdef __cplusplus
extern "C" {
#endif

void Init_SHA512ASM_avx2();
void Init_SHA512ASM_avx();
void Init_SHA512ASM_sse4();

void SHA512ASM_Init (SHA512ASM_Context *sc);
void SHA512ASM_Update (SHA512ASM_Context *sc, const void *data, size_t len);
void SHA512ASM_Final (SHA512ASM_Context *sc, uint8_t hash[SHA512ASM_HASH_SIZE]);

unsigned char *SHA512ASM(const unsigned char *d, size_t n,unsigned char *md);
    
/*
 * Intel's optimized SHA512 core routines. These routines are described in an
 * Intel White-Paper:
 * "Fast SHA-512 Implementations on Intel Architecture Processors"
 * Note: Works on AMD Bulldozer and later as well.
 */
extern void sha512_sse4(const void *input_data, void *digest, uint64_t num_blks);
extern void sha512_avx(const void *input_data, void *digest, uint64_t num_blks);
extern void sha512_rorx(const void *input_data, void *digest, uint64_t num_blks);

#ifdef __cplusplus
}
#endif

#endif /* !_APS_SHA512ASM_H */
