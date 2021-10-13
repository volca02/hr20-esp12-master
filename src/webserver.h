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

#pragma once

#include <Arduino.h>
// #include <ESP8266WebServer.h>
#include <IotWebConf.h>
#include <IotWebConfUsing.h>

#include "config.h"
#include "master.h"
#include "json.h"

#define LIST_MAX_SIZE (32*140)
#define TIMER_MAX_SIZE (32*8*8)
#define EVENT_MAX_SIZE (100*MAX_JSON_EVENTS)

namespace hr20 {

#define JSON200 "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n"
#define JSON200_LEN 70

struct Web {
    Web(Config &config, HR20Master &master);

    void begin();
    void update();

protected:
    void handle_list();
    void handle_timer();
    void handle_events();
    void handle_root();
    bool validate_config();

    Config &config;
    DNSServer dnsServer;
    WebServer server;
    IotWebConf iotWebConf;
    HR20Master &master;

    iotwebconf::ParameterGroup base_group;
    iotwebconf::TextParameter rfm_pass;
    iotwebconf::TextParameter ntp_server;

#ifdef MQTT
    iotwebconf::ParameterGroup mqtt_group;
    iotwebconf::TextParameter mqtt_server;
    iotwebconf::NumberParameter mqtt_port;
    iotwebconf::TextParameter mqtt_user;
    iotwebconf::PasswordParameter mqtt_pass;
    iotwebconf::TextParameter mqtt_topic;
#endif
};

} // namespace hr20
