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
#include <string.h>
#include <jsmn.h>

#include "debug.h"

namespace hr20 {

struct Config
{
    uint8_t rfm_pass[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0x01, 0x23, 0x45};

#ifdef NTP_CLIENT
    char ntp_server[41] = "2.europe.pool.ntp.org";
#endif

#ifdef MQTT
    char mqtt_client_id[20];
    char mqtt_server[41] = "";
    uint16_t mqtt_port = 1883;
    char mqtt_user[21] = "";
    char mqtt_pass[21] = "";
    char mqtt_topic_prefix[41] = "hr20";
#endif

    bool ICACHE_FLASH_ATTR save(const char *filename);
    bool ICACHE_FLASH_ATTR load(const char *filename);

    char *ICACHE_FLASH_ATTR get_rfm_pass_value();
    bool ICACHE_FLASH_ATTR set_rfm_pass(const char *pass);

    // loads the config
    bool ICACHE_FLASH_ATTR begin(const char *filename);

    char rfm_pass_hex[17];
private:
    bool ICACHE_FLASH_ATTR jsoneq(const char *json, jsmntok_t *tok, const char *s);

#ifdef MQTT
    const char *mqtt_client_id_prefix = "OpenHR20_";
#endif
};

// 2 minutes to resend the change request if we didn't yet get the value change confirmed
// this is now only used to throw away old packets in packet queue
constexpr const time_t RESEND_TIME = 2*60;

// Don't try re-reading every time. Skip a few packets in-between
constexpr const int8_t REREAD_CYCLES = 2;

// Don't try setting value every time. Skip a few packets in-between
constexpr const int8_t RESEND_CYCLES = 2;

// Max. number of timers queued per one packet exchange
constexpr const int8_t MAX_QUEUE_TIMERS = 8;

// Max. count of HR clients (and a max addr)
constexpr const uint8_t MAX_HR_COUNT = 29;

// Max. count of HR clients (and a max addr)
#define c2temp(c) (c*2)
constexpr const uint8_t TEMP_MIN = c2temp(5);
constexpr const uint8_t TEMP_MAX = c2temp(30);

// rfm version of OpenHR20 supports 8 timers a day
constexpr const uint8_t TIMER_SLOTS_PER_DAY = 8;

// timer has 7 slots for days and 1 slot extra for repeated everyday mode
constexpr const uint8_t TIMER_DAYS = 8;

// should we retain the published topics?
constexpr const bool MQTT_RETAIN = true;

// Reconnect attempt every N seconds
constexpr const time_t MQTT_RECONNECT_TIME = 10;

} // namespace hr20
