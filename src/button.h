#include <Arduino.h>
#include "hr-debug.h"

#define RESET_BUTTON_PIN 4
#define WIFI_RESET_TIME 3000
#define CONFIG_RESET_TIME 10000




volatile uint8_t resetButtonPressed;
volatile uint8_t resetButtonChanged;
uint32_t resetButtonPressedMillis;
uint8_t resetWifi;
uint8_t resetConfig;


ICACHE_RAM_ATTR static void buttonChange() {
    detachInterrupt(RESET_BUTTON_PIN);
    resetButtonChanged = true;
    resetButtonPressed = digitalRead(RESET_BUTTON_PIN) == HIGH;
    attachInterrupt(RESET_BUTTON_PIN, buttonChange, CHANGE);
}

ICACHE_FLASH_ATTR static void handleButton() {
    uint32_t millisNow = millis();
    noInterrupts();
    if (resetButtonChanged) {
        resetButtonChanged = false;

        if (resetButtonPressed) {
            resetWifi                = false;
            resetConfig              = false;
            resetButtonPressedMillis = millisNow;
        } else {
            if ((millisNow - resetButtonPressedMillis) > CONFIG_RESET_TIME) {
                resetConfig = true;
            } else if (millisNow - resetButtonPressedMillis > WIFI_RESET_TIME) {
                resetWifi = true;
            }
        }
    }
    interrupts();
    if (resetWifi) {
#warning REPLACE THIS WITH IOTWEBCONF
//        hr20::resetWifi();
        ESP.restart();
        resetWifi=false;
    } else if (resetConfig) {
        DBG("TODO: reset config");
        resetConfig=false;
    }
}
