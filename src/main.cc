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
#else
#define DBGI(...) do { } while (0)
#define DBG(...) do { } while (0)
#endif

// custom specified RFM password
constexpr const uint8_t rfm_pass[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0x01, 0x23, 0x45};

// RTC with NTP synchronization
ntptime::NTPTime time;

// this encompasses the encryption handling routines
crypto::Crypto crypt{rfm_pass, time};

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

/*    if (!isSync) {
        Serial.println("RTC STRUCT CONTENTS: ");
        const char *rtc = reinterpret_cast<const char*>(&crypt.rtc);
        size_t count = 8;
        HexDump(rtc, count);
        Serial.println("");
    }
*/

    // restore the pkt_cnt
    crypt.rtc.pkt_cnt -= cnt_offset;

    DBG(" %s%s PACKET VERIFICATION %s",
        ver ? "*" : "!",
        isSync ? " SYNC" : "",
        ver ? "SUCCESS" : "FAILURE");

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
            DBG(" ! Sync packet too short");
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
    time.update();
    bool sec_pass = crypt.update(); // update the crypto rtc if needed

    // TODO: if it's 00 or 30, we send sync
#ifndef CLIENT_MODE
    if (sec_pass) {
        if (crypt.rtc.ss == 0 ||
            crypt.rtc.ss == 30)
        {
            // send sync packet
            // TODO: sender.send_sync(crypt.rtc);
        }
    }
#endif

    // only update the receiver if we're currently receiving
    if (radio.isReceiving())
    {
        // always active in client mode and
        // Let's receive data
        receiver.update();
    }
}
