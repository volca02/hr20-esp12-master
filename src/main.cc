#include <Arduino.h>
#include <SPI.h>
#include "crypto.h"
#include "rfm12b.h"
#include "wifi.h"
#include "ntptime.h"

// custom specified RFM password
constexpr const uint8_t rfm_pass[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0x01, 0x23, 0x45};
// this encompasses the encryption handling routines
crypto::Crypto crypt{rfm_pass};

RFM12B radio;
int length = 0;
bool timeInSync = false;

void setup(void) {
  Serial.begin(38400);

  //this is perhaps useful for somtething (wifi, ntp) but not sure
  randomSeed(micros());
  setupWifi();
  timeClient.begin();
  timeInSync = timeClient.update();

  // also calls SPI.begin()!
  radio.begin();
}

void loop(void) {
    radio.poll();

    int b = radio.recv();

    if (!timeInSync) {
        timeClient.update();
    }
    if (b >= 0) {
        Serial.printf("%02x ", b);

        if (length == 0) {
            //time
            Serial.printf(" %lu ",localTime());


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
                printRTC();
                Serial.println();

                // packed received whole
                // reset to sync-word recv mode again.
                radio.wait_for_sync();
            }
        }
    }
}
