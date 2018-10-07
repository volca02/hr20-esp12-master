/*
 * HR20 ESP Master
 * ---------------
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see http:*www.gnu.org/licenses
 *
 */

#pragma once

#include <cstdint>

#include "crypto.h"
#include "queue.h"
#include "debug.h"
#include "error.h"

namespace hr20 {

constexpr const uint8_t PACKET_QUEUE_LEN = 32;
// constexpr const uint8_t SENT_PACKET_LEN = 76;
// 19 is empirical - maybe 24 could work, but we should analyze why the clients
// struggle to send more than that - it would benefit the comm speed greatly.
constexpr const uint8_t SENT_PACKET_LEN = 25;

// implements a packet queue
struct PacketQ {
    using Packet = ShortQ<SENT_PACKET_LEN>;

    enum SpecialAddrs : uint8_t {
        MASTER_ADDR = 0x00,
        SYNC_ADDR = 0x21 // MAX ADDR IS 0x20, we're fine here
    };

    ICACHE_FLASH_ATTR PacketQ(crypto::Crypto &crypto, time_t packet_max_age)
        : crypto(crypto), que(), packet_max_age(packet_max_age)
    {}

    struct Item {
        void clear() {
            addr = -1;
            time  = 0;
            packet.clear();
        }

        Item() = default;
        Item(const Item &) = delete;
        Item &operator=(const Item &) = delete;


        int8_t addr = -1;
        Packet packet;
        time_t time = 0;
    };

    //
    void clear() {
        for (int i = 0; i < PACKET_QUEUE_LEN; ++i) {
            que[i].clear();
        }

        // let's hope someone wasn't sending something...
        sending = nullptr;
        prologue.clear();
        cmac.clear();
    }

    uint8_t ICACHE_FLASH_ATTR get_update_count(uint8_t addr) {
        uint8_t cntr = 0;
        for (int i = 0; i < PACKET_QUEUE_LEN; ++i) {
            cntr += (que[i].addr == addr) ? 1 : 0;
        }
        return cntr;
    }

    /// insert into queue or return nullptr if full
    /// returns packet structure to be filled with data
    Packet * ICACHE_FLASH_ATTR want_to_send_for(uint8_t addr, uint8_t bytes, time_t curtime) {
#ifdef VERBOSE
        DBG(" * Q APP %p", this);
#endif
        for (int i = 0; i < PACKET_QUEUE_LEN; ++i) {
            // reverse index - we queue top first, send bottom first
            // to have fair queueing
            int ri = PACKET_QUEUE_LEN - 1 - i;
            Item &it = que[ri];

            if (addr != SYNC_ADDR && (it.addr == addr)) {
                if (it.packet.free_size() > bytes) {
#ifdef VERBOSE
                    DBG(" * Q APPEND [%d] %d", ri, addr);
#endif
                    return &it.packet;
                }
            }

            // free spot or too old packet
            if ((it.addr == -1) || (it.time + packet_max_age < curtime)) {
#ifdef VERBOSE
                DBG(" * Q NEW [%d] %d", ri, addr);
#endif
                it.addr = addr;
                it.time = curtime;
                it.packet.clear();
                return &it.packet;
            }
        }

        // full
        ERR(QUEUE_FULL);
        return nullptr;
    }

    // prepares queue to send data for address addr, if there's a
    // prepared packet present
    bool ICACHE_FLASH_ATTR prepare_to_send_to(uint8_t addr) {
        if (sending) {
            ERR(QUEUE_PREPARE_WHILE_SEND);
            return false;
        }

        for (unsigned i = 0; i < PACKET_QUEUE_LEN; ++i) {
            Item &it = que[i];
            if (it.addr == addr) {
#ifdef VERBOSE
                DBG("(PREP SND %d)", i);
#endif
                sending = &it;
                bool isSync = (it.addr == SYNC_ADDR);
                // just something to not get handled while we're sending this
                it.addr = -2;

                prepare_prologue();

                // 1 is the length itself
                // length, highest byte indicates sync word
                // non-sync packet includes an address (see branch below)
                uint8_t lenbyte = 1 + it.packet.size() + crypto::CMAC::CMAC_SIZE;

                cmac.clear();

                // non-sync packets have to be encrypted as well
                if (!isSync) {
                    ++lenbyte; // we're pushing address so we extend length
                    prologue.push(lenbyte);
                    prologue.push(MASTER_ADDR);

                    crypto.encrypt_decrypt(it.packet.data(), it.packet.size());

                    // non-sync packets include address in the cmac checksum
                    crypto.cmac_fill_addr(it.packet.data(),
                                          it.packet.size(),
                                          MASTER_ADDR, cmac);
                } else {
                    prologue.push(lenbyte | 0x80); // 0x80 indicates sync
                    crypto.cmac_fill_sync(it.packet.data(),
                                          it.packet.size(),
                                          cmac);
                }


                // dummy bytes, this gives the radio time to process the 16 bit
                // tx queue in time - we don't care if these get sent whole.
                cmac.push(0xAA); cmac.push(0xAA);

#ifdef VERBOSE
                hex_dump("PRLG", prologue.data(), prologue.size());
                hex_dump(" DTA", it.packet.data(), it.packet.size());
                hex_dump("CMAC", cmac.data(), cmac.size());
#endif
                return true;
            }
        }

#ifdef VERBOSE
        DBG("(NOTHING FOR %d)", addr);
#endif
        return false;
    }

    void prepare_prologue() {
        // TODO: this should probably be handled by Protocol class
        prologue.clear();
        prologue.push(0xaa); // just some gibberish
        prologue.push(0xaa);
        prologue.push(0x2d); // 2 byte sync word
        prologue.push(0xd4);
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

    // pops a char from queue after it sent well. returns if more data are present
    bool ICACHE_FLASH_ATTR pop() {
        if (!sending) return false;

        if (!prologue.empty()) {
            prologue.pop();
            return true;
        }

        if (!sending->packet.empty()) {
            sending->packet.pop();
            return true;
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
            return false;
        } else {
            return true;
        }
    }

    crypto::Crypto &crypto;
    Item que[PACKET_QUEUE_LEN];
    Item *sending = nullptr;
    ShortQ<6> prologue; // stores sync-word, size and optionally an address
    ShortQ<6> cmac; // stores cmac for sent packet, and 2 dummy bytes
    const time_t packet_max_age;
};

} // namespace hr20
