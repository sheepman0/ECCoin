/*-
 * Copyright 2009 Colin Percival, 2011 ArtForz, 2011 pooler, 2013 Balthazar
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
 *
 * This file was originally written by Colin Percival as part of the Tarsnap
 * online backup system.
 */

#include <stdlib.h>
#include <stdint.h>

// Here, we need to check if we're compiling for an ARM processor. If we are, we
// need to make sure we don't try to use SSE instructions
extern "C" {
#ifndef NOSSE
#include <xmmintrin.h>
#endif
}

#include "scrypt_mine.h"
#include "pbkdf2.h"

#include "util.h"
#include "net.h"

using namespace boost;
using namespace std;

extern bool fShutdown;
extern bool fGenerateBitcoins;

extern CBlockIndex* pindexBest;
extern uint32_t nTransactionsUpdated;

// Here, we need to check to see what type of processor we're using
// because x86 and x64 have different buffer size requirements.
// We're assuming ARM processors fall in with i386 processors
// because I can't think of a good reason to automatically go with a
// larger buffer, given the lower RAM and swap file sizes for ARM boards 
// compared to x86 boards.
// In case this is an issue, see the following URL for help determining
// a good buffer size:
// https://github.com/pooler/cpuminer/blob/4611186cb88eec76f22a88565675473b2eeade28/scrypt.c#L502-510
// It basically recommends SCRYPT_BUFFER_SIZE = (sizeof(int) * SCRYPT_MAX_WAYS * 128 + 63)

#if defined(__x86_64__)

#define SCRYPT_BUFFER_SIZE (3 * 131072 + 63)

extern "C" int scrypt_best_throughput();
extern "C" void scrypt_core(uint32_t *X, uint32_t *V);

#elif ( defined(__i386__)||defined(__arm__) )

#define SCRYPT_BUFFER_SIZE (131072 + 63)

#endif

static inline void xor_salsa8(unsigned int B[16], const unsigned int Bx[16])
{
    unsigned int x00,x01,x02,x03,x04,x05,x06,x07,x08,x09,x10,x11,x12,x13,x14,x15;
    int i;

    x00 = (B[0] ^= Bx[0]);
    x01 = (B[1] ^= Bx[1]);
    x02 = (B[2] ^= Bx[2]);
    x03 = (B[3] ^= Bx[3]);
    x04 = (B[4] ^= Bx[4]);
    x05 = (B[5] ^= Bx[5]);
    x06 = (B[6] ^= Bx[6]);
    x07 = (B[7] ^= Bx[7]);
    x08 = (B[8] ^= Bx[8]);
    x09 = (B[9] ^= Bx[9]);
    x10 = (B[10] ^= Bx[10]);
    x11 = (B[11] ^= Bx[11]);
    x12 = (B[12] ^= Bx[12]);
    x13 = (B[13] ^= Bx[13]);
    x14 = (B[14] ^= Bx[14]);
    x15 = (B[15] ^= Bx[15]);
    for (i = 0; i < 8; i += 2) {
#define R(a, b) (((a) << (b)) | ((a) >> (32 - (b))))
        /* Operate on columns. */
        x04 ^= R(x00+x12, 7); x09 ^= R(x05+x01, 7);
        x14 ^= R(x10+x06, 7); x03 ^= R(x15+x11, 7);

        x08 ^= R(x04+x00, 9); x13 ^= R(x09+x05, 9);
        x02 ^= R(x14+x10, 9); x07 ^= R(x03+x15, 9);

        x12 ^= R(x08+x04,13); x01 ^= R(x13+x09,13);
        x06 ^= R(x02+x14,13); x11 ^= R(x07+x03,13);

        x00 ^= R(x12+x08,18); x05 ^= R(x01+x13,18);
        x10 ^= R(x06+x02,18); x15 ^= R(x11+x07,18);

        /* Operate on rows. */
        x01 ^= R(x00+x03, 7); x06 ^= R(x05+x04, 7);
        x11 ^= R(x10+x09, 7); x12 ^= R(x15+x14, 7);

        x02 ^= R(x01+x00, 9); x07 ^= R(x06+x05, 9);
        x08 ^= R(x11+x10, 9); x13 ^= R(x12+x15, 9);

        x03 ^= R(x02+x01,13); x04 ^= R(x07+x06,13);
        x09 ^= R(x08+x11,13); x14 ^= R(x13+x12,13);

        x00 ^= R(x03+x02,18); x05 ^= R(x04+x07,18);
        x10 ^= R(x09+x08,18); x15 ^= R(x14+x13,18);
#undef R
    }
    B[0] += x00;
    B[1] += x01;
    B[2] += x02;
    B[3] += x03;
    B[4] += x04;
    B[5] += x05;
    B[6] += x06;
    B[7] += x07;
    B[8] += x08;
    B[9] += x09;
    B[10] += x10;
    B[11] += x11;
    B[12] += x12;
    B[13] += x13;
    B[14] += x14;
    B[15] += x15;
}


inline void scrypt_core(unsigned int *X, unsigned int *V)
{
    unsigned int i, j, k;

    for (i = 0; i < 1024; i++) {
        memcpy(&V[i * 32], X, 128);
        xor_salsa8(&X[0], &X[16]);
        xor_salsa8(&X[16], &X[0]);
    }
    for (i = 0; i < 1024; i++) {
        j = 32 * (X[16] & 1023);
        for (k = 0; k < 32; k++)
            X[k] ^= V[j + k];
        xor_salsa8(&X[0], &X[16]);
        xor_salsa8(&X[16], &X[0]);
    }
}


void *scrypt_buffer_alloc() {
    return malloc(SCRYPT_BUFFER_SIZE);
}

void scrypt_buffer_free(void *scratchpad)
{
    free(scratchpad);
}

/* cpu and memory intensive function to transform a 80 byte buffer into a 32 byte output
   scratchpad size needs to be at least 63 + (128 * r * p) + (256 * r + 64) + (128 * r * N) bytes
   r = 1, p = 1, N = 1024
 */

static void scrypt(const void* input, size_t inputlen, uint32_t *res, void *scratchpad)
{
    uint32_t *V;
    uint32_t X[32];
    V = (uint32_t *)(((uintptr_t)(scratchpad) + 63) & ~ (uintptr_t)(63));

    PBKDF2_SHA256((const uint8_t*)input, inputlen, (const uint8_t*)input, sizeof(block_header), 1, (uint8_t *)X, 128);

    scrypt_core(X, V);

    PBKDF2_SHA256((const uint8_t*)input, inputlen, (uint8_t *)X, 128, 1, (uint8_t*)res, 32);
}

void scrypt_hash_mine(const void* input, size_t inputlen, uint32_t *res, void *scratchpad)
{
    return scrypt(input, inputlen, res, scratchpad);
}

#ifdef SCRYPT_3WAY
static void scrypt_2way(const void *input1, const void *input2, size_t input1len, size_t input2len, uint32_t *res1, uint32_t *res2, void *scratchpad)
{
    uint32_t *V;
    uint32_t X[32], Y[32];
    V = (uint32_t *)(((uintptr_t)(scratchpad) + 63) & ~ (uintptr_t)(63));

    PBKDF2_SHA256((const uint8_t*)input1, input1len, (const uint8_t*)input1, input1len, 1, (uint8_t *)X, 128);
    PBKDF2_SHA256((const uint8_t*)input2, input2len, (const uint8_t*)input2, input2len, 1, (uint8_t *)Y, 128);

    scrypt_core_2way(X, Y, V);

    PBKDF2_SHA256((const uint8_t*)input1, input1len, (uint8_t *)X, 128, 1, (uint8_t*)res1, 32);
    PBKDF2_SHA256((const uint8_t*)input2, input2len, (uint8_t *)Y, 128, 1, (uint8_t*)res2, 32);
}

static void scrypt_3way(const void *input1, const void *input2, const void *input3,
   size_t input1len, size_t input2len, size_t input3len, uint32_t *res1, uint32_t *res2, uint32_t *res3,
   void *scratchpad)
{
    uint32_t *V;
    uint32_t X[32], Y[32], Z[32];
    V = (uint32_t *)(((uintptr_t)(scratchpad) + 63) & ~ (uintptr_t)(63));

    PBKDF2_SHA256((const uint8_t*)input1, input1len, (const uint8_t*)input1, input1len, 1, (uint8_t *)X, 128);
    PBKDF2_SHA256((const uint8_t*)input2, input2len, (const uint8_t*)input2, input2len, 1, (uint8_t *)Y, 128);
    PBKDF2_SHA256((const uint8_t*)input3, input3len, (const uint8_t*)input3, input3len, 1, (uint8_t *)Z, 128);

    scrypt_core_3way(X, Y, Z, V);

    PBKDF2_SHA256((const uint8_t*)input1, input1len, (uint8_t *)X, 128, 1, (uint8_t*)res1, 32);
    PBKDF2_SHA256((const uint8_t*)input2, input2len, (uint8_t *)Y, 128, 1, (uint8_t*)res2, 32);
    PBKDF2_SHA256((const uint8_t*)input3, input3len, (uint8_t *)Z, 128, 1, (uint8_t*)res3, 32);
}
#endif

unsigned int scanhash_scrypt(block_header *pdata, void *scratchbuf,
    uint32_t max_nonce, uint32_t &hash_count,
    void *result, block_header *res_header)
{
    hash_count = 0;
    block_header data = *pdata;
    uint32_t hash[8];
    unsigned char *hashc = (unsigned char *) &hash;

#ifdef SCRYPT_3WAY
    block_header data2 = *pdata;
    uint32_t hash2[8];
    unsigned char *hashc2 = (unsigned char *) &hash2;

    block_header data3 = *pdata;
    uint32_t hash3[8];
    unsigned char *hashc3 = (unsigned char *) &hash3;

    int throughput = scrypt_best_throughput();
#endif

    uint32_t n = 0;

    while (true) {

        data.nonce = n++;

#ifdef SCRYPT_3WAY
        if (throughput >= 2 && n < max_nonce) {
            data2.nonce = n++;
            if(throughput >= 3)
            {
                data3.nonce = n++;
                scrypt_3way(&data, &data2, &data3, 80, 80, 80, hash, hash2, hash3, scratchbuf);
                hash_count += 3;

                if (hashc3[31] == 0 && hashc3[30] == 0) {
                    memcpy(result, hash3, 32);
                    *res_header = data3;

                    return data3.nonce;
                }
            }
            else
            {
                scrypt_2way(&data, &data2, 80, 80, hash, hash2, scratchbuf);
                hash_count += 2;
            }

            if (hashc2[31] == 0 && hashc2[30] == 0) {
                memcpy(result, hash2, 32);

                return data2.nonce;
            }
        } else {
            scrypt(&data, 80, hash, scratchbuf);
            hash_count += 1;
        }
#else
        scrypt(&data, 80, hash, scratchbuf);
        hash_count += 1;
#endif
        if (hashc[31] == 0 && hashc[30] == 0) {
            memcpy(result, hash, 32);

            return data.nonce;
        }

        if (n >= max_nonce) {
            hash_count = 0xffff + 1;
            break;
        }
    }

    return (unsigned int) -1;
}

uint256 scryptX(const void* data, size_t datalen, const void* salt, size_t saltlen, void *scratchpad)
{
    unsigned int *V;
    unsigned int X[32];
    uint256 result = 0;
    V = (unsigned int *)(((uintptr_t)(scratchpad) + 63) & ~ (uintptr_t)(63));

    PBKDF2_SHA256((const uint8_t*)data, datalen, (const uint8_t*)salt, saltlen, 1, (uint8_t *)X, 128);
    scrypt_core(X, V);
    PBKDF2_SHA256((const uint8_t*)data, datalen, (uint8_t *)X, 128, 1, (uint8_t*)&result, 32);

    return result;
}

uint256 scrypt_salted_hash(const void* input, size_t inputlen, const void* salt, size_t saltlen)
{
    unsigned char scratchpad[SCRYPT_BUFFER_SIZE];
    return scryptX(input, inputlen, salt, saltlen, scratchpad);
}


uint256 scrypt_salted_multiround_hash(const void* input, size_t inputlen, const void* salt, size_t saltlen, const unsigned int nRounds)
{
    uint256 resultHash = scrypt_salted_hash(input, inputlen, salt, saltlen);
    uint256 transitionalHash = resultHash;

    for(unsigned int i = 1; i < nRounds; i++)
    {
        resultHash = scrypt_salted_hash(input, inputlen, (const void*)&transitionalHash, 32);
        transitionalHash = resultHash;
    }

    return resultHash;
}
