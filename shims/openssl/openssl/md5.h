// Вместо <openssl/md5.h>: libslic3r берёт из OpenSSL только MD5 (Utils.hpp,
// utils.cpp, bbs_3mf.cpp), а тащить OpenSSL в WebAssembly ради этого незачем.
// Обычный MD5 по RFC 1321 с тем же API: MD5_CTX, MD5_Init, MD5_Update, MD5_Final.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#define MD5_DIGEST_LENGTH 16

struct MD5_CTX
{
    std::uint32_t state[4];
    std::uint64_t bytes;
    unsigned char buffer[64];
};

namespace wasm_md5 {

inline std::uint32_t rotl(std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

inline void transform(std::uint32_t state[4], const unsigned char block[64])
{
    static const std::uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                              5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

    std::uint32_t m[16];
    for (int i = 0; i < 16; ++i)
        m[i] = std::uint32_t(block[i * 4]) | std::uint32_t(block[i * 4 + 1]) << 8 |
               std::uint32_t(block[i * 4 + 2]) << 16 | std::uint32_t(block[i * 4 + 3]) << 24;

    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    for (int i = 0; i < 64; ++i) {
        std::uint32_t f;
        int           g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        std::uint32_t next = d;
        d                  = c;
        c                  = b;
        b                  = b + rotl(a + f + K[i] + m[g], S[i]);
        a                  = next;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

} // namespace wasm_md5

inline int MD5_Init(MD5_CTX *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->bytes    = 0;
    return 1;
}

inline int MD5_Update(MD5_CTX *ctx, const void *data, std::size_t len)
{
    const unsigned char *p    = static_cast<const unsigned char *>(data);
    std::size_t          used = std::size_t(ctx->bytes % 64);
    ctx->bytes += len;
    if (used > 0) {
        std::size_t take = 64 - used < len ? 64 - used : len;
        std::memcpy(ctx->buffer + used, p, take);
        p += take;
        len -= take;
        if (used + take < 64)
            return 1;
        wasm_md5::transform(ctx->state, ctx->buffer);
    }
    for (; len >= 64; p += 64, len -= 64)
        wasm_md5::transform(ctx->state, p);
    std::memcpy(ctx->buffer, p, len);
    return 1;
}

inline int MD5_Final(unsigned char *digest, MD5_CTX *ctx)
{
    const std::uint64_t bits = ctx->bytes * 8;
    static const unsigned char pad[64] = {0x80};
    std::size_t used = std::size_t(ctx->bytes % 64);
    MD5_Update(ctx, pad, used < 56 ? 56 - used : 120 - used);
    unsigned char length[8];
    for (int i = 0; i < 8; ++i)
        length[i] = static_cast<unsigned char>(bits >> (8 * i));
    MD5_Update(ctx, length, 8);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            digest[i * 4 + j] = static_cast<unsigned char>(ctx->state[i] >> (8 * j));
    return 1;
}
