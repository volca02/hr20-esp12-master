#include <Arduino.h>

#ifdef WEB_SERVER
#include <ESP8266WebServer.h>
#endif

#ifdef MQTT
#include "mqtt.h"
#endif

#include "queue.h"
#include "crypto.h"
#include "packetqueue.h"
#include "rfm12b.h"
#include "wifi.h"
#include "ntptime.h"
#include "debug.h"

// Sync count after RTC update.
// 255 is OpenHR20 default. (122 minutes after time sync)
// TODO: make time updates perpetual and resolve out of sync client issues
// constexpr const uint8_t SYNC_COUNT = 255;

// custom specified RFM password
// TODO: set this via -DRFM_PASS macros for convenient customization, or allow setting this via some web interface
constexpr const uint8_t RFM_PASS[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0x01, 0x23, 0x45};

// sent packet definition...
// RFM_FRAME_MAX is 80, we have to have enough space for that too
// NOTE: Technically both can be 76, since the RcvPacket does not contain
// prologue (0xaa, 0xaa, 0x2d, 0xd4 that gets eaten by the radio as sync-word)
// that does not apply to OpenHR20 since it shares send/recv buffer in one.
using RcvPacket = ShortQ<76>;
// sent packet is shorter, as we hold cmac in an isolated place
using SndPacket = PacketQ::Packet;

// sending packet queue
using SendQ = PacketQ;

// 10 minutes to resend the change request if we didn't yet get the value change confirmed
constexpr const time_t RESEND_TIME = 10*60;

// read-only value that gets reported by HR20 (not set back, not changeable)
template<typename T>
struct CachedValue {
    bool ICACHE_FLASH_ATTR needs_read(time_t now) const {
        return (this->read_time == 0)
            && ((now - RESEND_TIME) >= this->request_time);
    }
    void ICACHE_FLASH_ATTR set_remote(T val, time_t when) { remote = val; read_time = when; }
    T ICACHE_FLASH_ATTR get_remote() const { return remote; }
    void ICACHE_FLASH_ATTR set_request_time(time_t when) { request_time = when; }
protected:
    time_t request_time = 0;
    time_t read_time = 0;
    T remote;
};

// implements a pair of values - local/remote, that is used to detect
// changes have to be written through to client.
template<typename T>
struct SyncedValue : public CachedValue<T> {
    bool ICACHE_FLASH_ATTR needs_write(time_t now) const {
        // don't try to update values if we don't they don't match
        // also don't re-send update before RESEND_TIME elapses
        // and of course don't send updates if the requested value
        // matches what's on remote
        return (this->read_time != 0)
            && ((now - this->send_time) > RESEND_TIME)
            && (local != this->remote);
    }

    T ICACHE_FLASH_ATTR get_local() const { return local; }

    void ICACHE_FLASH_ATTR set_local(T val) { local = val; send_time = 0; }

    // sets the time we tried to update remote last time
    void ICACHE_FLASH_ATTR set_send_time(time_t t) { send_time = t; }

private:
    time_t send_time = 0;
    T local;
};

// I can only fit 4 timers per day in RAM here. should not matter much
// if someone needs those 8 timers that RFM HR20 supports, feel free
// to rework this code (maybe by using one slot variable per day)
constexpr const uint8_t TIMER_SLOTS_PER_DAY = 4;

// timer has 7 slots for days and 1 slot extra for repeated everyday mode
constexpr const uint8_t TIMER_DAYS = 8;

using TimerSlot = SyncedValue<uint16_t>;

uint16_t encode_timer(uint8_t hr, uint8_t min, uint8_t mode) {
    return (uint16_t(mode) << 12) | (uint16_t(hr)*60 + min);
}

void decode_timer(uint16_t timer, uint8_t &hr, uint8_t &min, uint8_t &mode) {
    hr   = (timer & 0x0fff) / 60;
    min  = (timer & 0x0fff) % 60;
    mode = timer >> 12;
}

// categorizes the changes in HR20 model
enum ChangeCategory {
    CHANGE_FREQUENT = 1,
    CHANGE_TIMERS   = 2
};

// models a single HR20
struct HR20 {
    time_t last_contact = 0; // last contact

    // == Controllable values ==
    // these are mirrored values - we sync them to HR20 when a change is requested
    // but only after we verify (read_time != 0) that we know them to differ

    // temperature wanted - update A[XX] (0.5 degres accuracy)
    SyncedValue<uint8_t>  temp_wanted;
    // automatic/manual temperature selection - M[01]/M[00]
    SyncedValue<bool>     auto_mode;
    // false unlocked, true locked - L[01]/L[00]
    SyncedValue<bool>     menu_locked;

    // Timers - 8*8 16 bit values total - 128 bytes per HR20
    // R [Xx]
    // W [Xx][MT][TT]
    // X - Day of week 1-7 OR 0 - everyday timer
    // x - slot (0-7)
    // T - timer mode
    // T - 12 bit TIME of day
    // TIME is encoded in serial minutes since midnight (H*60+M)
    // here, we're storing the MTTT sequence in compatible format
    // (M being the highest 4 bits of the 16bit value, 12 lowest bits are time)
    TimerSlot timers[TIMER_DAYS][TIMER_SLOTS_PER_DAY];

    // == Just read from HR20 - not controllable ==
    // true means auto mode with temperature equal to requested
    CachedValue<bool>     test_auto;
    // true means open window
    CachedValue<bool>     mode_window;
    // average temperature
    CachedValue<uint16_t> temp_avg;
    // average battery - [/0.001]V
    CachedValue<uint16_t> bat_avg;
    // desired valve position
    CachedValue<uint8_t>  cur_valve_wtd;
    // controller error
    CachedValue<uint8_t>  ctl_err;
};

constexpr const uint8_t MAX_HR_COUNT = 32;

struct Model {
    HR20 * ICACHE_FLASH_ATTR operator[](uint8_t idx) {
        if (idx >= MAX_HR_COUNT) {
            ERR("Client address out of range");
            return nullptr;
        }
        return &clients[idx];
    }

protected:
    HR20 clients[MAX_HR_COUNT];
};

typedef std::function<void(uint8_t addr, ChangeCategory cat)> OnChangeCb;

// implements send/receive of the OpenHR20 protocol
struct Protocol {
    Protocol(Model &m, ntptime::NTPTime &time, crypto::Crypto &crypto, SendQ &sndQ)
        : model(m), time(time), crypto(crypto), sndQ(sndQ)
    {}

    // bitfield
    enum Error {
        OK = 0, // no bit allocated for OK
        ERR_PROTO = 1
    };

    void ICACHE_FLASH_ATTR set_callback(const OnChangeCb &cb) {
        on_change_cb = cb;
    }

    /// verifies incoming packet, processes it accordingly
    void ICACHE_FLASH_ATTR receive(RcvPacket &packet) {
        rd_time = time.localTime();

#ifdef VERBOSE
        DBG("== Will verify_decode packet of %d bytes ==", packet.size());
        hex_dump("PKT", packet.data(), packet.size());
#endif

        // length byte in packet contains the length byte itself (thus -1)
        size_t data_size = (packet[0] & 0x7f) - 1;
        bool   isSync    = (packet[0] & 0x80) != 0;

        if ((data_size + 1) > packet.size()) {
            ERR("Incomplete packet received");
            return;
        }

        // every packet has at-least length and CMAC (1+4)
        if (data_size < 5) {
            ERR("Packet too short");
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
            ERR("BAD CMAC");
            on_failed_verify();
            return;
        }

        if (isSync) {
            process_sync_packet(packet);
        } else {
            if (packet.size() < 6) {
                ERR("Packet too short");
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
                ERR("packet processing failed");
                hex_dump("PKT", packet.data(), packet.size());
            }
        }
    }

    // call this in the right timespot (time to spare) to prepare commands to be sent to clients
    void ICACHE_FLASH_ATTR fill_send_queues() {
        DBG(" * fill send queues");
        for (unsigned i = 0; i < MAX_HR_COUNT; ++i) {
            HR20 *hr = model[i];

            if (!hr) continue; // whatever
            if (hr->last_contact == 0) continue; // not yet seen client

            // we might have some changes to queue
            DBG("   * fill %u", i);
            queue_updates_for(i, *hr);
        }
    }

    void ICACHE_FLASH_ATTR update(time_t curtime,
                                  bool changed_time)
    {
        // clear
        if (crypto.rtc.ss == 0) last_addr = 0xFF;

        if ((crypto.rtc.ss == 0 ||
             crypto.rtc.ss == 30))
        {
            if (crypto.rtc.ss == 0) {
                // display current time/date in full format
                DBG(" * %04d-%02d-%02d (%d) %02d:%02d:%02d",
                    year(curtime), month(curtime), day(curtime),
                    (int)((dayOfWeek(curtime) + 5) % 7 + 1),
                    hour(curtime), minute(curtime), second(curtime)
                );
            }

#ifdef VERBOSE
            DBG(" * sync %d", crypto.rtc.ss);
#endif
            // send sync packet
            // TODO: set force flags is we want to comm with a specific client
            send_sync(curtime);

            // and immediately prepare to send it
            sndQ.prepare_to_send_to(SendQ::SYNC_ADDR);
        }

        // right before the next minute starts,
        // we diff the model and fill the queues

        // TODO: Enable this again, but after we use the force flags in sync
        // We now queue requests as a reaction to incoming packet, but that
        // will cause delays when clients switch to low verbosity mode.
        // if (crypto.rtc.ss == 59) fill_send_queues();
    }

protected:
    bool ICACHE_FLASH_ATTR process_sync_packet(RcvPacket &packet) {
        if (packet.rest_size() < 1+4+4) {
            ERR("Sync packet too short");
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

        DBG(" * Decoded time from sync packet: %02d.%02d.%02d %02d:%02d:%02d",
            decoded.DD, decoded.MM, decoded.YY,
            decoded.hh, decoded.mm, decoded.ss);

#ifndef NTP_CLIENT
        // is this local time or what?
        DBG(" * Synchronizing time");
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

        auto hr = model[addr];
        // the packet is valid, we can set last-contact time
        if (hr) hr->last_contact = rd_time;

        // eat up the MAC, it's already verified
        packet.trim(4);

        // indicates error in packet processing when nonzero (i.e. non OK)
        uint8_t err = OK;

        while (!packet.empty()) {
            // the first byte here is command
            auto c = packet.pop();
//            bool resp = c & 0x80; // responses have highest bit set
            c &= 0x7f;

/*            if (!resp) {
                ERR("Master can't process commands");
                return false;
            }
*/
            switch (c) {
            case 'V':
                err |= on_version(addr, packet); break;
                // these all respond with debug response
            case 'D': // Debug command response
            case 'A': // Set temperatures response (debug)
            case 'M': // Mode command response
                err |= on_debug(addr, packet); break;
            case 'T': // Watch command response (reads watched variables from PGM)
                err |= on_watch(addr, packet); break;
            case 'R': // Read timers
            case 'W': // Write timers
                err |= on_timers(addr, packet); break;
            case 'G': // get eeprom
            case 'S': // set eeprom
                err |= on_eeprom(addr, packet); break;
            case 'L': // locked menu?
                err |= on_menu_lock(addr, packet); break;
            case 'B':
                err |= on_reboot(addr, packet);
            default:
                ERR("At %u: UNK. CMD %x", packet.pos(), (int)c);
                return false;
            }

            if (err != OK) {
                ERR("BAD PKT!");
                return false;
            }
        }

        // inform send queue that we can send data for addr if we have any
        // we limit to one packet to each client every minute by this logic
        if (last_addr != addr) {
            // prepare for immediate response if possible - shortens discovery
            // time by 1 minute.
            queue_updates_for(addr, *hr);

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
                ERR("SHORT VER");
                p.clear();
                return ERR_PROTO;
            }

            auto c = p.pop();

            // ending byte
            if (c == '\n') {
                return OK;
            }

            // todo: append this somewhere or what?
            DBGI("%c", c);
        }

        // will never get here
        return OK;
    }

    bool ICACHE_FLASH_ATTR on_debug(uint8_t addr, RcvPacket &p) {
        if (p.rest_size() < 9) {
            ERR("SHORT DBG");
            p.clear();
            return true;
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

        // fetch client from model
        HR20 *hr = model[addr];
        if (!hr) return ERR_PROTO;

        hr->auto_mode.set_remote(min_ctl & 0x80, rd_time);
        hr->test_auto.set_remote(min_ctl & 0x40, rd_time);
        hr->menu_locked.set_remote(sec_mm & 0x80, rd_time);
        hr->mode_window.set_remote(sec_mm & 0x40, rd_time);
        hr->temp_avg.set_remote(tmp_avg_h << 8 | tmp_avg_l, rd_time);
        hr->bat_avg.set_remote(bat_avg_h << 8 | bat_avg_l, rd_time);
        hr->temp_wanted.set_remote(tmp_wtd, rd_time);
        hr->cur_valve_wtd.set_remote(valve_wtd, rd_time);
        hr->ctl_err.set_remote(ctl_err, rd_time);

#ifdef VERBOSE
        DBG(" * DBG RESP OK");
#endif

        // inform callback we had a change
        if (on_change_cb) on_change_cb(addr, CHANGE_FREQUENT);

        return OK;
    }

    bool ICACHE_FLASH_ATTR on_watch(uint8_t addr, RcvPacket &p) {
        if (p.rest_size() < 3) {
            ERR("SHORT WATCH");
            p.clear();
            return ERR_PROTO;
        }

/*        uint8_t idx  = p.pop();
        uint16_t val = p.pop() << 8;
        val |= p.pop();*/

        // IGNORED, 8 bit slot, 16 bit value
        p.pop(); p.pop(); p.pop();

        return OK;
    }


    bool ICACHE_FLASH_ATTR on_timers(uint8_t addr, RcvPacket &p) {
        if (p.rest_size() < 3) {
            ERR("SHORT TMR");
            p.clear();
            return ERR_PROTO;
        }

        uint8_t idx  = p.pop();
        uint16_t val = p.pop() << 8;
        val |= p.pop();

        // val: time | (mode << 12). Stored packed here
        HR20 *hr = model[addr];
        if (!hr) return ERR_PROTO;

        uint8_t day  = idx >> 4;
        uint8_t slot = idx & 0xF;

        if (slot >= TIMER_SLOTS_PER_DAY) {
            // slot num too high
            ERR("Timer slot too high %hu", slot);
            return ERR_PROTO;
        }

        if (day >= TIMER_DAYS) {
            // slot num too high
            ERR("Timer day too high %hu", day);
            return ERR_PROTO;
        }

        hr->timers[day][slot].set_remote(val, rd_time);

        return OK;
    }

    bool ICACHE_FLASH_ATTR on_eeprom(uint8_t addr, RcvPacket &p) {
        if (p.rest_size() < 2) {
            ERR("SHORT EEPROM");
            p.clear();
            return ERR_PROTO;
        }

        // uint8_t idx = p.pop();
        // uint8_t val = p.pop();
        // IGNORED

        p.pop(); p.pop();

        return OK;
    }

    bool ICACHE_FLASH_ATTR on_menu_lock(uint8_t addr, RcvPacket &p) {
        if (p.rest_size() < 1) {
            ERR("SHORT MLCK");
            p.clear();
            return ERR_PROTO;
        }

        uint8_t menu_locked = p.pop();

        HR20 *hr = model[addr];
        if (!hr) return ERR_PROTO;

        hr->last_contact = rd_time;
        hr->menu_locked.set_remote(menu_locked != 0, rd_time);

        return OK;
    }

    bool ICACHE_FLASH_ATTR on_reboot(uint8_t addr, RcvPacket &p) {
        if (p.rest_size() < 2) {
            ERR("SHORT REBOOT");
            p.clear();
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
        // wanted temperature
        if (hr.temp_wanted.needs_write(rd_time))
            send_set_temp(addr, hr.temp_wanted);

        if (hr.auto_mode.needs_write(rd_time))
            send_set_auto_mode(addr, hr.auto_mode);

        // get timers if we don't have them, set them if change happened
        for (uint8_t dow = 0; dow < 8; ++dow) {
            for (uint8_t slot = 0; slot < TIMER_SLOTS_PER_DAY; ++slot) {
                auto &timer = hr.timers[dow][slot];
                if (timer.needs_read(rd_time))
                    send_get_timer(addr, dow, slot, timer);
                if (timer.needs_write(rd_time))
                    send_set_timer(addr, dow, slot, timer);
            }
        }
    }

    void ICACHE_FLASH_ATTR send_set_temp(uint8_t addr,
                                         SyncedValue<uint8_t> &temp_wanted)
    {
#ifdef VERBOSE
        DBG("   * TEMP %u", addr);
#endif
        // 2 bytes [A][xx] xx is in half degrees
        SndPacket *p = sndQ.want_to_send_for(addr, 2, rd_time);
        if (!p) return;

        p->push('A');
        p->push(temp_wanted.get_local());

        // set the time we last requested the change
        temp_wanted.set_send_time(rd_time);
    }

    void ICACHE_FLASH_ATTR send_set_auto_mode(uint8_t addr,
                                              SyncedValue<bool> &auto_mode)
    {
#ifdef VERBOSE
        DBG("   * AUTO %u", addr);
#endif
        // 2 bytes [A][xx] xx is in half degrees
        SndPacket *p = sndQ.want_to_send_for(addr, 2, rd_time);
        if (!p) return;

        p->push('M');
        p->push(auto_mode.get_local() ? 1 : 0);

        // set the time we last requested the change
        auto_mode.set_send_time(rd_time);
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

        // set the time we last requested the change
        timer.set_request_time(rd_time);
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
        p->push(timer.get_local() >> 8);
        p->push(timer.get_local() && 0xFF);

        // set the time we last requested the change
        timer.set_send_time(rd_time);
    }

    void ICACHE_FLASH_ATTR send_sync(time_t curtime) {
#ifdef NTP_CLIENT
        SndPacket *p = sndQ.want_to_send_for(SendQ::SYNC_ADDR, 4, curtime);
        if (!p) return;

        // TODO: force flags, if needed!
        auto &rtc = crypto.rtc;
        p->push(rtc.YY);
        p->push((rtc.MM << 4) | (rtc.DD >> 3));
        p->push((rtc.DD << 5) | rtc.hh);
        p->push(rtc.mm << 1 | (rtc.ss == 30 ? 1 : 0));

        // TODO: flags
        p->push(0x00);
        p->push(0x00);
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
    SendQ &sndQ;

    // last address we sent a packet to - limits to 1 packet for every client
    // every minute
    uint8_t last_addr = 0xFF;

    // current read time
    time_t rd_time;
};

/// Implements the state machine for packet sending/retrieval and radio control.
struct HR20Master {
    ICACHE_FLASH_ATTR HR20Master()
        : time{},
          crypto{RFM_PASS, time},
          queue{crypto, RESEND_TIME},
          proto{model, time, crypto, queue}
    {}

    void ICACHE_FLASH_ATTR begin() {
        time.begin();
        radio.begin();
    }

    void ICACHE_FLASH_ATTR update() {
        bool changed_time = time.update();
        radio.poll();

        // Note: could use [[maybe_unused]] in C++17
        bool __attribute__((unused)) sec_pass = crypto.update(); // update the crypto rtc if needed

        // TODO: if it's 00 or 30, we send sync
        if (sec_pass) {
            DBGI("\r(%d)", crypto.rtc.ss);
            time_t curtime = time.localTime();
            proto.update(curtime, changed_time);
        }

        // send data/receive data as appropriate
        send();
        receive();
    }

    void ICACHE_FLASH_ATTR receive() {
        int b = radio.recv();

        // no data on input means we just ignore
        if (b < 0) return;

        if (!packet.push(b)) {
            ERR("packet exceeds maximal length. Discarding");
            wait_for_sync();
        }

        if (length == 0) {
            length = b & ~0x80;
            if (length == 0) {
                ERR("Zero length packet");
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
            DBG("(RX %u)", packet.size());
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

    // RTC with NTP synchronization
    ntptime::NTPTime time;
    crypto::Crypto crypto;
    RFM12B radio;
    SendQ queue;
    Model model;
    Protocol proto;

    // received packet
    RcvPacket packet;
    int length = 0;
};


#ifdef MQTT
namespace mqtt {

struct MQTTPublisher {
    // TODO: Make this configurable!
    static constexpr const char *mqtt_server = "192.168.1.22";

    ICACHE_FLASH_ATTR MQTTPublisher(HR20Master &master) : master(master) {
        for (uint8_t i = 0; i < MAX_HR_COUNT; ++i)
            states[i] = 0;
    }

    ICACHE_FLASH_ATTR void begin() {
        client.setServer(mqtt_server, 1883);
        client.setCallback([&](char *topic, byte *payload, unsigned int length)
                           {
                               callback(topic, payload, length);
                           });
        // every change gets a bitmask info here
        master.proto.set_callback([&](uint8_t addr, uint32_t mask) {
                                      states[addr] |= mask;
                                  });
    }

    ICACHE_FLASH_ATTR void update() {
        for (uint8_t addr = 1; addr < MAX_HR_COUNT; ++addr) {
            auto chngs = states[addr]; states[addr] = 0;

            if (chngs & CHANGE_FREQUENT) {
                publish_frequent(addr);
            }
        }
    }

    template<typename T>
    ICACHE_FLASH_ATTR void publish(const Path &p, const T &val) const {
        String sv(val);
        client.publish(p.compose().c_str(), sv.c_str());
    }

    ICACHE_FLASH_ATTR void publish_frequent(uint8_t addr) const {
        auto *hr = master.model[addr];
        if (!hr) {
            ERR("Change on invalid client");
            return;
        }

        Path p{addr, mqtt::INVALID_TOPIC};

        p.topic = mqtt::MODE;
        publish(p, hr->auto_mode.get_remote());

        // TODO: test_auto

        p.topic = mqtt::LOCK;
        publish(p, hr->menu_locked.get_remote());

        p.topic = mqtt::WND;
        publish(p, hr->mode_window.get_remote());

        p.topic = mqtt::AVG_TMP;
        publish(p, hr->temp_avg.get_remote());

        p.topic = mqtt::BAT;
        publish(p, hr->bat_avg.get_remote());

        p.topic = mqtt::REQ_TMP;
        publish(p, hr->temp_wanted.get_remote());

        p.topic = mqtt::VALVE_WTD;
        publish(p, hr->cur_valve_wtd.get_remote());

        p.topic = mqtt::ERR;
        publish(p, hr->ctl_err.get_remote());
    }

    ICACHE_FLASH_ATTR void callback(char *topic, byte *payload,
                                    unsigned int length)
    {
        // only allowed on some endpoints. will switch through
        Path p = Path::parse(topic);

        if (!p.valid()) {
            ERR("Invalid topic %s", topic);
            return;
        }

        auto *hr = master.model[p.addr];
        if (!hr) {
            ERR("Callback to set on invalid client");
            return;
        }

#warning the temp_wanted setting is accurate to 1 degree now. not ideal!
        const char *val = reinterpret_cast<const char*>(payload);
        switch (p.topic) {
        case mqtt::REQ_TMP: hr->temp_wanted.set_local(atoi(val) * 2); break;
        case mqtt::MODE: hr->auto_mode.set_local(atoi(val)); break;
        case mqtt::LOCK: hr->menu_locked.set_local(atoi(val)); break;
        default: ERR("Non-settable topic change requested: %s", topic);
        }
    }

    /// seriously, const correctness anyone? PubSubClient does not have single const method...
    mutable PubSubClient client;
    HR20Master &master;
    uint8_t states[MAX_HR_COUNT];
};

} // namespace mqtt
#endif


HR20Master master;
int last_int = 1;

#ifdef WEB_SERVER
ESP8266WebServer server(80);
#endif

#ifdef MQTT
mqtt::MQTTPublisher publisher(master);
#endif


void setup(void) {
    Serial.begin(38400);
    master.begin();

    // TODO: this is perhaps useful for something (wifi, ntp) but not sure
    randomSeed(micros());

    setupWifi();

#ifdef MQTT
    publisher.begin();
#endif

#ifdef WEB_SERVER
    server.on("/",
              [](){
                  String status = "Valves + temperatures\n\n";
                  unsigned cnt = 0;
                  for (unsigned i = 0; i < MAX_HR_COUNT; ++i) {
                      auto m = master.model[i];

                      if (!m) continue;
                      if (m->last_contact == 0) continue;


                      ++cnt;
                      status += "Valve ";
                      status += String(i, HEX);
                      status += " - ";
                      status += String(float(m->temp_avg.get_remote()) / 100.0f, 2);
                      status += String('\n');
                  }

                  status += String('\n');
                  status += String(cnt);
                  status += " total";

                  server.send(200, "text/plain", status);
              });
    server.begin();
#endif
}


void loop(void) {
    master.update();
#ifdef WEB_SERVER
    server.handleClient();
#endif
}
