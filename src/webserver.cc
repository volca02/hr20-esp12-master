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
#include <FS.h>

#include "webserver.h"
#include "util.h"

namespace hr20 {

static bool validate_hex(const char *buf) {
    for (;*buf;++buf) {
        if (hex2int(*buf) < 0)
            return false;
    }
    return true;
}


static bool validate_dec(const char *buf) {
    for (;*buf;++buf) {
        if (todigit(*buf) < 0)
            return false;
    }
    return true;
}

// configuration button pin
#define CONFIG_BUTTON_PIN 4

// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char thingName[] = "hr20";

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "accpass2019";

// Note: if configuration changes, this has to be updated as well!
#define CONFIG_VERSION "ver1"

// TODO: Include the logo in the html somehow optimally

ICACHE_FLASH_ATTR Web::Web(Config &config, HR20Master &master)
    : config(config),
      server(80),
      iotWebConf(thingName,
                 &dnsServer,
                 &server,
                 wifiInitialApPassword,
                 CONFIG_VERSION),
      master(master),
      rfm_pass("RFM Password", "rfm_pass", config.rfm_pass_hex, 17),
      separator1(),
      ntp_server("NTP Server", "ntp_server", config.ntp_server, 40)
#ifdef MQTT
      ,
      separator2(),
      mqtt_server("MQTT Server", "mqtt_server", config.mqtt_server, 40),
      mqtt_port("MQTT Port",
                "mqtt_port",
                config.mqtt_port,
                5,
                "number",
                "1883",
                NULL,
                "min='1' max='65535' step='1'"),
      mqtt_user("MQTT User", "mqtt_user", config.mqtt_user, 20),
      mqtt_pass("MQTT Password", "mqtt_pass", config.mqtt_pass, 20),
      mqtt_topic("MQTT Topic", "mqtt_topic", config.mqtt_topic_prefix, 40)
#endif
{
}

ICACHE_FLASH_ATTR void Web::begin() {
    SPIFFS.begin();

//    iotWebConf.setConfigPin(CONFIG_BUTTON_PIN);
    iotWebConf.addParameter(&rfm_pass);
    iotWebConf.addParameter(&separator1);
    iotWebConf.addParameter(&ntp_server);

#ifdef MQTT
    iotWebConf.addParameter(&separator2);
    iotWebConf.addParameter(&mqtt_server);
    iotWebConf.addParameter(&mqtt_port);
    iotWebConf.addParameter(&mqtt_user);
    iotWebConf.addParameter(&mqtt_pass);
    iotWebConf.addParameter(&mqtt_topic);
#endif

    // what is this?
    iotWebConf.getApTimeoutParameter()->visible = true;

    iotWebConf.setFormValidator([&] { return validate_config(); });
    iotWebConf.setConfigSavedCallback([&] { master.config_updated(); });

    iotWebConf.init();

    server.on("/list", [&]   { handle_list(); } );
    server.on("/timer", [&]  { handle_timer(); } );
    server.on("/events", [&] { handle_events(); } );

    // iotWebConf handling
    server.on("/config", [&] { iotWebConf.handleConfig(); });

    // handles static content (index.html from SPIFFS) conditionally
    server.on("/", [&] { handle_root(); });

    server.onNotFound( [&] { iotWebConf.handleNotFound(); });

    server.begin();
}

ICACHE_FLASH_ATTR void Web::handle_list() {
    // we need 32*140 for client list
    static BufferHolder<LIST_MAX_SIZE> buf;
    StrMaker result(buf);
    {
        json::Object main(result);

        // iterate all clients
        for (unsigned i = 0; i < hr20::MAX_HR_ADDR; ++i) {
            auto m = master.model[i];

            if (!m) continue;
            if (m->last_contact == 0) continue;

            // append a key for this client
            main.key(i);
            // append value for this object
            json::append_client_attr(result, *m);
        }

    } // closes the curly brace

    result += "\r\n";

    // compose a json list of all visible clients
    // send header and content separately
    server.sendContent_P(JSON200, JSON200_LEN);
    server.sendContent_P(result.data(), result.size());
}

ICACHE_FLASH_ATTR void Web::handle_timer() {
    // WE NEED 32*8*8 for timers
    static BufferHolder<TIMER_MAX_SIZE> buf;
    StrMaker result(buf);
    // did we get an argument?
    auto client = server.arg("client");

    int caddr = client.toInt();

    if (caddr == 0) {
        server.send_P(404, "text/plain", "Invalid client");
        return;
    }

    // try to fetch the client
    const auto &m = master.model[caddr];

    if (!m || m->last_contact == 0) {
        server.send_P(404, "text/plain", "Invalid client");
        return;
    }

    { // intentional brace to close the json before we send it
        json::Object obj(result);

        // spew the whole timer table
        for (uint8_t idx = 0; idx < TIMER_DAYS; ++idx) {
            obj.key(idx);
            json::append_timer_day(result, *m, idx);
        }
    }

    result += "\r\n";

    server.sendContent_P(JSON200, JSON200_LEN);
    server.sendContent_P(result.data(), result.size());
}

ICACHE_FLASH_ATTR void Web::handle_events() {
    static BufferHolder<EVENT_MAX_SIZE> buf;
    StrMaker result(buf);

    auto soffset = server.arg("offset");
    unsigned offset   = std::max(0L, soffset.toInt());
    unsigned counter  = MAX_JSON_EVENTS;
    {
        json::Object main(result);

        main.key("events");
        json::Array arr(main);

        // iterate all clients
        for (const auto &event : eventLog) {
            if (offset != 0) {
                --offset;
                continue;
            }

            if (!counter) break;
            --counter;

            // skip empty events
            if (event.time == 0) continue;

            // a comma is inserted unless this is first element
            arr.element();

            // append value itself
            json::append_event(result, event);
        }

    } // closes the curly brace

    result += "\r\n";

    server.sendContent_P(JSON200, JSON200_LEN);
    server.sendContent_P(result.data(), result.size());
}

ICACHE_FLASH_ATTR void Web::handle_root() {
    // we can't use serveStatic because of the redirection to iotWebConf's
    // captive portal when applicable...
    if (iotWebConf.handleCaptivePortal()) {
        DBG("(handle_root_captive)");
        return;
    }

    // if we're in AP mode, display the config page
    auto state = iotWebConf.getState();
    if (state == IOTWEBCONF_STATE_AP_MODE
        || state == IOTWEBCONF_STATE_NOT_CONFIGURED)
    {
        DBG("(handle_ap_mode)");
        iotWebConf.handleConfig();
        return;
    }

    DBG("(handle_root %d)", iotWebConf.getState());
    File f = SPIFFS.open("/index.html", "r");
    server.streamFile(f, "text/html");
}

ICACHE_FLASH_ATTR void Web::update() {
    iotWebConf.doLoop();
    server.handleClient();
}

ICACHE_FLASH_ATTR bool Web::validate_config() {
    bool valid = true;

    auto rfm_p = server.arg(rfm_pass.getId());
    int l = rfm_p.length();
    if (l < 16)
    {
        rfm_pass.errorMessage =
            "RFM Password has to be 8 bytes (16 hex chars) long!";
        valid = false;
    }

    if (!validate_hex(rfm_p.c_str())) {
        rfm_pass.errorMessage = "Only use hexadecimal characters in RFM Password!";
        valid = false;
    }

#ifdef MQTT
    auto mqtt_p = server.arg(mqtt_port.getId());
    if (!validate_dec(mqtt_p.c_str())) {
        mqtt_port.errorMessage = "MQTT Port expects a number!";
        valid = false;
    }
#endif

    // ....

    return valid;
}

} // namespace hr20
