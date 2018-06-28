#pragma once

#include <Arduino.h>
#include <cstdint>

#include "queue.h"

namespace ntptime {
struct NTPTime;
} // namespace ntptime

namespace crypto {

// simple block based xtea enc/dec
struct XTEA {
    static constexpr const size_t XTEA_BLOCK_SIZE = 8;
    static constexpr const uint32_t XTEA_DELTA = 0x09E3779B9;

    XTEA(const uint8_t *k) : key(reinterpret_cast<const uint32_t*>(k)) {}

    // encrypts a block of 8 bytes using XTEA
    void encrypt(const uint8_t *src,
                 uint8_t *dst)
    {
        const uint32_t* s = reinterpret_cast<const uint32_t*>(src);
        uint32_t sum = 0;
        uint32_t* d = reinterpret_cast<uint32_t*>(dst);

        uint32_t s0 = s[0];
        uint32_t s1 = s[1];

        for (unsigned int i = 0; i < 32; ++i) {
            s0  += (((s1 << 4) ^ (s1 >> 5)) + s1) ^ (sum + key[sum & 3]);
            sum += XTEA_DELTA;
            s1  += (((s0 << 4) ^ (s0 >> 5)) + s0) ^ (sum + key[(sum >> 11) & 3]);
        }

        d[0] = s0;
        d[1] = s1;
    }

    // decrypts a block of 8 bytes using XTEA
    void decrypt(const uint8_t *src,
                 uint8_t *dst)
    {
        const uint32_t* s = (const uint32_t*)src;
        uint32_t s0 = s[0];
        uint32_t s1 = s[1];
        uint32_t sum = XTEA_DELTA * 32;
        uint32_t* d = (uint32_t*)dst;

        unsigned int i;
        for (i = 0; i < 32; i++) {
            s1  -= (((s0 << 4) ^ (s0 >> 5)) + s0) ^ (sum + key[(sum >> 11) & 3]);
            sum -= XTEA_DELTA;
            s0  -= (((s1 << 4) ^ (s1 >> 5)) + s1) ^ (sum + key[sum & 3]);
        }

        d[0] = s0;
        d[1] = s1;
    }

protected:
    const uint32_t *key;
};

// cmac calculator, reimplementation of cmac.c
struct CMAC {
    CMAC(const uint8_t *k1,
         const uint8_t *k2,
         const uint8_t *kmac)
         : k1(k1), k2(k2), kmac(kmac)
    {}

    // sets cmac to a given ShortQ
    void compute(const uint8_t *data, size_t size, ShortQ<4> &cmac,
                 const uint8_t *prefix = nullptr) const
    {
        uint8_t buf[8];
        calc_cmac(data, size, prefix, buf);
        cmac.clear();
        for (int i = 0; i < 4; ++i) cmac.push(buf[i]);
    }

    // verifies 4 bytes after the specified buffer end for cmac signature
    bool verify(const uint8_t *data, size_t size, const uint8_t *prefix = nullptr) const {
        uint8_t buf[8];
        calc_cmac(data, size, prefix, buf);

        for (uint8_t i = 0; i < 4; i++) {
            if (data[size + i] != buf[i]) return false;
        }

        return true;
    }

protected:
    void calc_cmac(const uint8_t *data, size_t size, const uint8_t *prefix, uint8_t *buf) const;

    const uint8_t *k1;
    const uint8_t *k2;
    const uint8_t *kmac;
};

struct RTC {
    uint8_t YY; //!< \brief Date: Year (0-255) -> 2000 - 2255
    uint8_t MM; //!< \brief Date: Month
    uint8_t DD; //!< \brief Date: Day
    uint8_t hh; //!< \brief Time: Hours
    uint8_t mm; //!< \brief Time: Minutes
    uint8_t ss; //!< \brief Time: Seconds
    uint8_t DOW;  //!< Date: Day of Week
    uint8_t pkt_cnt;
};

// main packet encrypt/decript routines
struct Crypto {
    // upper part of master key for OpenHR20 (upper half for the key generation)
    static const uint8_t Km_upper[8];

    // overlapping Kmac and Kenc keys (middle 8 bytes are shared)
    uint8_t keys[3 * 8] = {0};
    uint8_t *Kmac = keys;
    uint8_t *Kenc = &keys[8];

    // CMAC keys
    uint8_t K1[8] = {0,0,0,0,0,0,0,0};
    uint8_t K2[8] = {0,0,0,0,0,0,0,0};

    // Time management:
    ntptime::NTPTime &time;
    time_t lastTime;
    RTC rtc;

    crypto::CMAC cmac;

    // initializes Kmac, Kenc, K1 and K2
    Crypto(const uint8_t *rfm_pass, ntptime::NTPTime &time);

    // updates the rtc if needed. Returns true if second passed
    bool update();

    // packet payload encrypt/decrypt function
    void encrypt_decrypt(uint8_t *data, unsigned size);

    bool cmac_verify(const uint8_t *data, size_t size, bool isSync) {
        return cmac.verify(
                data, size,
                isSync ? nullptr : rtc_bytes());
    }

    // fills cmac for given packet
    void cmac_fill(const uint8_t *data, size_t size,
                   bool isSync, ShortQ<4> &tgt) const
    {
        cmac.compute(data, size, tgt,
                     isSync ? nullptr : rtc_bytes());

    }

    const uint8_t *rtc_bytes() const {
        return reinterpret_cast<const uint8_t *>(&rtc);
    }
};

} // namespace crypto
