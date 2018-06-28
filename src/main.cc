#include <Arduino.h>
#include <SPI.h>

#include "queue.h"
#include "crypto.h"
#include "packetqueue.h"
#include "rfm12b.h"
#include "wifi.h"
#include "ntptime.h"

#ifdef DEBUG
#define DBGI(...) do { Serial.printf(__VA_ARGS__); } while (0)
#define DBG(...) do { Serial.printf(__VA_ARGS__); Serial.println(); } while (0)
// TODO: BETTER ERROR REPORTING
#define ERR(...) do { Serial.write("! ERR "); Serial.printf(__VA_ARGS__); Serial.println(); } while (0)
#else
#define DBGI(...) do { } while (0)
#define DBG(...) do { } while (0)
#define ERR(...) do { } while (0)
#endif

// Sync count after RTC update.
// 255 is OpenHR20 default. (122 minutes after time sync)
// TODO: make time updates perpetual and resolve out of sync client issues
#define SYNC_COUNT 255

// custom specified RFM password
// TODO: set this via -DRFM_PASS macros for convenient customization, or allow setting this via some web interface
constexpr const uint8_t rfm_pass[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0x01, 0x23, 0x45};

// RTC with NTP synchronization
ntptime::NTPTime time;

// sent packet definition...
// RFM_FRAME_MAX is 80, we have to have enough space for that too
using RcvPacket = ShortQ<80>;
// sent packet is shorter, as we hold cmac in an isolated place
using SndPacket = ShortQ<76>;

// this encompasses the encryption handling routines
crypto::Crypto crypt{rfm_pass, time};

// sending packet queue
using SendQ = PacketQ<32, SndPacket>;

int sync_count = 0;

// 10 minutes to resend the change request if we didn't yet get the value change confirmed
const time_t RESEND_TIME = 10*60;

RFM12B radio;

void setup(void) {
  Serial.begin(38400);

  //this is perhaps useful for somtething (wifi, ntp) but not sure
  randomSeed(micros());
  setupWifi();

  time.begin();

  // also calls SPI.begin()!
  radio.begin();
}

void HexDump(const char *prefix, const void *p, size_t size) {
    const char *ptr = reinterpret_cast<const char *>(p);
    DBGI("%s : [%d bytes] ", prefix, size);
    for(;size;++ptr,--size) {
        DBGI("%02x ", (uint8_t)*ptr);
    }
    DBG(".");
}

// read-only value that gets reported by HR20 (not set back, not changeable)
template<typename T>
struct CachedValue {
    bool needs_read() const { return read_time == 0; }
    void set_remote(T val, time_t when) { remote = val; read_time = when; }
    T get_remote() const { return remote; }
protected:
    time_t read_time = 0;
    T remote;
};

// implements a pair of values - local/remote
template<typename T>
struct SyncedValue : public CachedValue<T> {
    bool needs_write(time_t now) const {
        // don't try to update values if we don't they don't match
        // also don't re-send update before RESEND_TIME elapses
        // and of course don't send updates if the requested value
        // matches what's on remote
        return (this->read_time != 0)
            && ((now - this->send_time) > RESEND_TIME)
            && (local != this->remote);
    }

    T get_local() const { return local; }

    void set_local(T val) { local = val; send_time = 0; }

    // sets the time we tried to update remote last time
    void set_send_time(time_t t) { send_time = t; }

private:
    time_t send_time = 0;
    T local;
};

// models a single HR20
struct HR20 {
    HR20()
        : last_contact(0)
    {}

    time_t last_contact; // last contact

    // == Controllable values ==
    // these are mirrored values - we sync them to HR20 when a change is requested
    // but only after we verify (read_time != 0) that we know them to differ

    // temperature wanted - update A[XX] (0.5 degres accuracy)
    SyncedValue<uint8_t>  temp_wanted;
    // automatic/manual temperature selection - M[01]/M[00]
    SyncedValue<bool>     auto_mode;
    // false unlocked, true locked - L[01]/L[00]
    SyncedValue<bool>     menu_locked;

    // TODO:
    // weekly automatic temperature timer - R[DD], W[DD][XX][XX]
    // DD == week day (0=monday). XXXX - plan
    // TODO: LATER: DowTimer timer;

    // == Just read from HR20 - not controllable ==
    // true means auto mode with temperature equal to requested
    CachedValue<bool>     test_auto;
    // true means open window
    CachedValue<bool>     mode_window;
    // average temperature
    CachedValue<uint16_t> temp_avg;
    // average battery - [/0.001]V
    CachedValue<uint16_t> bat_avg;
    // TODO: merge this with temp_wanted? Are they fundamentally different?
    // current wanted temperature, degrees
    CachedValue<uint8_t>  cur_temp_wtd;
    // desired valve position
    CachedValue<uint8_t>  cur_valve_wtd;
};

constexpr const uint8_t MAX_HR_COUNT = 32;

struct Model {
    HR20 *operator[](uint8_t idx) {
        if (idx >= MAX_HR_COUNT) {
            DBG(" ! Client address out of range");
            return nullptr;
        }
        return &clients[idx];
    }

protected:
    HR20 clients[MAX_HR_COUNT];
};

// implements send/receive of the OpenHR20 protocol
struct Protocol {
    Protocol(Model &m, crypto::Crypto &crypto, SendQ &sndQ)
        : model(m), crypto(crypto), sndQ(sndQ)
    {}

    // bitfield
    enum Error {
        OK = 0, // no bit allocated for OK
        ERR_PROTO = 1
    };

    /// verifies incoming packet, processes it accordingly
    void receive(RcvPacket &packet) {
        rd_time = now();

        DBG("== Will verify_decode packet of %d bytes ==", packet.size());

        size_t data_size = packet.size() - 1;

        if (data_size != (packet[0] & 0x7f)) {
            DBG(" ! Incomplete packet received");
            return;
        }

        if (data_size < 5) {
            DBG(" ! packet too short");
            return;
        }

        bool isSync = (packet[0] & 0x80) != 0;

        // what is this magic used in original code?
        uint8_t cnt_offset = (packet.size() + 1) / 8;
        crypt.rtc.pkt_cnt += cnt_offset;

        bool ver = crypto.cmac_verify(
            reinterpret_cast<const uint8_t *>(packet.data() + 1), data_size - 5,
            isSync);

        // restore the pkt_cnt
        crypt.rtc.pkt_cnt -= cnt_offset;

        DBG(" %s%s PACKET VERIFICATION %s",
            ver    ? "*"       : "!",
            isSync ? " SYNC"   : "",
            ver    ? "SUCCESS" : "FAILURE");

        // verification failed? return
        if (!ver) {
            // bad packet might get special handling later on...
            on_failed_verify();
            return;
        }

        if (isSync) {
            process_sync_packet(packet);
        } else {
            if (packet.size() < 6) {
                DBG(" ! packet too short");
                return;
            }
            // not a sync packet. we have to decode it
            crypt.encrypt_decrypt(
                    reinterpret_cast<uint8_t *>(packet.data()) + 2,
                    packet.size() - 6);

            // return the packet counter back
            crypt.rtc.pkt_cnt++;

#ifdef DEBUG
            HexDump(" * Decoded packet data", packet.data(), packet.size());
#endif
            if (!process_packet(packet)) {
                ERR("packet processing failed");
            }
        }
    }

    // call this in the right timespot (time to spare) to prepare commands to be sent to clients
    void fill_send_queues() {
        for (unsigned i = 0; i < MAX_HR_COUNT; ++i) {
            HR20 *hr = model[i];

            if (!hr) continue; // whatever
            if (hr->last_contact == 0) continue; // not yet seen client

            // we might have some changes to queue
            queue_updates_for(i, *hr);
        }
    }

protected:
    bool process_sync_packet(RcvPacket &packet) {
        if (packet.size() < 1+4+4) {
            Serial.println(" ! Sync packet too short");
            return false;
        }

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

#ifdef CLIENT_MODE
        // is this local time or what?
        DBG(" * Synchronizing time");
        setTime(decoded.hh, decoded.mm, decoded.ss, decoded.DD, decoded.MM, decoded.YY);
        DBGI(" * "); time.printRTC();
#endif

        return true;
    }

    // processes packet from clients, returns false on error
    bool process_packet(RcvPacket &packet) {
        // owned by object to save on now() calls and param passes
        // before the main loop, we trim the packet's mac and first 2 bytes
        packet.pop(); // skip length
        // sending device's address
        uint8_t addr = packet.pop();

        // eat up the MAC, it's already verified
        packet.trim(4);

        // indicates error in packet processing when nonzero (i.e. non OK)
        uint8_t err = OK;

        while (!packet.empty()) {
            // the first byte here is command
            auto c = packet.pop();
            bool resp = c & 0x80; // responses have highest bit set
            c &= 0x7f;

#ifdef CLIENT_MODE
            if (resp) {
                // TODO:
                /*
                ERR("Slave can't process responses");
                return false;
                */
            }
#else
            if (!resp) {
                ERR("Master can't process commands");
                return false;
            }
#endif

            switch (c) {
            case 'V':
                err |= on_version(addr, packet); break;
                // these all respond with debug response
            case 'D': // Debug command response
            case 'A': // Set temperatures response (debug)
            case 'M': // Mode command response
                err |= on_debug(addr, packet); break;
            case 'T': // Watch command response (reads watched variables from PGM)
            case 'R': // Read timers (?)
            case 'W': // Write timers (?)
                err |= on_timers(addr, packet); break;
            case 'G': // get eeprom
            case 'S': // set eeprom
                err |= on_eeprom(addr, packet); break;
            case 'L': // locked menu?
                err |= on_menu_lock(addr, packet); break;
            case 'B':
                err |= on_reboot(addr, packet);
            default:
                ERR("UNK. CMD %x", (int)c);
                packet.clear();
                return true;
            }
        }

        // inform send queue that we can send data for addr if we have any
        // TODO: Implement time slotting to limit the count of packets in one turn
        // - we could very well be stuck in a send/recv loop with one client
        sndQ.prepare_to_send_to(addr);

        return err == OK;
    }

    void on_failed_verify() {
        // TODO: Might immediately send sync as response to sync a stubborn HR20
        // with incompatible receive windows that does not hear normal Synces
    }

    Error on_version(uint8_t addr, RcvPacket &p) {
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

    bool on_debug(uint8_t addr, RcvPacket &p) {
        // 10 0a 44 27 1e 04 08 9f 0a 5a 23 1e 44 e0 8e 87 01
        // 16 bytes len, address 10
        // 44 == D
        // data[9] 27 1e 04 08 9f 0a 5a 23 1e
        // ?? 44
        // cmac[4] e0 8e 87 01
        if (p.size() < 9) {
            ERR("SHORT DBG");
            p.clear();
            return true;
        }

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

        // TODO: dummy or what?! Maybe off-by-one problem in original impl.
        p.pop();

        // fetch client from model
        HR20 *hr = model[addr];
        if (!hr) return ERR_PROTO;

        hr->last_contact = rd_time;

        hr->auto_mode.set_remote(min_ctl & 0x80, rd_time);
        hr->test_auto.set_remote(min_ctl & 0x40, rd_time);
        hr->menu_locked.set_remote(sec_mm & 0x80, rd_time);
        hr->mode_window.set_remote(sec_mm & 0x40, rd_time);
        hr->temp_avg.set_remote(tmp_avg_h << 8 | tmp_avg_l, rd_time);
        hr->bat_avg.set_remote(bat_avg_h << 8 | bat_avg_l, rd_time);
        hr->cur_temp_wtd.set_remote(tmp_wtd, rd_time);
        hr->cur_valve_wtd.set_remote(valve_wtd, rd_time);

        DBG(" * DBG RESP OK");
        return OK;
    }

    bool on_timers(uint8_t addr, RcvPacket &p) {
        if (p.size() < 3) {
            ERR("SHORT TMR");
            p.clear();
            return ERR_PROTO;
        }

        uint8_t idx  = p.pop();
        uint16_t val = p.pop() << 8;
        val |= p.pop();

        // TODO: change the timer value in model

        return OK;
    }

    bool on_eeprom(uint8_t addr, RcvPacket &p) {
        if (p.size() < 2) {
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

    bool on_menu_lock(uint8_t addr, RcvPacket &p) {
        if (p.size() < 1) {
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

    bool on_reboot(uint8_t addr, RcvPacket &p) {
        if (p.size() < 2) {
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

    void queue_updates_for(uint8_t addr, HR20 &hr) {
        // wanted temperature
        if (hr.temp_wanted.needs_write(rd_time))
            send_set_temp(addr, hr.temp_wanted);
        if (hr.auto_mode.needs_write(rd_time))
            send_set_auto_mode(addr, hr.auto_mode);

    }

    void send_set_temp(uint8_t addr, SyncedValue<uint8_t> &temp_wanted) {
        // 2 bytes [A][xx] xx is in half degrees
        SndPacket *p = sndQ.want_to_send_for(addr, 2);
        if (!p) return;

        p->push('A');
        p->push(temp_wanted.get_local());

        // set the time we last requested the change
        temp_wanted.set_send_time(rd_time);
    }

    void send_set_auto_mode(uint8_t addr, SyncedValue<bool> &auto_mode) {
        // 2 bytes [A][xx] xx is in half degrees
        SndPacket *p = sndQ.want_to_send_for(addr, 2);
        if (!p) return;

        p->push('M');
        p->push(auto_mode.get_local() ? 1 : 0);

        // set the time we last requested the change
        auto_mode.set_send_time(rd_time);
    }


    // ref to model of the network
    Model &model;

    // encryption tools
    crypto::Crypto &crypto;

    // ref to packet queue responsible for packet retrieval for sending
    SendQ &sndQ;

    // current read time
    time_t rd_time;
};

/// Implements the state machine for packet sending/retrieval and radio control.
template<typename HandlerT>
struct Transceiver {
    Transceiver(RFM12B &rfm, SendQ &queue, HandlerT handler) : rfm(rfm), queue(queue), handler(handler) {}

    void update() {
        // only update the receiver if we're currently receiving
        // - sending blocks recv
        if (radio.isReceiving()) {
            receive();
        } else {
            send();
        }
    }

    void receive() {
        int b = rfm.recv();

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
            }
        } else {
            --length;
            if (!length) {
                DBG(" * packet received. Will process");
                handler(packet);
                DBG("=== END OF PACKET PROCESSING ===");
                wait_for_sync();
            }
        }
    }

    void send() {
        int b = queue.peek();

        if (b >= 0) {
            if (radio.send(b))
                queue.pop();
        } else {
            // no more data, recv again
            wait_for_sync();
        }
    }

    void wait_for_sync() {
        packet.clear();
        radio.wait_for_sync();
    }

    RFM12B &rfm;
    SendQ &queue;
    HandlerT handler;
    // received packet
    RcvPacket packet;
    int length = 0;
};

// the main classes - they implement local cache, request tracking,
// synchronization and packet queueing
Model model;
SendQ queue(crypt);
Protocol proto{model, crypt, queue};

// helper that induces the template parameter based on argument
template<typename HandlerT>
Transceiver<HandlerT> make_transceiver(RFM12B &radio, SendQ &queue, HandlerT h) {
    return Transceiver<HandlerT>{radio, queue, h};
}

// receiver that calls the protocol handler with the packet after it's been
// completely received.
auto transciever =
    make_transceiver(radio, queue, [&](RcvPacket &p) { proto.receive(p); });

void loop(void) {
    radio.poll();
    if (time.update()) sync_count = SYNC_COUNT;

    // Note: could use [[maybe_unused]] in C++17
    bool __attribute__((unused)) sec_pass = crypt.update(); // update the crypto rtc if needed

    // TODO: if it's 00 or 30, we send sync
#ifndef CLIENT_MODE
    if (sec_pass) {
        if (sync_count &&
            (crypt.rtc.ss == 0 ||
             crypt.rtc.ss == 30))
        {
            // send sync packet
            // TODO: set force flags is we want to comm with a specific client
            // TODO: protocol.send_sync(crypt.rtc);
            --sync_count;
        }

        // right before the next minute starts...
        if (crypt.rtc.ss == 59) protocol.fill_send_queues();
    }
#endif

    transciever.update();
}
