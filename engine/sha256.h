// sha256.h - 独立实现 SHA-256（无外部依赖）
#ifndef ETC_SHA256_H
#define ETC_SHA256_H

#include <cstdint>
#include <cstring>
#include <string>

namespace etc {

class Sha256 {
public:
    Sha256() { reset(); }

    void reset() {
        state[0] = 0x6a09e667UL; state[1] = 0xbb67ae85UL;
        state[2] = 0x3c6ef372UL; state[3] = 0xa54ff53aUL;
        state[4] = 0x510e527fUL; state[5] = 0x9b05688cUL;
        state[6] = 0x1f83d9abUL; state[7] = 0x5be0cd19UL;
        total[0] = 0; total[1] = 0;
        buflen = 0;
    }

    void update(const void* data, size_t len) {
        const uint8_t* p = (const uint8_t*)data;
        while (len > 0) {
            size_t take = 64 - buflen;
            if (take > len) take = len;
            memcpy(buf + buflen, p, take);
            buflen += (uint32_t)take;
            p += take; len -= take;
            if (buflen == 64) {
                transform(buf);
                total[0] += 64;
                if (total[0] < 64) total[1]++;
                buflen = 0;
            }
        }
    }

    std::string hex() {
        // 完成填充
        uint64_t bitlen = ((uint64_t)total[1] << 32 | total[0]) * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while (buflen != 56) update(&zero, 1);
        uint8_t lenbuf[8];
        for (int i = 0; i < 8; i++) lenbuf[7 - i] = (uint8_t)(bitlen >> (i * 8));
        update(lenbuf, 8);

        static const char* hexd = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (int i = 0; i < 8; i++) {
            for (int b = 3; b >= 0; b--) {
                uint8_t v = (state[i] >> (b * 8)) & 0xFF;
                out += hexd[v >> 4];
                out += hexd[v & 0xF];
            }
        }
        reset();
        return out;
    }

private:
    uint32_t state[8];
    uint32_t total[2];
    uint8_t buf[64];
    uint32_t buflen;

    static inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    static inline uint32_t shr(uint32_t x, int n) { return x >> n; }

    void transform(const uint8_t* block) {
        static const uint32_t K[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
                   ((uint32_t)block[i * 4 + 2] << 8) | block[i * 4 + 3];
        }
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ shr(w[i - 15], 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ shr(w[i - 2], 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = h + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }
};

} // namespace etc

#endif
