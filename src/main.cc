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

#include <Arduino.h>
#include <ArduinoOTA.h>

#include "config.h"
#include "wifi.h"
#include "ntptime.h"
#include "debug.h"
#include "master.h"
#include "eventlog.h"
#include "button.h"
#include "webserver.h"

#ifdef HR20_DISPLAY
#include "display.h"
#endif

#ifdef MQTT
#include "mqtt.h"
#endif

hr20::Config config;
hr20::ntptime::NTPTime ntptime;
hr20::HR20Master master{config, ntptime};
int last_int = 1;

// webserver is mandatory for the configuration of the device
hr20::Web webserver(config, master);

#ifdef MQTT
hr20::mqtt::MQTTPublisher publisher(config, master);
#endif

#ifdef HR20_DISPLAY
hr20::Display display(master);
#endif

void setup(void) {
    Serial.begin(38400);

    // Config/wifi is handled by IotWebConf
    // must be before wdtEnable
    // bool loaded = config.begin("config.json");
    // this may change the config so it has to be at the top
    // setupWifi(config, loaded);

    // set watchdog to 2 seconds.
    ESP.wdtEnable(2000);

    ntptime.begin();
    master.begin();

    // TODO: this is perhaps useful for something (wifi, ntp) but not sure
    randomSeed(micros());

    attachInterrupt(RESET_BUTTON_PIN, buttonChange, CHANGE);

    ArduinoOTA.begin();

#ifdef MQTT
    // attaches the path's prefix to the setup value
    hr20::mqtt::Path::begin(config.mqtt_topic_prefix);
    publisher.begin();
#endif

#ifdef HR20_DISPLAY
    display.begin();
#endif

    // needed for the webserver
    webserver.begin();
}

void loop(void) {
    // handle OTA updates as appropriate
    ArduinoOTA.handle();

    // feed the watchdog...
    ESP.wdtFeed();

    // don't try to repeat ntp time updates more than once a second!
    bool changed_time = false;
    time_t now = ntptime.update(master.can_update_ntp(), changed_time);

    hr20::eventLog.update(now);

    bool __attribute__((unused))
        sec_pass = master.update(changed_time, ntptime.localTime());

    // sec_pass = second passed (once every second)

    handleButton();

    static int last_status = -1;
    int status = WiFi.status();
    if (status != last_status) {
        last_status = status;
        DBG("(WIFI %d)", status);
    }

#ifdef MQTT
    // only update mqtt if we have a time to do so, as controlled by master
    if (master.is_idle()) publisher.update(now);
#endif

#ifdef HR20_DISPLAY
    // TODO: Eats a lot of time. display.update();
#endif

#ifdef RFM_POLL_MODE
    // only update web when radio's not talking
    if (master.is_idle())
        webserver.update();
#else
    webserver.update();
#endif
}
