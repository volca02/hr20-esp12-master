#include <Arduino.h>

#ifdef WEB_SERVER
#include <ESP8266WebServer.h>
#endif

#ifdef MQTT
#include "mqtt.h"
#endif

#include "config.h"
#include "queue.h"
#include "crypto.h"
#include "packetqueue.h"
#include "rfm12b.h"
#include "wifi.h"
#include "ntptime.h"
#include "debug.h"
#include "converters.h"

// Sync count after RTC update.
// 255 is OpenHR20 default. (122 minutes after time sync)
// TODO: make time updates perpetual and resolve out of sync client issues
// constexpr const uint8_t SYNC_COUNT = 255;

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

// 2 minutes to resend the change request if we didn't yet get the value change confirmed
// this is now only used to throw away old packets in packet queue
constexpr const time_t RESEND_TIME = 2*60;

// Don't try re-reading every time. Skip a few packets in-between
constexpr const int8_t REREAD_CYCLES = 2;

// Don't try setting value every time. Skip a few packets in-between
constexpr const int8_t RESEND_CYCLES = 2;

// Max. number of timers queued per one packet exchange
constexpr const int8_t MAX_QUEUE_TIMERS = 8;

// Max. count of HR clients (and a max addr)
constexpr const uint8_t MAX_HR_COUNT = 29;

// Max. count of HR clients (and a max addr)
#define c2temp(c) (c*2)
constexpr const uint8_t TEMP_MIN = c2temp(5);
constexpr const uint8_t TEMP_MAX = c2temp(30);

/// Delays re-requests a number of skips. Used to delay re-requests/re-submits of values
template<int8_t RETRY_SKIPS>
struct RequestDelay {
    // starts paused
    RequestDelay() : counter(-1) {}

    bool should_retry() {
        if (counter < 0)
            return false;

        if (counter == 0) {
            counter = RETRY_SKIPS;
            return true;
        }

        counter--;
        return false;
    }

    void pause() { counter = -1; }
    void resume() { counter = 0; }
    bool is_paused() const { return counter < 0; }
private:
    int8_t counter;
};

// 8bit flags packed in uint8_t
struct Flags {
    class const_accessor;

    struct accessor {
        friend class const_accessor;

        accessor(Flags &f, uint8_t idx) : f(f), idx(idx)
        {}

        operator bool() const {
            return f.val & (1 << idx);
        }

        bool operator=(bool b) {
            auto m = mask();
            f.val = (f.val & ~m) | (b ? m : 0);
            return operator bool();
        }

    protected:
        uint8_t mask() const { return 1 << idx; }
        Flags &f;
        uint8_t idx;
    };

    struct const_accessor {
        const_accessor(accessor &a) : f(a.f), idx(a.idx) {}
        const_accessor(const Flags &f, uint8_t idx) : f(f), idx(idx)
        {}

        operator bool() const {
            return f.val & (1 << idx);
        }

    protected:
        uint8_t mask() const { return 1 << idx; }
        const Flags &f;
        uint8_t idx;
    };

    const_accessor operator[](uint8_t flag) const {
        return {*this, flag};
    }

    accessor operator[](uint8_t flag) {
        return {*this, flag};
    }

private:
    uint8_t val = 0;
};

// read-only value that gets reported by HR20 (not set back, not changeable)
template<typename T, typename CvT = cvt::Simple>
struct CachedValue {
    using Converter = CvT;

    // bitfield flags. internal rep to save space instead of bools...
    enum ValueFlags {
        REMOTE_VALID = 1,
        PUBLISHED    = 2,
        SUBSCRIBED   = 4
    };

    CachedValue() : reread_ctr() {
        reread_ctr.resume(); // we need to know the value immediately if possible
    }

    bool ICACHE_FLASH_ATTR needs_read() {
        // TODO: Too old values could be re-read here by forcing true return val
        return (!remote_valid()) && reread_ctr.should_retry();
    }

    void ICACHE_FLASH_ATTR set_remote(T val) {
        remote_valid() = true;
        if (remote != val) published() = false;
        remote = val;
        reread_ctr.pause();
    }

    T ICACHE_FLASH_ATTR get_remote() const { return remote; }

    Flags::accessor published() {
        return flags[PUBLISHED];
    }

    Flags::const_accessor published() const {
        return flags[PUBLISHED];
    }

    Flags::accessor subscribed() {
        return flags[SUBSCRIBED];
    }

    Flags::const_accessor subscribed() const {
        return flags[SUBSCRIBED];
    }

    Flags::accessor remote_valid() {
        return flags[REMOTE_VALID];
    };

    Flags::const_accessor remote_valid() const {
        return flags[REMOTE_VALID];
    }

    ICACHE_FLASH_ATTR String to_str() const {
        return Converter::to_str(remote);
    }

protected:

    Flags flags;
    RequestDelay<REREAD_CYCLES> reread_ctr;
    T remote;
};

// implements a pair of values - local/remote, that is used to detect
// changes have to be written through to client.
template<typename T, typename CvT = cvt::Simple>
struct SyncedValue : public CachedValue<T, CvT> {
    using Converter = CvT;

    bool ICACHE_FLASH_ATTR needs_write() {
        return (!is_synced()) && (resend_ctr.should_retry());
    }

    T& ICACHE_FLASH_ATTR get_requested() { return requested; }
    const T& ICACHE_FLASH_ATTR get_requested() const { return requested; }

    void ICACHE_FLASH_ATTR set_requested(T val) {
        requested = val;

        // don't set requested if we already hold the same value
        if (!this->needs_read() && is_synced()) return;

        // resume the send requests
        resend_ctr.resume();
    }

    void ICACHE_FLASH_ATTR reset_requested() {
        this->requested = this->remote;
        resend_ctr.pause();
    }

    // override for synced values - confirmations reset req_time
    void ICACHE_FLASH_ATTR set_remote(T val) {
        // set over a known prev. value should reset set requests
        bool diff = this->remote != val && this->remote_valid();

        CachedValue<T, CvT>::set_remote(val);
        // if none was requested in the meantime, also set requested
        if (this->resend_ctr.is_paused()) {
            this->requested = this->remote;
        } else {
            // set went through or was overwritten
            if (is_synced() || diff) {
                // don't want to set any more
                this->resend_ctr.pause();
                // sync the value just in case it was a diff
                this->requested = this->remote;
            }
        }
    }

    // returns true for values that don't have newer change request
    bool ICACHE_FLASH_ATTR is_synced() const {
        return this->remote == this->requested;
    }

    ICACHE_FLASH_ATTR void set_requested_from_str(const String &val) {
        set_requested(Converter::from_str(val));
    }

private:
    T requested;
    RequestDelay<RESEND_CYCLES> resend_ctr;
};

// rfm version of OpenHR20 supports 8 timers a day
constexpr const uint8_t TIMER_SLOTS_PER_DAY = 8;

// timer has 7 slots for days and 1 slot extra for repeated everyday mode
constexpr const uint8_t TIMER_DAYS = 8;

struct Timer {
    Timer(uint16_t val = 0) : timer(val) {}

    uint8_t hour() const {
        return time() / 60;
    }

    uint8_t min() const {
        return time() % 60;
    }

    uint16_t time() const {
        return (timer & 0x0fff);
    }

    uint8_t mode() const {
        return timer >> 12;
    }

    void set_hour(uint8_t shour) {
        set(mode(), shour, min());
    }

    void set_min(uint8_t smin) {
        set(mode(), hour(), smin);
    }

    void set_mode(uint8_t smode) {
        set(smode, hour(), min());
    }

    void set_time(uint8_t shour, uint8_t smin) {
        set(mode(), shour, smin);
    }

    void set_time(uint16_t time) {
        set(mode(), time / 60, time % 60);
    }

    void set(uint8_t shour, uint8_t smin, uint8_t smode) {
        // some mandatory fixes to stop producing crap timers
        shour = shour % 24; // wraparound in one day

        if (smin >= 60) // no good way to handle this...
            smin = 0;

        timer = smode << 12 | (shour * 60 + smin);
    }

    uint16_t raw() const { return timer; }

    uint16_t operator=(uint16_t val) {
        return timer = val;
    }

    bool operator==(const Timer &other) const { return timer == other.timer; }
    bool operator!=(const Timer &other) const { return timer != other.timer; }

protected:
    uint16_t timer;
};

using TimerSlot = SyncedValue<Timer>;

/*
uint16_t encode_timer(uint8_t hr, uint8_t min, uint8_t mode) {
    return (uint16_t(mode) << 12) | (uint16_t(hr)*60 + min);
}

void decode_timer(uint16_t timer, uint8_t &hr, uint8_t &min, uint8_t &mode) {
    hr   = (timer & 0x0fff) / 60;
    min  = (timer & 0x0fff) % 60;
    mode = timer >> 12;
}
*/
// categorizes the changes in HR20 model
enum ChangeCategory {
    CHANGE_FREQUENT = 1,
    CHANGE_TIMER_MASK = 0x1FE, // mask of timer changes
    CHANGE_TIMER_0  = 2, // 8 bits of timers correspond to 8 days
    CHANGE_TIMER_1  = 4,
    CHANGE_TIMER_2  = 8,
    CHANGE_TIMER_3  = 16,
    CHANGE_TIMER_4  = 32,
    CHANGE_TIMER_5  = 64,
    CHANGE_TIMER_6  = 128,
    CHANGE_TIMER_7  = 256
};

ChangeCategory timer_day_2_change[8] = {
    CHANGE_TIMER_0,
    CHANGE_TIMER_1,
    CHANGE_TIMER_2,
    CHANGE_TIMER_3,
    CHANGE_TIMER_4,
    CHANGE_TIMER_5,
    CHANGE_TIMER_6,
    CHANGE_TIMER_7
};

uint8_t change_get_timer_mask(uint16_t change) {
    return (change & CHANGE_TIMER_MASK) >> 1;
}

// models a single HR20 client
struct HR20 {
    time_t last_contact = 0; // last contact
    bool synced = false;     // we have fully populated copy of values if true

    // == Controllable values ==
    // these are mirrored values - we sync them to HR20 when a change is requested
    // but only after we verify (read_time != 0) that we know them to differ

    // temperature wanted - update A[XX] (0.5 degres accuracy)
    SyncedValue<uint8_t, cvt::TempHalfC>  temp_wanted;
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
    // average temperature [/0.01]C
    CachedValue<uint16_t, cvt::Temp001C> temp_avg;
    // average battery - [/0.001]V
    CachedValue<uint16_t, cvt::Voltage0001V> bat_avg;
    // desired valve position
    CachedValue<uint8_t>  cur_valve_wtd;
    // controller error
    CachedValue<uint8_t>  ctl_err;

    ICACHE_FLASH_ATTR void set_timer_mode(uint8_t day, uint8_t slot, const char *val) {
        if (day >= TIMER_DAYS) return;
        if (slot >= TIMER_SLOTS_PER_DAY) return;

        timers[day][slot].get_requested().set_mode(atoi(val) & 0x0F);
    }

    ICACHE_FLASH_ATTR void set_timer_time(uint8_t day, uint8_t slot, const char *val) {
        if (day >= TIMER_DAYS) return;
        if (slot >= TIMER_SLOTS_PER_DAY) return;

        uint8_t h, m;

        // time is expected in minutes of the day
        // might implement a better parser later
        uint16_t mins = atoi(val);
        h = mins / 60;
        m = mins % 60;

        timers[day][slot].get_requested().set_time(h, m);
    }
};

/// Accumulates non-synced client addrs for sync force flags/addrs
struct ForceFlags {

    ICACHE_FLASH_ATTR void push(uint8_t addr) {
        if (addr >= MAX_HR_COUNT) return;

        if (ctr < 2) {
            small[ctr] = addr;
        }

        ctr++;
        big += 1 << addr;
    }

    template<typename SyncP>
    ICACHE_FLASH_ATTR void write(SyncP &p) const {
        DBG("(FORCE %u)", big);
        if (ctr <= 2) {
            p->push(small[0]);
            p->push(small[1]);
        } else {
            // ATMega should be little endian
            p->push( big        & 0x0FF);
            p->push((big >>  8) & 0x0FF);
            p->push((big >> 16) & 0x0FF);
            p->push((big >> 24) & 0x0FF);
        }
    }

    uint8_t ctr = 0;
    uint8_t small[2] = {0,0};
    uint32_t big = 0;
};

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
        ERR_PROTO = 1,
        ERR_MODEL = 2
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
        DBG("(QUEUE)");
        for (unsigned i = 0; i < MAX_HR_COUNT; ++i) {
            HR20 *hr = model[i];

            if (!hr) continue; // whatever
            if (hr->last_contact == 0) continue; // not yet seen client

            // we might have some changes to queue
            DBG("(Q %u)", i);
            queue_updates_for(i, *hr);
        }
    }

    void ICACHE_FLASH_ATTR update(time_t curtime,
                                  bool changed_time)
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
                DBG("(TM %04d-%02d-%02d (%d) %02d:%02d:%02d)",
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
            case 'A': // Set temperatures response (debug)
                err |= on_temperature(addr, packet); break;
            case 'D': // Debug command response
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
            ERR("BAD TEMP %d", addr);
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
            ERR("SHORT DBG");
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
            ERR("SHORT WATCH");
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
            ERR("SHORT TMR");
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
            // slot num too high
            ERR("Timer slot too high %hu", slot);
            return ERR_PROTO;
        }

        if (day >= TIMER_DAYS) {
            // slot num too high
            ERR("Timer day too high %hu", day);
            return ERR_PROTO;
        }

        hr->timers[day][slot].set_remote(val);

        if (on_change_cb) on_change_cb(addr, timer_day_2_change[day]);

        return OK;
    }

    Error ICACHE_FLASH_ATTR on_eeprom(uint8_t addr, RcvPacket &p) {
        if (p.rest_size() < 2) {
            ERR("SHORT EEPROM");
            return ERR_PROTO;
        }

        // uint8_t idx = p.pop();
        // uint8_t val = p.pop();
        // IGNORED

        p.pop(); p.pop();

        return OK;
    }

    Error ICACHE_FLASH_ATTR on_menu_lock(uint8_t addr, RcvPacket &p) {
        if (p.rest_size() < 1) {
            ERR("SHORT MLCK");
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
            ERR("SHORT REBOOT");
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

        // wanted temperature
        if (hr.temp_wanted.needs_write()) {
            synced = false;
            send_set_temp(addr, hr.temp_wanted);
        }

        if (hr.auto_mode.needs_write()) {
            synced = false;
            send_set_auto_mode(addr, hr.auto_mode);
        }

        // only allow queueing 8 timers to save time
        uint8_t tmr_ctr = MAX_QUEUE_TIMERS;

        // get timers if we don't have them, set them if change happened
        for (uint8_t dow = 0; dow < 8; ++dow) {
            for (uint8_t slot = 0; slot < TIMER_SLOTS_PER_DAY; ++slot) {
                auto &timer = hr.timers[dow][slot];
                if (timer.needs_read()) {
                    synced = hr.synced = false; // shortcut, we might return
                    send_get_timer(addr, dow, slot, timer);
                    if (!(--tmr_ctr)) return;
                }
                if (timer.needs_write()) {
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
        }
    }

    void ICACHE_FLASH_ATTR send_set_temp(
            uint8_t addr, SyncedValue<uint8_t, cvt::TempHalfC> &temp_wanted)
    {
#ifdef VERBOSE
        DBG("   * TEMP %u", addr);
#endif

        uint8_t temp = temp_wanted.get_requested();

        if (temp < TEMP_MIN - 1) {
            ERR("Temp too low %d", temp);
            temp_wanted.reset_requested();
        }

        if (temp > TEMP_MAX + 1) {
            ERR("Temp too high %d", temp);
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
        // 2 bytes [A][xx] xx is in half degrees
        SndPacket *p = sndQ.want_to_send_for(addr, 2, rd_time);
        if (!p) return;

        p->push('M');
        p->push(auto_mode.get_requested() ? 1 : 0);
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

    void ICACHE_FLASH_ATTR send_sync(time_t curtime) {
#ifdef NTP_CLIENT
        SndPacket *p = sndQ.want_to_send_for(SendQ::SYNC_ADDR, 8, curtime);
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
            for (uint8_t a = 0; a < MAX_HR_COUNT;++a) {
                auto *hr = model[a];
                if (!hr) continue;
                if (hr->last_contact == 0) continue;
                if (!hr->synced) ff.push(a);
            }
        }

        // write 2 byte force addrs or 4 byte sync flags
        ff.write(p);
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

    bool ICACHE_FLASH_ATTR update(bool changed_time) {
        radio.poll();

        // Note: could use [[maybe_unused]] in C++17
        bool __attribute__((unused)) sec_pass = crypto.update(); // update the crypto rtc if needed

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

    // RTC with NTP synchronization
    Config &config;
    ntptime::NTPTime &time;
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
    ICACHE_FLASH_ATTR MQTTPublisher(Config &config, ntptime::NTPTime &time,
                                    HR20Master &master)
        : config(config), time(time), master(master), wifiClient(),
          client(wifiClient)
    {
      for (uint8_t i = 0; i < MAX_HR_COUNT; ++i)
        states[i] = 0;
    }

    ICACHE_FLASH_ATTR void begin() {
        client.setServer(config.mqtt_server, config.mqtt_port);
        client.setCallback([&](char *topic, byte *payload, unsigned int length)
                           {
                               callback(topic, payload, length);
                           });
        // every change gets a bitmask info here
        master.proto.set_callback([&](uint8_t addr, uint32_t mask) {
                                      states[addr] |= mask;
                                  });
    }

    ICACHE_FLASH_ATTR bool reconnect() {
        if (!client.connected()) {
            DBG("(MQTT CONN)");
            // TODO: only try this once a few seconds or so
            // just store last time we did it and retry
            // if we get over retry time
            if (!client.connect(config.mqtt_client_id)) {
                ERR("MQTT conn. err.");
                return false;
            }
        }
        return true;
    }

    enum StateMajor {
        STM_FREQ        = 0,
        STM_TIMER       = 1,
        STM_NEXT_CLIENT = 2
    };

    ICACHE_FLASH_ATTR void update() {
        client.loop();

        // TODO: Try to reconnect in intervals. Don't block the main loop too
        // often
        if (!reconnect()) return;

        if (!states[addr]) {
            // no changes for this client
            // switch to next one and check here next loop
            next_client();
            return;
        }

        switch (state_maj) {
        case STM_FREQ:
            publish_frequent();
            break;
        case STM_TIMER:
            publish_timers();
            break;
        default:
            next_client();
        }
    }

    ICACHE_FLASH_ATTR void next_client() {
        // process one client per loop call (i.e. per second)
        ++addr;

        // wraparound
        if (addr >= MAX_HR_COUNT) addr = 0;

        // reset the major/minor state indicators
        state_maj = STM_FREQ;
        state_min = 0;
    }

    ICACHE_FLASH_ATTR void next_major() {
        state_min = 0;
        ++state_maj;
        if (state_maj >= STM_NEXT_CLIENT) next_client();
    }

    template <typename T, typename CvT>
    ICACHE_FLASH_ATTR void publish_subscribe(const Path &p,
                                             CachedValue<T, CvT> &val) const
    {
        String path = p.compose();
        if (!val.published() && val.remote_valid()) {
            client.publish(path.c_str(), val.to_str().c_str());
            val.published() = true;
        }

        if (!val.subscribed()) {
            client.subscribe(path.c_str());
            val.subscribed() = true;
        }
    }

    // timer publishes and subscribes two topics, so we overload for it...
    ICACHE_FLASH_ATTR void publish_subscribe(const Path &p, CachedValue<Timer> &val) const
    {
        // clone paths and set the two possile endings for them
        Path mode_path{p};
        Path time_path{p};

        mode_path.timer_topic = mqtt::TIMER_MODE;
        time_path.timer_topic = mqtt::TIMER_TIME;

        // format the paths to strings
        String mode_p_str = mode_path.compose();
        String time_p_str = time_path.compose();

        if (!val.published() && val.remote_valid()) {
            String smode(val.get_remote().mode());
            String stime(val.get_remote().time());
            client.publish(mode_p_str.c_str(), smode.c_str());
            client.publish(time_p_str.c_str(), stime.c_str());
            val.published() = true;
        }

        if (!val.subscribed()) {
            client.subscribe(mode_p_str.c_str());
            client.subscribe(time_p_str.c_str());
            val.subscribed() = true;
        }
    }

    template <typename T, typename CvT>
    ICACHE_FLASH_ATTR void publish(const Path &p,
                                   CachedValue<T, CvT> &val) const
    {
        if (val.published())
            return;

        String path = p.compose();
        client.publish(path.c_str(), val.to_str().c_str());
        val.published() = true;
    }

    template <typename T>
    ICACHE_FLASH_ATTR void publish_subscribe(const Path &p, const T &val,
                                             bool is_published,
                                             bool is_subscribed) const
    {
        String sv(val);
        String path = p.compose();
        if (!is_published)  client.publish(path.c_str(), sv.c_str());
        if (!is_subscribed) client.subscribe(path.c_str());
    }

    template<typename T>
    ICACHE_FLASH_ATTR void publish(const Path &p, const T &val) const {
        String sv(val);
        String path = p.compose();
        client.publish(path.c_str(), sv.c_str());
    }

    ICACHE_FLASH_ATTR void publish_frequent() {
        if ((states[addr] & CHANGE_FREQUENT) == 0) {
            // no changes. advance...
            next_major();
            return;
        }

#ifdef VERBOSE
        // TOO VERBOSE
        DBG("(MF %u)", addr);
#endif

        auto *hr = master.model[addr];
        if (!hr) {
            ERR("Change on invalid client");
            return;
        }

        Path p{addr, mqtt::INVALID_TOPIC};

        switch (state_min) {
        case 0:
            p.topic = mqtt::MODE;
            publish_subscribe(p, hr->auto_mode);
            ++state_min;
            break;
        case 1:
            p.topic = mqtt::LOCK;
            publish_subscribe(p, hr->menu_locked);
            ++state_min;
            break;
        case 2:
            p.topic = mqtt::WND;
            publish(p, hr->mode_window);
            ++state_min;
            break;
        case 3:
            // TODO: this is in 0.01 of C, change it to float?
            p.topic = mqtt::AVG_TMP;
            publish(p, hr->temp_avg);
            ++state_min;
            break;
        case 4:
            // TODO: Battery is in 0.01 of V, change it to float?
            p.topic = mqtt::BAT;
            publish(p, hr->bat_avg);
            ++state_min;
            break;
        case 5:
            // TODO: Fix formatting for temp_wanted - float?
            // temp_wanted is in 0.5 C
            p.topic = mqtt::REQ_TMP;
            publish_subscribe(p, hr->temp_wanted);
            ++state_min;
            break;
        case 6:
            p.topic = mqtt::VALVE_WTD;
            publish(p, hr->cur_valve_wtd);
            ++state_min;
            break;
        // TODO: test_auto
        case 7:
            p.topic = mqtt::ERR;
            publish(p, hr->ctl_err);
            ++state_min;
            break;
        case 8:
            p.topic = mqtt::LAST_SEEN;
            publish(p, hr->last_contact);
            // fallthrough!
        case 9:
            // clear out the change bit
            states[addr] &= ~CHANGE_FREQUENT;
            next_major(); // moves to next major state
            break;
        }
    }

    ICACHE_FLASH_ATTR void publish_timers() {
        auto *hr = master.model[addr];
        if (!hr) {
            ERR("Change on invalid client");
            return;
        }

        if ((states[addr] & CHANGE_TIMER_MASK) == 0) {
            next_major();
            return;
        }

        // the minor state encodes day/slot
        uint8_t day  = state_min >> 3;
        uint8_t slot = state_min  & 0x7;
        ++state_min;

        uint8_t mask = change_get_timer_mask(states[addr]);

        // if we overshot the day counter, we switch to next
        // major slot (or client in this case)
        if (day >= 8) {
            next_major();
            return;
        }

        // the current day/slot is not changed?
        // visit the next day/slot next time
        if (!((1 << day) & mask)) {
            return;
        }

        // clear out the mask bit for the particular day if the last slot is hit
        if (slot == 7) states[addr] &= ~timer_day_2_change[day];

#ifdef VERBOSE
        // TOO VERBOSE
        DBG("(MT %u %u %u)", addr, day, slot);
#endif
        // only publish timers that have bit set in mask
        Path p{addr, mqtt::TIMER, mqtt::TIMER_NONE, day, slot};

        // TODO: Rework this to implicit conversion system
        publish_subscribe(p, hr->timers[day][slot]);
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
        case mqtt::REQ_TMP: hr->temp_wanted.set_requested_from_str(val); break;
        case mqtt::MODE: hr->auto_mode.set_requested_from_str(val); break;
        case mqtt::LOCK: hr->menu_locked.set_requested_from_str(val); break;
        case mqtt::TIMER: {
            // subswitch based on the timer topic
            switch (p.timer_topic) {
            case mqtt::TIMER_MODE: hr->set_timer_mode(p.day, p.slot, val); break;
            case mqtt::TIMER_TIME: hr->set_timer_time(p.day, p.slot, val); break;
            default: ERR("Non-settable timer topic change requested: %s", topic);
            }
            break;
        }
        default: ERR("Non-settable topic change requested: %s", topic);
        }

#ifdef VERBOSE
        DBG("(MQC %d %d)", p.addr, p.topic);
#endif
    }

    Config &config;
    ntptime::NTPTime &time;
    HR20Master &master;
    WiFiClient wifiClient;
    /// seriously, const correctness anyone? PubSubClient does not have single const method...
    mutable PubSubClient client;
    uint32_t states[MAX_HR_COUNT];

    // Publisher state machine
    uint8_t addr = 0;
    uint8_t state_maj = 0; // state category (FREQUENT, CALENDAR)
    uint8_t state_min = 0; // state detail (depends on major state)
};

} // namespace mqtt
#endif

Config config;

ntptime::NTPTime time;

HR20Master master{config, time};
int last_int = 1;

#ifdef WEB_SERVER
ESP8266WebServer server(80);
#endif

#ifdef MQTT
mqtt::MQTTPublisher publisher(config, time, master);
#endif


void setup(void) {
    Serial.begin(38400);

    // must be before wdtEnable
    bool loaded = config.begin("config.json");

    // this may change the config so it has to be at the top
    setupWifi(config, loaded);

    // set watchdog to 2 seconds.
    ESP.wdtEnable(2000);

    time.begin();
    master.begin();

    // TODO: this is perhaps useful for something (wifi, ntp) but not sure
    randomSeed(micros());

#ifdef MQTT
    // attaches the path's prefix to the setup value
    mqtt::Path::begin(config.mqtt_topic_prefix);
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
    bool changed_time = time.update();

    bool __attribute__((unused)) sec_pass = master.update(changed_time);

    // sec_pass = second passed (once every second)

#ifdef MQTT
    // only update mqtt if we have a time to do so, as controlled by master
    if (master.is_idle()) publisher.update();
#endif

    // feed the watchdog...
    ESP.wdtFeed();

#ifdef WEB_SERVER
    server.handleClient();
#endif
}
