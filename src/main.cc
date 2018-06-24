#include <Arduino.h>
#include <SPI.h>
#include "crypto.h"
#include "rfm12b.h"
#include "wifi.h"
#include "ntptime.h"

// FOR NOW
#define DEBUG

#ifdef DEBUG
#define DBGI(...) do { Serial.printf(__VA_ARGS__); } while (0)
#define DBG(...) do { Serial.printf(__VA_ARGS__); Serial.println(); } while (0)
// TODO: BETTER ERROR REPORTING
#define ERR(...) do { Serial.write("ERR "); Serial.printf(__VA_ARGS__); Serial.println(); } while (0)
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
constexpr const uint8_t rfm_pass[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0x01, 0x23, 0x45};

// RTC with NTP synchronization
ntptime::NTPTime time;

// this encompasses the encryption handling routines
crypto::Crypto crypt{rfm_pass, time};

int sync_count = 0;

RFM12B radio;

// we have 32 bytes for packets. more than enough
using Packet = ShortQ<32>;

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

// processes response packets from HR20s
struct Parser {

    bool process_response_packet(Packet &packet) {
        // before the main loop, we trim the packet's mac and first 2 bytes
        packet.pop(); // skip length
        // sending device's address
        uint8_t addr = packet.pop();
        size_t pos = 0;

        // eat up the MAC, it's already verified
        packet.trim(4);

        // indicates error in packet processing
        bool err = false;

        while (!packet.empty()) {
            // the first byte here is command
            auto c = packet.pop();
            bool resp = c & 0x80; // responses have highest bit set
            c &= 0x7f;

            // TODO: if (!resp) ERR...

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
                ERR("UNK. CMD %c", c);
                packet.clear();
                return true;
            }
        }
        return err;
    }

    bool on_version(uint8_t addr, Packet &p) {
        // sequence of bytes terminated by \n
        while (1) {
            // packet too short or newline missing?
            if (p.empty()) {
                ERR("SHORT VER");
                p.clear();
                return true;
            }

            auto c = p.pop();

            // ending byte
            if (c == '\n') {
                return true;
            }

            // todo: append this somewhere or what?
            DBGI("%c", c);
        }

        // will never get here
        return false;
    }

    bool on_debug(uint8_t addr, Packet &p) {
        if (p.size() < 9) {
            ERR("SHORT DBG");
            p.clear();
            return true;
        }

        // minutes. 0x80 is CTL_mode_auto, 0x40 is CTL_test_auto
        uint8_t min_ctl   = p.pop();
        // seconds. 0x80 is menu_locked, 0x40 is mode_window
        uint8_t sec_mm    = p.pop();
        uint8_t ctl_err   = p.pop();
        // 16 bit temp average
        uint8_t tmp_avg_h = p.pop();
        uint8_t tmp_avg_l = p.pop();
        // 16 bit batery measurement?
        uint8_t bat_avg_h = p.pop();
        uint8_t bat_avg_l = p.pop();
        // wanted temperature
        uint8_t tmp_wtd   = p.pop();
        // wanted valve position
        uint8_t valve_wtd = p.pop();

        return false;
    }

    bool on_timers(uint8_t addr, Packet &p) {
        if (p.size() < 3) {
            ERR("SHORT TMR");
            p.clear();
            return true;
        }

        uint8_t idx  = p.pop();
        uint16_t val = p.pop() << 8 | p.pop();
        return false;
    }

    bool on_eeprom(uint8_t addr, Packet &p) {
        if (p.size() < 2) {
            ERR("SHORT EEPROM");
            p.clear();
            return true;
        }

        uint8_t idx = p.pop();
        uint8_t val = p.pop();
        return false;
    }

    bool on_menu_lock(uint8_t addr, Packet &p) {
        if (p.size() < 1) {
            ERR("SHORT MLCK");
            p.clear();
            return true;
        }

        uint8_t menu_locked = p.pop();
        return false;
    }

    bool on_reboot(uint8_t addr, Packet &p) {
        if (p.size() < 2) {
            ERR("SHORT REBOOT");
            p.clear();
            return true;
        }

        // fixed response. has to be 0x13, 0x24
        uint8_t b13 = p.pop();
        uint8_t b24 = p.pop();
        return false;
    }
};

/// temporary test that verifies the packet integrity and spills out decoded data
void verify_decode(Packet &packet) {
    DBG("== Will verify_decode packet of %d bytes ==", packet.size());

    crypto::CMAC cm(crypt.K1, crypt.K2, crypt.Kmac);
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

    bool ver = cm.verify(reinterpret_cast<const uint8_t *>(packet.data() + 1),
                         data_size - 5, isSync ? nullptr : reinterpret_cast<const uint8_t*>(&crypt.rtc));

    // restore the pkt_cnt
    crypt.rtc.pkt_cnt -= cnt_offset;

    DBG(" %s%s PACKET VERIFICATION %s",
        ver    ? "*"       : "!",
        isSync ? " SYNC"   : "",
        ver    ? "SUCCESS" : "FAILURE");

    if (isSync) {
        if (packet.size() < 1+4+4) {
            Serial.println(" ! Sync packet too short");
            return;
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

        if (ver) {
            // is this local time or what?
            DBG(" * Synchronizing time");
            setTime(decoded.hh, decoded.mm, decoded.ss, decoded.DD, decoded.MM, decoded.YY);
            DBGI(" * "); time.printRTC();
        }
    } else {
        if (packet.size() < 6) {
            DBG(" ! packet too short");
            return;
        }
        // not a sync packet. we have to decode it
        crypt.encrypt_decrypt(reinterpret_cast<uint8_t*>(packet.data()) + 2, packet.size() - 6);
        crypt.rtc.pkt_cnt++;
        HexDump(" * Decoded packet data", packet.data(), packet.size());
    }
}

/// Implements the state machine for packet retrieval and radio control.
template<typename HandlerT>
struct Receiver {
    Receiver(RFM12B &rfm, HandlerT handler) : rfm(rfm), handler(handler) {}

    void update() {
        int b = rfm.recv();

        // no data on input means we just ignore
        if (b < 0) return;

        if (!packet.push(b)) {
            DBG(" ! packet exceeds maximal length. Discarding");
            wait_for_sync();
        }

        if (length == 0) {
            length = b & ~0x80;

            if (length == 0) {
                DBG("! Zero length packet");
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

    void wait_for_sync() {
        packet.clear();
        radio.wait_for_sync();
    }

    RFM12B &rfm;
    HandlerT handler;
    Packet packet;
    int length = 0;
};

// helper that induces the template parameter based on argument
template<typename HandlerT>
Receiver<HandlerT> make_receiver(RFM12B &radio, HandlerT h) {
    return Receiver<HandlerT>{radio, h};
}

// receiver that calls handler with the packet after it's been completely rcvd.
auto receiver = make_receiver(radio, [](Packet &p) { verify_decode(p); });

void loop(void) {
    radio.poll();
    if (time.update()) sync_count = SYNC_COUNT;

    bool sec_pass = crypt.update(); // update the crypto rtc if needed

    // TODO: if it's 00 or 30, we send sync
#ifndef CLIENT_MODE
    if (sec_pass) {
        if (sync_count &&
            (crypt.rtc.ss == 0 ||
             crypt.rtc.ss == 30))
        {
            // send sync packet
            // TODO: sender.send_sync(crypt.rtc);
            --sync_count;
        }
    }
#endif

    // only update the receiver if we're currently receiving
    // - sending blocks recv
    if (radio.isReceiving())
    {
        receiver.update();
    }
}
