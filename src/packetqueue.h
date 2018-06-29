#pragma once

#include <cstdint>

#include "crypto.h"
#include "queue.h"

constexpr const uint8_t PACKET_QUEUE_LEN = 32;
constexpr const uint8_t SENT_PACKET_LEN = 76;

// implements a packet queue
struct PacketQ {
    using Packet = ShortQ<SENT_PACKET_LEN>;

    enum SpecialAddrs : uint8_t {
        MASTER_ADDR = 0x00,
        SYNC_ADDR = 0xff
    };

    ICACHE_FLASH_ATTR PacketQ(crypto::Crypto &crypto, time_t packet_max_age)
        : crypto(crypto), que(), packet_max_age(packet_max_age)
    {}

    struct Item {
        void clear() {
            addr = -1;
            packet.clear();
        }

        int8_t  addr = -1;
        Packet  packet;
        time_t  time;
    };

    /// insert into queue or return nullptr if full
    /// returns packet structure to be filled with data
    Packet * ICACHE_FLASH_ATTR want_to_send_for(uint8_t addr, uint8_t bytes, time_t curtime) {
        for (int i = 0; i < PACKET_QUEUE_LEN; ++i) {
            // reverse index - we queue top first, send bottom first
            // to have fair queueing
            int ri = PACKET_QUEUE_LEN - 1 - i;
            Item &it = que[ri];

            if (it.addr == addr) {
                if (it.packet.free_size() > bytes)
                    return &it.packet;
            }

            // free spot or too old packet
            if ((it.addr == -1) || (it.time + packet_max_age < curtime)) {
                it.addr = addr;
                it.time = curtime;
                // every non-sync packet starts with addr
                // which is contained in cmac calculation
                if (addr != SYNC_ADDR) it.packet.push(addr);
                return &it.packet;
            }
        }

        // full
        return nullptr;
    }

    // prepares queue to send data for address addr, if there's a
    // prepared packet present
    void ICACHE_FLASH_ATTR prepare_to_send_to(uint8_t addr) {
        if (sending) return;

        for (unsigned i = 0; i < PACKET_QUEUE_LEN; ++i) {
            Item &it = que[i];
            if (it.addr == addr) {
                sending = &it;
                // just something to not get handled while we're sending this
                it.addr = -2;
                // we need to sign this with cmac
                cmac.clear();
                crypto.cmac_fill(it.packet.data(), it.packet.size(), false, cmac);
                // TODO: this should probably be handled by Protocol class
                prologue.clear();
                prologue.push(0xaa); // just some gibberish
                prologue.push(0xaa);
                prologue.push(0x2d); // 2 byte sync word
                prologue.push(0xd4);
                // length, highest byte indicates sync word
                prologue.push(it.packet.size() | ((it.addr == SYNC_ADDR) ? 0x80 : 0x00));
                // non-sync packets have to be encrypted as well
                if (it.addr != SYNC_ADDR)
                    crypto.encrypt_decrypt(it.packet.data(), it.packet.size());
                return;
            }
        }
    }

    int ICACHE_FLASH_ATTR peek() {
        if (!sending) return -1;

        // first comes the prologue
        if (!prologue.empty())
            return prologue.peek();

        if (!sending->packet.empty())
            return sending->packet.peek();

        if (!cmac.empty())
            return cmac.peek();

        return -1;
    }

    void ICACHE_FLASH_ATTR pop() {
        if (!sending) return;

        if (!prologue.empty()) {
            prologue.pop();
            return;
        }

        if (!sending->packet.empty()) {
            sending->packet.pop();
            return;
        }

        if (!cmac.empty()) {
            cmac.pop();
        }

        // empty after all this?
        if (cmac.empty()) {
            prologue.clear();
            sending->clear();
            cmac.clear();
            sending = nullptr;
        }
    }

    crypto::Crypto &crypto;
    Item que[PACKET_QUEUE_LEN];
    Item *sending = nullptr;
    ShortQ<5> prologue; // stores sync-word and size
    ShortQ<4> cmac; // stores cmac for packet at sndIdx
    time_t packet_max_age;
};
