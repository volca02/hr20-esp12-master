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

#include "debug.h"

namespace hr20 {

struct Config
{
    char rfm_pass_hex[17] = "0123456789012345";

#ifdef NTP_CLIENT
    char ntp_server[41] = "2.europe.pool.ntp.org";
#endif

#ifdef MQTT
    char mqtt_client_id[20];
    char mqtt_server[41] = "";
    char mqtt_port[6] = "1883";
    char mqtt_user[21] = "";
    char mqtt_pass[21] = "";
    char mqtt_topic_prefix[41] = "hr20";
#endif

    // converts hexadecimal config form of rfm password to binary into 8 byte buffer
    bool ICACHE_FLASH_ATTR rfm_pass_to_binary(unsigned char *rfm_pass);

private:
#ifdef MQTT
    const char *mqtt_client_id_prefix = "OpenHR20_";
#endif
};

// 2 minutes before the packet is surely discarded
constexpr const time_t PACKET_DISCARD_AGE = 2*60;

// Don't try re-reading every time. Skip a few packets in-between
constexpr const int8_t REREAD_CYCLES = 2;

// Don't try setting value every time. Skip a few packets in-between
constexpr const int8_t RESEND_CYCLES = 2;

// Max. number of timers queued per one packet exchange
constexpr const int8_t MAX_QUEUE_TIMERS = 8;
// Max. count of eeprom accesses queued per one client roundtrip
constexpr const int8_t MAX_QUEUE_EEPROM = 8;

// Max. count of HR clients
constexpr const uint8_t MAX_HR_COUNT = 8;
// Max. address (first invalid address, to be precise)
constexpr const uint8_t MAX_HR_ADDR  = 30;

// size of eeprom image, cached
constexpr const uint16_t EEPROM_SIZE = 256;

// Max. count of HR clients (and a max addr)
#define c2temp(c) (c*2)
constexpr const uint8_t TEMP_OFF = c2temp(5) - 1; // 4.5 C is "off"
constexpr const uint8_t TEMP_OPEN = c2temp(30) + 1; // 30.5 C is "open"
constexpr const uint8_t TEMP_MIN = c2temp(5);
constexpr const uint8_t TEMP_MAX = c2temp(30);

// rfm version of OpenHR20 supports 8 timers a day
constexpr const uint8_t TIMER_SLOTS_PER_DAY = 8;

// timer has 7 slots for days and 1 slot extra for repeated everyday mode
constexpr const uint8_t TIMER_DAYS = 8;

// should we, by default, retain the published topics? Only last_seen
// is forcefully retained, other topics are not.
constexpr const bool MQTT_RETAIN = true;

// Reconnect attempt every N seconds
constexpr const time_t MQTT_RECONNECT_TIME = 10;

// Length of a log ring buffer (last N events)
constexpr const uint16_t EVENT_LOG_LEN = 64;

// Count of events per event request
constexpr const uint16_t MAX_JSON_EVENTS = 10;

// every 4 minutes NTP is updated
constexpr const int NTP_UPDATE_SECS = (4 * 60 * 1000);


} // namespace hr20
