#pragma once

#include <cstdint>

#include "crypto.h"
#include "queue.h"

// implements a packet queue
template<uint8_t LenT, typename PacketT>
struct PacketQ {
    PacketQ(crypto::Crypto &crypto) : crypto(crypto), que() {}

    struct Item {
        int8_t addr = -1;
        PacketT packet;
    };

    /// insert into queue or return nullptr if full
    /// returns packet structure to be filled with data
    PacketT *want_to_send_for(uint8_t addr, uint8_t bytes) {
        for (int i = 0; i < LenT; ++i) {
            // reverse index - we queue top first, send bottom first
            // to have fair queueing
            int ri = LenT - 1 - i;
            Item &it = que[ri];

            if (it.addr == addr) {
                if (it.packet.free_size() > bytes)
                    return &it.packet;
            }


            if (it.addr == -1) {
                it.addr = addr;
                return &it.packet;
            }
        }

        // full
        return nullptr;
    }

    // prepares queue to send data for address addr, if there's a
    // prepared packet present
    void prepare_to_send_to(uint8_t addr) {
        if (sending) return;

        for (unsigned i = 0; i < LenT; ++i) {
            Item &it = que[i];
            if (it.addr == addr) {
                sending = &it;
                // just something to not get handled while we're sending this
                it.addr = -2;
                // we need to sign this with cmac
                cmac.clear();
                crypto.cmac_fill(it.packet.data(), it.packet.size(), false, cmac);
                return;
            }
        }
    }

    int peek() {
        if (!sending) return -1;

        if (!sending->packet.empty())
            sending->packet.peek();

        if (cmac.empty()) return -1;
        return cmac.peek();
    }

    void pop() {
        if (sending) return;

        if (sending->packet.empty()) {
            if (cmac.empty()) {
                sending->addr = -1;
                sending = nullptr;
                cmac.clear();
            } else {
                cmac.pop();
            }
        } else {
            sending->packet.pop();
        }

    }

    crypto::Crypto &crypto;
    Item que[LenT];
    Item *sending = nullptr;
    ShortQ<4> cmac; // stores cmac for packet at sndIdx
};
