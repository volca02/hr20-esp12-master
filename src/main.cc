#include <Arduino.h>
#include <SPI.h>
#include "crypto.h"
#include "rfm12b.h"
#include "wifi.h"
#include "ntptime.h"

// custom specified RFM password
constexpr const uint8_t rfm_pass[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0x01, 0x23, 0x45};

ntptime::NTPTime time;

// this encompasses the encryption handling routines
crypto::Crypto crypt{rfm_pass, time};


RFM12B radio;

// TODO: make a class out of this that processes the incoming packets
int length = 0;
ShortQ<32> packet;


void setup(void) {
  Serial.begin(38400);

  //this is perhaps useful for somtething (wifi, ntp) but not sure
  randomSeed(micros());
  setupWifi();

  time.begin();

  // also calls SPI.begin()!
  radio.begin();
}

void hexdump(char *ptr, size_t size) {
    for(;size;++ptr,--size) {
        Serial.printf("%02x ", (uint8_t)*ptr);
    }
}

/// temporary test that veri
void verify_decode() {
    Serial.printf("== Will verify_decode packet of %d bytes ==\n", packet.size());

    crypto::CMAC cm(crypt.K1, crypt.K2, crypt.Kmac);
    size_t data_size = packet.size() - 1;

    if (data_size != (packet[0] & 0x7f)) {
        Serial.println("- Incomplete packet received?");
        return;
    }

    if (data_size < 5) {
        Serial.println(" ! packet too short");
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

    Serial.printf("-%s PACKET VERIFICATION %s -\n",
                  isSync ? " SYNC" : "",
                  ver ? "SUCCESS" : "FAILURE");

    if (isSync) {
        if (packet.size() < 1+4+4) {
            Serial.println("Sync packet too short");
            return;
        }
        crypto::RTC decoded;
        decoded.YY = packet[1];
        decoded.MM = packet[2] >> 4;
        decoded.DD = (packet[3] >> 5) + ((packet[2] << 3) & 0x18);
        decoded.hh = packet[3] & 0x1f;
        decoded.mm = packet[4] >> 1;
        decoded.ss = packet[4] & 1 ? 30 : 00;

        Serial.printf("Decoded time from sync packet: %02d.%02d.%02d %02d:%02d:%02d\n",
                      decoded.DD, decoded.MM, decoded.YY,
                      decoded.hh, decoded.mm, decoded.ss);

        if (ver) {
            // is this local time or what?
            Serial.println("Synchronizing time");
            setTime(decoded.hh, decoded.mm, decoded.ss, decoded.DD, decoded.MM, decoded.YY);
            time.printRTC();
        }
    } else {
        if (packet.size() < 6) {
            Serial.println("Sync packet too short");
            return;
        }
        // not a sync packet. we have to decode it
        crypt.encrypt_decrypt(reinterpret_cast<uint8_t*>(packet.data()) + 2, packet.size() - 6);
        Serial.println("Decoded data of the whole packet:");
        hexdump(packet.data(), packet.size());
        Serial.println("");
    }
}

void loop(void) {
    radio.poll();

    int b = radio.recv();

    time.update();  // sync, upda
    crypt.update(); // update the crypto rtc if needed


    if (b >= 0) {
        Serial.printf("%02x ", b);

        packet.push(b);

        if (length == 0) {
            //time
            Serial.printf(" %lu ", time.localTime());

            // first byte - length+sync byte bit (0x80)
            length = b & ~0x80;
            Serial.printf("{%d:", length);

            if (length == 0) {
                Serial.println("?!}");
                radio.wait_for_sync();
            }
        } else {
            --length;
            if (!length) {
                Serial.println("}");

                //print rtc test
                time.printRTC();

                // we have the whole packet
                verify_decode();

                // ...

                // we did what we could with the packet, now throw it away
                packet.clear();

                Serial.println();

                // packed received whole
                // reset to sync-word recv mode again.
                radio.wait_for_sync();
            }
        }
    }
}
