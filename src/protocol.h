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

#include "config.h"
#include "util.h"
#include "ntptime.h"
#include "packetqueue.h"
#include "model.h"

namespace hr20 {

// sent packet definition...
// RFM_FRAME_MAX is 80, we have to have enough space for that too
// NOTE: Technically both can be 76, since the RcvPacket does not contain
// prologue (0xaa, 0xaa, 0x2d, 0xd4 that gets eaten by the radio as sync-word)
// that does not apply to OpenHR20 since it shares send/recv buffer in one.
using RcvPacket = ShortQ<80>;

// sent packet is shorter, as we hold cmac in an isolated place
using SndPacket = PacketQ::Packet;

typedef std::function<void(uint8_t addr, ChangeCategory cat)> OnChangeCb;

// implements send/receive of the OpenHR20 protocol
struct Protocol {
    Protocol(Model &m, ntptime::NTPTime &time, crypto::Crypto &crypto, PacketQ &sndQ)
        : model(m), time(time), crypto(crypto), sndQ(sndQ)
    {}

    // bitfield
    enum Error {
        OK = 0, // no bit allocated for OK
        ERR_PROTO = 1,
        ERR_MODEL = 2
    };

    void ICACHE_FLASH_ATTR set_callback(const OnChangeCb &cb) {
        on_change_cb = cb;
    }

    /// verifies incoming packet, processes it accordingly
    void ICACHE_FLASH_ATTR receive(RcvPacket &packet) {
        rd_time = time.unixTime();

#ifdef VERBOSE
        DBG("== Will verify_decode packet of %d bytes ==", packet.size());
        hex_dump("PKT", packet.data(), packet.size());
#endif
        // length byte in packet contains the length byte itself (thus -1)
        size_t data_size = (packet[0] & 0x7f) - 1;
        bool   isSync    = (packet[0] & 0x80) != 0;

        if ((data_size + 1) > packet.size()) {
            ERR(PROTO_INCOMPLETE_PACKET);
            return;
        }

        // every packet has at-least length and CMAC (1+4)
        if (data_size < 5) {
            ERR(PROTO_PACKET_TOO_SHORT);
            return;
        }

        // pkt_cnt gets increased the number of times it was
        // increased in encrypt_decrypt by sender (not applicable for sync)
        uint8_t cnt_offset = isSync ? 0 : (packet.size() + 1) / 8;
        crypto.rtc.pkt_cnt += cnt_offset;

        bool ver = crypto.cmac_verify(
            reinterpret_cast<const uint8_t *>(packet.data() + 1),
            data_size - crypto::CMAC::CMAC_SIZE, isSync);

        // restore the pkt_cnt
        crypto.rtc.pkt_cnt -= cnt_offset;

#ifdef VERBOSE
        DBG(" %s%s PACKET VERIFICATION %s",
            ver    ? "*"       : "!",
            isSync ? " SYNC"   : "",
            ver    ? "SUCCESS" : "FAILURE");
#endif
        // verification failed? return
        if (!ver) {
            // bad packet might get special handling later on...
            ERR_ARG(PROTO_BAD_CMAC, packet[1]);
            on_failed_verify();
            return;
        }

        // log that we received a packet from client with address
        EVENT_ARG(PROTO_PACKET_RECEIVED, packet[1]);

        if (isSync) {
            process_sync_packet(packet);
        } else {
            if (packet.size() < 6) {
                ERR(PROTO_PACKET_TOO_SHORT);
                return;
            }
            // not a sync packet. we have to decode it
            crypto.encrypt_decrypt(
                    reinterpret_cast<uint8_t *>(packet.data()) + 2,
                    packet.size() - 6);

            crypto.rtc.pkt_cnt++;

#ifdef VERBOSE
            hex_dump(" * Decoded packet data", packet.data(), packet.size());
#endif
            if (!process_packet(packet)) {
                ERR(PROTO_CANNOT_PROCESS);
                hex_dump("PKT", packet.data(), packet.size());
            }
        }
    }

    void ICACHE_FLASH_ATTR update(time_t curtime,
                                  bool is_synced,
                                  bool changed_time,
                                  long slew)
    {
        // reset last_addr every second. this way we limit comms to 1 exchange
        // per second
        last_addr = 0xFF;

        // clear
        if (crypto.rtc.ss == 0) {
            sndQ.clear(); // everything old is lost by design.
        }

        if ((crypto.rtc.ss == 0 ||
             crypto.rtc.ss == 30))
        {
            if (crypto.rtc.ss == 0) {
                // display current time/date in full format
                DBG("(TM %04d-%02d-%02d (%d) %02d:%02d:%02d [%ld])",
                    year(curtime), month(curtime), day(curtime),
                    (int)((dayOfWeek(curtime) + 5) % 7 + 1),
                    hour(curtime), minute(curtime), second(curtime),
                    slew
                );
            }

#ifdef VERBOSE
            DBG(" * sync %d", crypto.rtc.ss);
#endif
            // send sync packet
            // TODO: set force flags is we want to comm with a specific client
            if (is_synced) {
                send_sync(curtime);
                // and immediately prepare to send it
                sndQ.prepare_to_send_to(PacketQ::SYNC_ADDR);
            }
        }
    }

    bool ICACHE_FLASH_ATTR no_forces() const {
        return last_force_count == 0;
    }

protected:
    bool ICACHE_FLASH_ATTR process_sync_packet(RcvPacket &packet) {
        if (packet.rest_size() < 1+4+4) {
            ERR(PROTO_PACKET_TOO_SHORT);
            return false;
        }

        // TODO: process force flags if present

        crypto::RTC decoded;
        decoded.YY = packet[1];
        decoded.MM = packet[2] >> 4;
        decoded.DD = (packet[3] >> 5) + ((packet[2] << 3) & 0x18);
        decoded.hh = packet[3] & 0x1f;
        decoded.mm = packet[4] >> 1;
        decoded.ss = packet[4] & 1 ? 30 : 00;

        DBG("(RCV SYNC %02d.%02d.%02d %02d:%02d:%02d)",
            decoded.DD, decoded.MM, decoded.YY,
            decoded.hh, decoded.mm, decoded.ss);

#ifndef NTP_CLIENT
        // is this local time or what?
        DBG("(SET TIME)");
        setTime(decoded.hh, decoded.mm, decoded.ss, decoded.DD, decoded.MM, decoded.YY);
#endif

        return true;
    }

    // processes packet from clients, returns false on error
    bool ICACHE_FLASH_ATTR process_packet(RcvPacket &packet) {
        // owned by object to save on now() calls and param passes
        // before the main loop, we trim the packet's mac and first 2 bytes
        packet.pop(); // skip length
        // sending device's address
        uint8_t addr = packet.pop();

        auto hr = model.prepare_client(addr);

        // the packet is valid, we can set last-contact time
        if (!hr) return false;

        hr->last_contact = rd_time;

        // eat up the MAC, it's already verified
        packet.trim(4);

        // indicates error in packet processing when nonzero (i.e. non OK)
        uint8_t err = OK;

        // bitmap of encountered events
        uint16_t bitmap = 0;

        while (!packet.empty()) {
            // the first byte here is command
            auto c = packet.pop();
//            bool resp = c & 0x80; // responses have highest bit set
            c &= 0x7f;

/*            if (!resp) {
                ERR(PROTO_CANNOT_PROCESS);
                return false;
            }
*/
            switch (c) {
            case 'V':
                bitmap |= PROTO_CMD_VER;
                err |= on_version(addr, packet); break;
                // these all respond with debug response
            case 'A': // Set temperatures response (debug)
                bitmap |= PROTO_CMD_TMP;
                err |= on_temperature(addr, packet); break;
            case 'D': // Debug command response
            case 'M': // Mode command response
                bitmap |= PROTO_CMD_DBG;
                err |= on_debug(addr, packet); break;
            case 'T': // Watch command response (reads watched variables from PGM)
                bitmap |= PROTO_CMD_WTCH;
                err |= on_watch(addr, packet); break;
            case 'R': // Read timers
            case 'W': // Write timers
                bitmap |= PROTO_CMD_TMR;
                err |= on_timers(addr, packet); break;
            case 'G': // get eeprom
            case 'S': // set eeprom
                bitmap |= PROTO_CMD_EEPROM;
                err |= on_eeprom(addr, packet); break;
            case 'L': // locked menu?
                bitmap |= PROTO_CMD_LOCK;
                err |= on_menu_lock(addr, packet); break;
            case 'B':
                bitmap |= PROTO_CMD_REBOOT;
                err |= on_reboot(addr, packet); break;
            default:
                ERR(PROTO_UNKNOWN_SEQUENCE);
                return false;
            }

            if (err != OK) {
                return false;
            }
        }

        EVENT_ARG(PROTO_HANDLED_OPS, bitmap);

        // inform send queue that we can send data for addr if we have any
        // we limit to one packet to each client every minute by this logic
        if (last_addr != addr) {
            // prepare for immediate response if possible - shortens discovery
            // time by 1 minute.
            queue_updates_for(addr, *hr);

            // how many packets are queued for the client?
            hr->need_fat_comms = (sndQ.get_update_count(addr) > 1);

            // if there's anything for the current address, we prepare to
            // send right away.
#ifdef VERBOSE
            bool haveData = sndQ.prepare_to_send_to(addr);
            DBG(" * prep: %s for %d", haveData ? "packet" : "nothing", addr);
#else
            sndQ.prepare_to_send_to(addr);
#endif
            last_addr = addr;
        }

        return err == OK;
    }

    void ICACHE_FLASH_ATTR on_failed_verify() {
        // TODO: Might immediately send sync as response to sync a stubborn HR20
        // with incompatible receive windows that does not hear normal Synces
    }

    Error ICACHE_FLASH_ATTR on_version(uint8_t addr, RcvPacket &p) {
        // spilled into serial if enabled, but othewise ignored
        // sequence of bytes terminated by \n
        while (1) {
            // packet too short or newline missing?
            if (p.empty()) {
                ERR(PROTO_RESPONSE_TOO_SHORT);
                return ERR_PROTO;
            }

            auto c = p.pop();

            // ending byte
            if (c == '\n') {
                return OK;
            }

            // todo: append this somewhere or what?
            // DBGI("(V %c)", c);
        }

        // will never get here
        return OK;
    }

    Error ICACHE_FLASH_ATTR on_temperature(uint8_t addr, RcvPacket &p) {
        if (p.rest_size() < 9) {
            ERR(PROTO_RESPONSE_TOO_SHORT);
            // reset the temp request status on the HR20 that reported this problem
            HR20 *hr = model[addr];
            if (!hr) return ERR_MODEL;
            hr->temp_wanted.reset_requested();
            return ERR_PROTO; // can't continue after this...
        }

        return on_debug(addr, p);
    }

    Error ICACHE_FLASH_ATTR on_debug(uint8_t addr, RcvPacket &p) {
        if (p.rest_size() < 9) {
            ERR(PROTO_RESPONSE_TOO_SHORT);
            return ERR_PROTO;
        }

        // TODO: Can't just store the value here, the client seems to accumulate
        // the debug packet responses.
        // Investigation is needed to determine if this is a side-effect of
        // mis-steps of this implementation or a normal behavior.
        // If it IS a normal behavior, we need to handle the value carefully

        // minutes. 0x80 is CTL_mode_auto(0 manual, 1 auto), 0x40 is CTL_test_auto
        uint8_t min_ctl   = p.pop();
        // seconds. 0x80 is menu_locked, 0x40 is mode_window(0 closed, 1 open)
        uint8_t sec_mm    = p.pop();
        uint8_t ctl_err   = p.pop();
        // 16 bit temp average
        uint8_t tmp_avg_h = p.pop();
        uint8_t tmp_avg_l = p.pop();
        // 16 bit batery measurement?
        uint8_t bat_avg_h = p.pop();
        uint8_t bat_avg_l = p.pop();
        // current wanted temperature (may be sourced from timer)
        uint8_t tmp_wtd   = p.pop();
        // wanted valve position
        uint8_t valve_wtd = p.pop();

        // TODO: implement calendar checksum handling here
        // * if the packet is longer it contains calendar checksum
        // * we then can introduce a passive mode that only refreshes
        // * calendar when the checksum differs.

        // fetch client from model
        HR20 *hr = model[addr];
        if (!hr) return ERR_MODEL;

        hr->auto_mode.set_remote(min_ctl & 0x80);
        hr->test_auto.set_remote(min_ctl & 0x40);
        hr->menu_locked.set_remote(sec_mm & 0x80);
        hr->mode_window.set_remote(sec_mm & 0x40);
        hr->temp_avg.set_remote(tmp_avg_h << 8 | tmp_avg_l);
        hr->bat_avg.set_remote(bat_avg_h << 8 | bat_avg_l);
        hr->temp_wanted.set_remote(tmp_wtd);
        hr->cur_valve_wtd.set_remote(valve_wtd);
        hr->ctl_err.set_remote(ctl_err);

#ifdef VERBOSE
        DBG(" * DBG RESP OK");
#endif

        // inform callback we had a change
        if (on_change_cb) on_change_cb(addr, CHANGE_FREQUENT);

        return OK;
    }

    Error ICACHE_FLASH_ATTR on_watch(uint8_t addr, RcvPacket &p) {
        if (p.rest_size() < 3) {
            ERR(PROTO_RESPONSE_TOO_SHORT);
            return ERR_PROTO;
        }

/*        uint8_t idx  = p.pop();
        uint16_t val = p.pop() << 8;
        val |= p.pop();*/

        // IGNORED, 8 bit slot, 16 bit value
        p.pop(); p.pop(); p.pop();

        return OK;
    }


    Error ICACHE_FLASH_ATTR on_timers(uint8_t addr, RcvPacket &p) {
        if (p.rest_size() < 3) {
            ERR(PROTO_RESPONSE_TOO_SHORT);
            return ERR_PROTO;
        }

        uint8_t idx  = p.pop();
        uint16_t val = p.pop() << 8;
        val |= p.pop();

        // val: time | (mode << 12). Stored packed here
        HR20 *hr = model[addr];
        if (!hr) return ERR_MODEL;

        uint8_t day  = idx >> 4;
        uint8_t slot = idx & 0xF;

        if (slot >= TIMER_SLOTS_PER_DAY) {
            ERR(PROTO_BAD_TIMER);
            return ERR_PROTO;
        }

        if (day >= TIMER_DAYS) {
            ERR(PROTO_BAD_TIMER);
            return ERR_PROTO;
        }

        hr->timers[day][slot].set_remote(val);

        if (on_change_cb) on_change_cb(addr, timer_day_2_change[day]);

        return OK;
    }

    Error ICACHE_FLASH_ATTR on_eeprom(uint8_t addr, RcvPacket &p) {
        if (p.rest_size() < 3) {
            ERR(PROTO_RESPONSE_TOO_SHORT);
            return ERR_PROTO;
        }

        uint8_t eeaddr = p.pop();
        uint8_t eeval  = p.pop();

        HR20 *hr = model[addr];
        if (!hr) return ERR_MODEL;

        hr->last_contact = rd_time;
        hr->eeprom.set_remote({eeaddr, eeval});

        // callback to publish the changes. we use frequent here, no big deal
        if (on_change_cb) on_change_cb(addr, CHANGE_EEPROM);

        return OK;
    }

    Error ICACHE_FLASH_ATTR on_menu_lock(uint8_t addr, RcvPacket &p) {
        if (p.rest_size() < 1) {
            ERR(PROTO_RESPONSE_TOO_SHORT);
            return ERR_PROTO;
        }

        uint8_t menu_locked = p.pop();

        HR20 *hr = model[addr];
        if (!hr) return ERR_MODEL;

        hr->last_contact = rd_time;
        hr->menu_locked.set_remote(menu_locked != 0);

        return OK;
    }

    Error ICACHE_FLASH_ATTR on_reboot(uint8_t addr, RcvPacket &p) {
        if (p.rest_size() < 2) {
            ERR(PROTO_RESPONSE_TOO_SHORT);
            return ERR_PROTO;
        }

        // fixed response. has to be 0x13, 0x24
        uint8_t b13 = p.pop();
        uint8_t b24 = p.pop();

        if (b13 != 0x13 || b24 != 0x24)
            return ERR_PROTO;

        return OK;
    }

    void ICACHE_FLASH_ATTR queue_updates_for(uint8_t addr, HR20 &hr) {
        bool synced = true;
        bool was_synced = hr.synced;

        // base values not yet read are not a reason for non-synced state
        // the reason is that we'll get them for free after the client
        // shows up
        unsigned flags = 0;

        // wanted temperature
        if (hr.temp_wanted.needs_write()) {
            synced = false;
            flags |= 1;
            send_set_temp(addr, hr.temp_wanted);
        }

        if (hr.auto_mode.needs_write()) {
            synced = false;
            flags |= 2;
            send_set_auto_mode(addr, hr.auto_mode);
        }

        if (hr.menu_locked.needs_write()) {
            synced = false;
            flags |= 4;
            send_set_menu_locked(addr, hr.menu_locked);
        }

        // read on eeprom?
        if (hr.eeprom.needs_read()) {
            synced = false;
            flags |= 8;
            send_get_eeprom(addr, hr.eeprom);
        } else if (hr.eeprom.needs_write()) {
            synced = false;
            flags |= 8;
            send_set_eeprom(addr, hr.eeprom);
        }

        // only allow queueing 8 timers to save time
        uint8_t tmr_ctr = MAX_QUEUE_TIMERS;

        // get timers if we don't have them, set them if change happened
        for (uint8_t dow = 0; dow < 8; ++dow) {
            for (uint8_t slot = 0; slot < TIMER_SLOTS_PER_DAY; ++slot) {
                auto &timer = hr.timers[dow][slot];
                if (timer.needs_read()) {
                    flags |= 16;
                    synced = hr.synced = false; // shortcut, we might return
                    send_get_timer(addr, dow, slot, timer);
                    if (!(--tmr_ctr)) return;
                }
                if (timer.needs_write()) {
                    flags |= 32;
                    synced = hr.synced = false;
                    send_set_timer(addr, dow, slot, timer);
                    if (!(--tmr_ctr)) return;
                }
            }
        }

        // maybe we didn't need anything? In that case consider the client
        // synced. We will skip any of these in the force flags/addresses
        hr.synced = synced;
        if (synced && !was_synced) {
            // report the client is fully synced
            DBG("(OK %d)", addr);
        } else {
            DBG("(WAIT %d %d)", addr, flags);
        }

        // nothing was sent, so send an acknowledge if needed
        if (synced) {
            send_ack(addr);
        }
    }

    void ICACHE_FLASH_ATTR send_ack(uint8_t addr) {
#ifdef VERBOSE
        DBG("   * ACK %u", addr);
#endif

        // 0 bytes just empty packet for the client
        // we acknowledge we know about the client this way
        SndPacket *p = sndQ.want_to_send_for(addr, 0, rd_time);
        if (!p) return;
    }

    void ICACHE_FLASH_ATTR send_set_temp(
            uint8_t addr, SyncedValue<uint8_t, cvt::TempHalfC> &temp_wanted)
    {
#ifdef VERBOSE
        DBG("   * TEMP %u", addr);
#endif

        uint8_t temp = temp_wanted.get_requested();

        if (temp < TEMP_MIN - 1) {
            ERR(PROTO_BAD_TEMP);
            temp_wanted.reset_requested();
        }

        if (temp > TEMP_MAX + 1) {
            ERR(PROTO_BAD_TEMP);
            temp_wanted.reset_requested();
        }

        // 2 bytes [A][xx] xx is in half degrees
        SndPacket *p = sndQ.want_to_send_for(addr, 2, rd_time);
        if (!p) return;

        p->push('A');
        p->push(temp_wanted.get_requested());
    }

    void ICACHE_FLASH_ATTR send_set_auto_mode(uint8_t addr,
                                              SyncedValue<bool> &auto_mode)
    {
#ifdef VERBOSE
        DBG("   * AUTO %u", addr);
#endif
        SndPacket *p = sndQ.want_to_send_for(addr, 2, rd_time);
        if (!p) return;

        p->push('M');
        p->push(auto_mode.get_requested() ? 1 : 0);
    }

    void ICACHE_FLASH_ATTR send_set_menu_locked(uint8_t addr,
                                                SyncedValue<bool> &menu_locked)
    {
#ifdef VERBOSE
        DBG("   * LOCK %u", addr);
#endif
        SndPacket *p = sndQ.want_to_send_for(addr, 2, rd_time);
        if (!p) return;

        p->push('L');
        p->push(menu_locked.get_requested() ? 1 : 0);
    }

    void ICACHE_FLASH_ATTR send_get_timer(uint8_t addr, uint8_t dow,
                                          uint8_t slot, TimerSlot &timer)
    {
#ifdef VERBOSE
        DBG("   * GET TIMER %u", addr);
#endif
        SndPacket *p = sndQ.want_to_send_for(addr, 2, rd_time);
        if (!p) return;

        p->push('R');
        p->push(dow << 4 | slot);
    }

    void ICACHE_FLASH_ATTR send_set_timer(uint8_t addr, uint8_t dow,
                                          uint8_t slot, TimerSlot &timer)
    {
#ifdef VERBOSE
        DBG("   * SET TIMER %u", addr);
#endif
        SndPacket *p = sndQ.want_to_send_for(addr, 4, rd_time);
        if (!p) return;

        p->push('W');
        p->push(dow << 4 | slot);
        p->push(timer.get_requested().raw() >> 8);
        p->push(timer.get_requested().raw() && 0xFF);
    }

    void ICACHE_FLASH_ATTR send_set_eeprom(
            uint8_t addr, SyncedValue<EEPROMReq> &eeprom)
    {
#ifdef VERBOSE
        DBG("   * EEPROM S %u", addr);
#endif
        SndPacket *p = sndQ.want_to_send_for(addr, 3, rd_time);
        if (!p) return;

        p->push('S');
        auto &req = eeprom.get_requested();
        p->push(req.address);
        p->push(req.value);
    }

    void ICACHE_FLASH_ATTR send_get_eeprom(
            uint8_t addr, SyncedValue<EEPROMReq> &eeprom)
    {
#ifdef VERBOSE
        DBG("   * EEPROM G %u", addr);
#endif
        SndPacket *p = sndQ.want_to_send_for(addr, 3, rd_time);
        if (!p) return;

        p->push('G');
        // address is validly stored in remote, eventhough complete value is
        // invalidated
        const auto &rem = eeprom.get_remote();
        p->push(rem.address);
    }


    void ICACHE_FLASH_ATTR send_sync(time_t curtime) {
#ifdef NTP_CLIENT
        SndPacket *p = sndQ.want_to_send_for(PacketQ::SYNC_ADDR, 8, curtime);
        if (!p) return;

        // TODO: force flags, if needed!
        auto &rtc = crypto.rtc;
        p->push(rtc.YY);
        p->push((rtc.MM << 4) | (rtc.DD >> 3));
        p->push((rtc.DD << 5) | rtc.hh);
        p->push(rtc.mm << 1 | (rtc.ss == 30 ? 1 : 0));

        // see if we need 1-2 or N valves synced
        // based on that knowledge, send flags or addresses
        ForceFlags ff;

        // only fill force flags on :30
        if (rtc.ss == 30) {
            for (uint8_t a = 0; a < MAX_HR_ADDR; ++a) {
                auto *hr = model[a];
                if (!hr) continue;

#ifdef VERBOSE
                DBG("(FF %d %d %d)",
                    a,
                    hr->last_contact,
                    ((!hr->synced) || hr->needs_basic_value_sync()) ? 1 : 0);
#endif
                if (hr->last_contact == 0) continue;
                if ((!hr->synced) || hr->needs_basic_value_sync()) {
                    ff.push(a, hr->need_fat_comms);
                }
            }
        }

        // write 2 byte force addrs or 4 byte sync flags
        ff.write(p);

        last_force_count = ff.count();
#endif
    }

    // ref to model of the network
    Model &model;

    // timer
    ntptime::NTPTime &time;

    // encryption tools
    crypto::Crypto &crypto;

    // callback that gets informed about changes
    OnChangeCb on_change_cb;

    // ref to packet queue responsible for packet retrieval for sending
    PacketQ &sndQ;

    // last address we sent a packet to - limits to 1 packet for every client
    // every minute
    uint8_t last_addr = 0xFF;

    /// count of forced addrs last time we iterated them in send_sync
    uint8_t last_force_count = 0;

    // current read time
    time_t rd_time;
};


} // namespace hr20
