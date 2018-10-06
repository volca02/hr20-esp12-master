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

#ifdef WIFI_MGR
#include <WiFiManager.h>
#endif

#include "wifi.h"

namespace hr20 {

#ifndef WIFI_MGR

void setupWifi()
{
}

#else

WiFiManager wifiManager;

bool saveConfig;

void ICACHE_FLASH_ATTR saveConfigCallback() {
    DBG("Configuration is beeing saved");
    saveConfig = true;
}

void ICACHE_FLASH_ATTR setupWifi(Config &config, bool cfgLoaded) {
    //uncomment for testing
    //wifiManager.resetSettings();
    //uncomment to format fs
    //SPIFFS.format();
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    if (!cfgLoaded) {
        DBG("Resetting WiFi settings");
        wifiManager.resetSettings();
    }

    WiFiManagerParameter logo_css("<h2>OpenHR20 configuration</h2><div id='logo'></div><style>#logo {  width:100%; height:100px; background-repeat: no-repeat; background-position: center; background-image: url(data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiPz48c3ZnIGNsYXNzPSIiIGVuYWJsZS1iYWNrZ3JvdW5kPSJuZXcgMCAwIDUxMiA1MTIiIHZlcnNpb249IjEuMSIgdmlld0JveD0iMCAwIDUxMiA1MTIiIHhtbDpzcGFjZT0icHJlc2VydmUiIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyI+PHBhdGggY2xhc3M9ImFjdGl2ZS1wYXRoIiBkPSJtNDk3IDE4MWM4LjI4NCAwIDE1LTYuNzE2IDE1LTE1di02MGMwLTguMjg0LTYuNzE2LTE1LTE1LTE1aC00NmMwLTMzLjA4NC0yNi45MTYtNjAtNjAtNjAtMTcuOTA4IDAtMzMuOTk3IDcuODk2LTQ1IDIwLjM3Ny0xMS4wMDMtMTIuNDgxLTI3LjA5Mi0yMC4zNzctNDUtMjAuMzc3cy0zMy45OTcgNy44OTYtNDUgMjAuMzc3Yy0xMS4wMDMtMTIuNDgxLTI3LjA5Mi0yMC4zNzctNDUtMjAuMzc3cy0zMy45OTcgNy44OTYtNDUgMjAuMzc3Yy0xMS4wMDMtMTIuNDgxLTI3LjA5Mi0yMC4zNzctNDUtMjAuMzc3LTMzLjA4NCAwLTYwIDI2LjkxNi02MCA2MGgtMzF2LTE1YzAtOC4yODQtNi43MTYtMTUtMTUtMTVzLTE1IDYuNzE2LTE1IDE1djEyMGMwIDguMjg0IDYuNzE2IDE1IDE1IDE1czE1LTYuNzE2IDE1LTE1di0xNWgzMXYxMjBoLTMxdi0xNWMwLTguMjg0LTYuNzE2LTE1LTE1LTE1cy0xNSA2LjcxNi0xNSAxNXYxMjBjMCA4LjI4NCA2LjcxNiAxNSAxNSAxNXMxNS02LjcxNiAxNS0xNXYtMTVoMzF2MzBjMCAzMy4wODQgMjYuOTE2IDYwIDYwIDYwIDE3LjkwOCAwIDMzLjk5Ny03Ljg5NiA0NS0yMC4zNzcgMTEuMDAzIDEyLjQ4MSAyNy4wOTIgMjAuMzc3IDQ1IDIwLjM3N3MzMy45OTctNy44OTYgNDUtMjAuMzc3YzExLjAwMyAxMi40ODEgMjcuMDkyIDIwLjM3NyA0NSAyMC4zNzdzMzMuOTk3LTcuODk2IDQ1LTIwLjM3N2MxMS4wMDMgMTIuNDgxIDI3LjA5MiAyMC4zNzcgNDUgMjAuMzc3IDMzLjA4NCAwIDYwLTI2LjkxNiA2MC02MHYtMzBoNDZjOC4yODQgMCAxNS02LjcxNiAxNS0xNXYtNjBjMC04LjI4NC02LjcxNi0xNS0xNS0xNWgtNDZ2LTEyMGg0NnptLTQ2LTYwaDMxdjMwaC0zMXYtMzB6bS0zOTAgMjQwaC0zMXYtMzBoMzF2MzB6bTAtMjEwaC0zMXYtMzBoMzF2MzB6bTkwIDI3MGMwIDE2LjU0Mi0xMy40NTggMzAtMzAgMzBzLTMwLTEzLjQ1OC0zMC0zMHYtMzMwYzAtMTYuNTQyIDEzLjQ1OC0zMCAzMC0zMHMzMCAxMy40NTggMzAgMzB2MzMwem05MCAwYzAgMTYuNTQyLTEzLjQ1OCAzMC0zMCAzMHMtMzAtMTMuNDU4LTMwLTMwdi0zMzBjMC0xNi41NDIgMTMuNDU4LTMwIDMwLTMwczMwIDEzLjQ1OCAzMCAzMHYzMzB6bTkwIDBjMCAxNi41NDItMTMuNDU4IDMwLTMwIDMwcy0zMC0xMy40NTgtMzAtMzB2LTMzMGMwLTE2LjU0MiAxMy40NTgtMzAgMzAtMzBzMzAgMTMuNDU4IDMwIDMwdjMzMHptOTAgMGMwIDE2LjU0Mi0xMy40NTggMzAtMzAgMzBzLTMwLTEzLjQ1OC0zMC0zMHYtMzMwYzAtMTYuNTQyIDEzLjQ1OC0zMCAzMC0zMHMzMCAxMy40NTggMzAgMzB2MzMwem02MS05MHYzMGgtMzF2LTMwaDMxeiIgZmlsbD0iIzFGQTNFQyIgZGF0YS1vbGRfY29sb3I9IiMxZmEzZWMiIGRhdGEtb3JpZ2luYWw9IiMwMDAwMDAiLz4gPC9zdmc+);}</style>");

    WiFiManagerParameter rfm_pass(
        "rfm_pass", "RFM pass", config.get_rfm_pass_value(), 16,
        "onkeypress=\"regexp=/^[0-9a-fA-F]/i; if(!regexp.test(event.key)) "
        "return false;\"");

    WiFiManagerParameter ntp_server("ntp_server", "NTP server",
                                    config.ntp_server, 40);
#ifdef MQTT
    //mqtt
    WiFiManagerParameter mqtt_server("mqtt_server", "MQTT server",
                                     config.mqtt_server, 40);
    char port[6];
    snprintf(port, 6, "%u", config.mqtt_port);
    WiFiManagerParameter mqtt_port(
        "mqtt_port", "MQTT port", port, 5,
        "onkeypress=\"regexp=/^[0-9]/i; if(!regexp.test(event.key)) return "
        "false;\"");
    WiFiManagerParameter mqtt_user("mqtt_user", "MQTT user", config.mqtt_user,
                                   20);
    WiFiManagerParameter mqtt_pass("mqtt_pass", "MQTT pass", config.mqtt_pass,
                                   20);
    WiFiManagerParameter mqtt_topic_prefix(
        "mqtt_topic_prefix", "MQTT topic prefix", config.mqtt_topic_prefix, 40);
#endif

    wifiManager.addParameter(&logo_css);
    wifiManager.addParameter(&rfm_pass);
    wifiManager.addParameter(&ntp_server);

#ifdef MQTT
    wifiManager.addParameter(&mqtt_server);
    wifiManager.addParameter(&mqtt_port);
    wifiManager.addParameter(&mqtt_user);
    wifiManager.addParameter(&mqtt_pass);
    wifiManager.addParameter(&mqtt_topic_prefix);
#endif

    if (!wifiManager.autoConnect("OpenHR20 is not dead"))
    {
        ERR("Failed to connect. Reset...");
        delay(3000);
        ESP.reset();
        delay(5000);
    }
    if (saveConfig)
    {
        config.set_rfm_pass(rfm_pass.getValue());
        strncpy(config.ntp_server, ntp_server.getValue(), sizeof(config.ntp_server));

#ifdef MQTT
        strncpy(config.mqtt_server, mqtt_server.getValue(),
                sizeof(config.mqtt_server));
        config.mqtt_port = (uint16_t)atoi(mqtt_port.getValue());
        strncpy(config.mqtt_user, mqtt_user.getValue(),
                sizeof(config.mqtt_user));
        strncpy(config.mqtt_pass, mqtt_pass.getValue(),
                sizeof(config.mqtt_pass));
        strncpy(config.mqtt_topic_prefix, mqtt_topic_prefix.getValue(),
                sizeof(config.mqtt_topic_prefix));
#endif

        DBG("Saving configuration to file");
        if (!config.save("config.json")) {
            ERR("Saving configuration failed.");
        }
    }
}

#endif

} // namespace hr20
