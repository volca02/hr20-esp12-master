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

#include "debug.h"
#include "ntptime.h"
#include "protocol.h"
#include "rfm12b.h"
#include "crypto.h"
#include "packetqueue.h"

namespace hr20 {

/// Implements the state machine for packet sending/retrieval and radio control.
struct HR20Master {
    ICACHE_FLASH_ATTR HR20Master(Config &config, ntptime::NTPTime &tm)
        : config(config),
          time(tm),
          crypto{time},
          queue{crypto, RESEND_TIME},
          proto{model, time, crypto, queue}
    {}

    // has to be called after config.begin()!
    void ICACHE_FLASH_ATTR begin() {
        crypto.begin(config.rfm_pass);
        radio.begin();
    }

    bool ICACHE_FLASH_ATTR update(bool changed_time, time_t now) {
        radio.poll();

        // Note: could use [[maybe_unused]] in C++17
        // update the crypto rtc if needed
        bool __attribute__((unused)) sec_pass = crypto.update(now);

        // TODO: if it's 00 or 30, we send sync
        if (sec_pass) {
            DBGI("\r[:%d]", crypto.rtc.ss);
            time_t curtime = time.localTime();
            proto.update(curtime, changed_time);
        }

        // send data/receive data as appropriate
        send();
        receive();
        return sec_pass;
    }

    void ICACHE_FLASH_ATTR receive() {
        int b = radio.recv();

        // no data on input means we just ignore
        if (b < 0) return;

        if (!packet.push(b)) {
            ERR(PROTO_PACKET_TOO_LONG);
            wait_for_sync();
        }

        if (length == 0) {
            length = b & ~0x80;
            if (length == 0) {
                ERR(PROTO_EMPTY_PACKET);
                wait_for_sync();
                return;
            } else {
#ifdef VERBOSE
                DBG(" * Start rcv. of %d bytes", length);
#endif
            }
        }

        --length;

        if (!length) {
            DBG("(RCV %u)", packet.size());
            proto.receive(packet);
            wait_for_sync();
        }
    }

    bool ICACHE_FLASH_ATTR send() {
        while (true) {
            int b = queue.peek();

            if (b >= 0) {
                if (radio.send(b)) {
                    queue.pop();
                    if (queue.peek() < 0) DBG("(SNT)");
                } else {
                    // come back after the radio gets free
                    return true;
                }
            } else {
                // no more data...
                return false;
            }
        }
    }

    void ICACHE_FLASH_ATTR wait_for_sync() {
        packet.clear();
        radio.wait_for_sync();
    }

    /** indicates master has no time-sensitive work
     * going on, so we can do some time consuming updates that could break
     * radio comms.
     */
    bool ICACHE_FLASH_ATTR is_idle() {
        // every second if we have realtime
#ifndef NO_REALTIME
        auto millis = time.getMillis();
        // every second half of every second (not last 100 ms though, sending
        // can take time), but only if radio is idle
        return (millis >= 500) && (millis < 900) && radio.is_idle();
#endif
        auto sec = second(time.localTime());
        return (sec >= 50) && (sec <= 58) && radio.is_idle();
    }

    bool ICACHE_FLASH_ATTR can_update_ntp() {
        auto sec = second(time.localTime());
        return radio.is_idle() && (sec == 31) && proto.no_forces();
    }

    // RTC with NTP synchronization
    Config &config;
    ntptime::NTPTime &time;
    crypto::Crypto crypto;
    RFM12B radio;
    PacketQ queue;
    Model model;
    Protocol proto;

    // received packet
    RcvPacket packet;
    int length = 0;
};

} // namespace hr20
