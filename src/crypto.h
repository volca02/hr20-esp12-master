#pragma once

#include <cstdint>
#include <Time.h>

#include "queue.h"

namespace ntptime {
struct NTPTime;
} // namespace ntptime

namespace crypto {

// simple block based xtea enc/dec
struct XTEA {
    static constexpr const uint8_t  XTEA_BLOCK_SIZE = 8;
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
    void ICACHE_FLASH_ATTR decrypt(const uint8_t *src,
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
    enum { CMAC_SIZE = 4 };

    ICACHE_FLASH_ATTR CMAC(const uint8_t *k1,
                           const uint8_t *k2,
                           const uint8_t *kmac,
                           const uint8_t *prefix = nullptr)
        : k1(k1), k2(k2), kmac(kmac), xt(kmac), pos(0)
    {
        if (prefix) {
            xt.encrypt(prefix, buf);
        } else {
            memset(buf, 0, 8);
        }
    }

    void append(const uint8_t *data, uint8_t size) {
        uint8_t x = 0;

        while (true) {
            for (; x < size && pos < 8; ++x, ++pos) {
                wip[pos] = data[x];
            }

            // leave rest for finish or next run
            if (x >= size) break;

            // not a whole window, leave it for the next time
            if (pos < 8) break;

            for (unsigned j = 0; j < 8; ++j) {
                buf[j] ^= wip[j];
            }
            pos = 0;
            xt.encrypt(buf, buf);
        }
    }

    uint8_t *finish() {
        // any excess data that didn't get processed?
        const uint8_t *kx = ((pos == 8) ? k1 : k2);

        if (pos > 0) {
            uint8_t trail = 0x80;

            // pre-prepare the rest of the wip buffer
            for (uint8_t p = pos; p < 8; ++p) {
                wip[p] = trail;
                trail  = 0x0;
            }

            for (unsigned j = 0; j < 8; j++) {
                buf[j] ^= wip[j];
            }
        }

        for (unsigned j = 0; j < 8; j++) {
            buf[j] ^= kx[j];
        }

        xt.encrypt(buf, buf);
        return buf;
    }

protected:
    const uint8_t *k1;
    const uint8_t *k2;
    const uint8_t *kmac;
    XTEA          xt;
    uint8_t       buf[8];

    uint8_t       pos;
    uint8_t       wip[8];
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

    // initializes Kmac, Kenc, K1 and K2
    Crypto(const uint8_t *rfm_pass, ntptime::NTPTime &time);

    // updates the rtc if needed. Returns true if second passed
    bool update();

    // packet payload encrypt/decrypt function
    void encrypt_decrypt(uint8_t *data, unsigned size);

    bool ICACHE_FLASH_ATTR cmac_verify(const uint8_t *data, size_t size,
                                       bool isSync)
    {
        CMAC cmac(K1, K2, Kmac, isSync ? nullptr : rtc_bytes());

        cmac.append(data, size);
        const uint8_t *buf = cmac.finish();

        for (uint8_t i = 0; i < CMAC::CMAC_SIZE; i++) {
            if (data[size + i] != buf[i])
                return false;
        }

        return true;
    }

    // fills cmac for given packet
    template<uint8_t CNT>
    void ICACHE_FLASH_ATTR cmac_fill_sync(const uint8_t *data, size_t size,
                                          ShortQ<CNT> &tgt) const
    {
        CMAC cmac(K1, K2, Kmac);

        cmac.append(data, size);
        const uint8_t *buf = cmac.finish();

        for (int i = 0; i < CMAC::CMAC_SIZE; ++i) tgt.push(buf[i]);
    }

    // fills cmac for given packet
    template<uint8_t CNT>
    void ICACHE_FLASH_ATTR cmac_fill_addr(const uint8_t *data, size_t size,
                                          uint8_t addr, ShortQ<CNT> &tgt)
    {
        CMAC cmac(K1, K2, Kmac, rtc_bytes());

        cmac.append(&addr, 1);
        cmac.append(data, size);
        const uint8_t *buf = cmac.finish();

        for (int i = 0; i < CMAC::CMAC_SIZE; ++i) tgt.push(buf[i]);

        // non-sync packets increment packet count
        rtc.pkt_cnt++;
    }

    const uint8_t * ICACHE_FLASH_ATTR rtc_bytes() const {
        return reinterpret_cast<const uint8_t *>(&rtc);
    }
};

} // namespace crypto
