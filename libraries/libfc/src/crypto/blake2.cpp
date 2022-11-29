/*
   BLAKE2 reference source code package - reference C implementations

   Copyright 2012, Samuel Neves <sneves@dei.uc.pt>.  You may use this under the
   terms of the CC0, the OpenSSL Licence, or the Apache Public License 2.0, at
   your option.  The terms of these licenses can be found at:

   - CC0 1.0 Universal : http://creativecommons.org/publicdomain/zero/1.0
   - OpenSSL license   : https://www.openssl.org/source/license.html
   - Apache 2.0        : http://www.apache.org/licenses/LICENSE-2.0

   More information about the BLAKE2 hash function can be found at
   https://blake2.net.
*/

#include <cstdint>
#include <cstring>
#include <limits>
#include <fc/crypto/blake2.hpp>

namespace fc {

static const uint64_t blake2b_IV[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
                                       0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
                                       0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};

static const uint8_t blake2b_sigma[12][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4}, {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13}, {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11}, {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5}, {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3}};

static inline uint64_t load64(const void *src) {
    uint64_t w;
    memcpy(&w, src, sizeof w);
    return w;
}

static inline uint64_t rotr64(const uint64_t w, const unsigned c) { return (w >> c) | (w << (64 - c)); }

inline void blake2b_wrapper::G(uint8_t r, uint8_t i, uint64_t& a, uint64_t& b, uint64_t& c, uint64_t& d) noexcept
{
    a = a + b + m[blake2b_sigma[r][2 * i + 0]];
    d = rotr64(d ^ a, 32);
    c = c + d;
    b = rotr64(b ^ c, 24);
    a = a + b + m[blake2b_sigma[r][2 * i + 1]];
    d = rotr64(d ^ a, 16);
    c = c + d;
    b = rotr64(b ^ c, 63);
}

inline void blake2b_wrapper::ROUND(uint8_t r) noexcept
{
    G(r, 0, v[0], v[4], v[8], v[12]);
    G(r, 1, v[1], v[5], v[9], v[13]);
    G(r, 2, v[2], v[6], v[10], v[14]);
    G(r, 3, v[3], v[7], v[11], v[15]);
    G(r, 4, v[0], v[5], v[10], v[15]);
    G(r, 5, v[1], v[6], v[11], v[12]);
    G(r, 6, v[2], v[7], v[8], v[13]);
    G(r, 7, v[3], v[4], v[9], v[14]);
}

void blake2b_wrapper::blake2b_compress(blake2b_state *S, const uint8_t block[BLAKE2B_BLOCKBYTES], size_t r, const yield_function_t& yield) {
    blake2b_compress_init(S, block, r);

    for (i = 0; i < r; ++i) {
        ROUND(i % 10);
        if (i % 100) {
            yield();
        }
    }

    blake2b_compress_end(S);
}

void blake2b_wrapper::blake2b_compress_init(blake2b_state *S, const uint8_t block[BLAKE2B_BLOCKBYTES], size_t r) {
    for (i = 0; i < 16; ++i) {
        m[i] = load64(block + i * sizeof(m[i]));
    }

    for (i = 0; i < 8; ++i) {
        v[i] = S->h[i];
    }

    v[8] = blake2b_IV[0];
    v[9] = blake2b_IV[1];
    v[10] = blake2b_IV[2];
    v[11] = blake2b_IV[3];
    v[12] = blake2b_IV[4] ^ S->t[0];
    v[13] = blake2b_IV[5] ^ S->t[1];
    v[14] = blake2b_IV[6] ^ S->f[0];
    v[15] = blake2b_IV[7];
}

void blake2b_wrapper::blake2b_compress_end(blake2b_state *S) {
    for (i = 0; i < 8; ++i) {
        S->h[i] = S->h[i] ^ v[i] ^ v[i + 8];
    }
}

std::variant<blake2b_error, bytes> blake2b(uint32_t _rounds, const bytes& _h, const bytes& _m, const bytes& _t0_offset, const bytes& _t1_offset, bool _f, const yield_function_t& yield) {

    //  EIP-152 [4 bytes for rounds][64 bytes for h][128 bytes for m][8 bytes for t_0][8 bytes for t_1][1 byte for f] : 213
    //          [------------------][64 bytes for h][128 bytes for m][8 bytes for t_0][8 bytes for t_1][------------] : 208
    //  * rounds and final indicator flag are not serialized
    if (_h.size() != 64 || _m.size() != blake2b_wrapper::BLAKE2B_BLOCKBYTES || _t0_offset.size() != 8 || _t1_offset.size() != 8) {
        return blake2b_error::input_len_error;
    }
    
    blake2b_wrapper b2wrapper;
    blake2b_state state{};
    
    memcpy(state.h, _h.data(), 64);

    // final indicator flag set words to 1's if true
    state.f[0] = _f ? std::numeric_limits<uint64_t>::max() : 0;

    memcpy(&state.t[0], _t0_offset.data(), 8);
    memcpy(&state.t[1], _t1_offset.data(), 8);

    uint8_t block[blake2b_wrapper::BLAKE2B_BLOCKBYTES];
    memcpy(block, _m.data(), blake2b_wrapper::BLAKE2B_BLOCKBYTES);
    
    b2wrapper.blake2b_compress(&state, block, _rounds, yield);

    bytes out(sizeof(state.h), 0);
    memcpy(&out[0], &state.h[0], out.size());
    return out;
}

}
#undef G
#undef ROUND
