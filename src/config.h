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

#include "hr-debug.h"

namespace hr20 {

// Max. length of a user-defined thermostat name
constexpr const uint8_t THERMO_NAME_MAX = 24;

// Max. count of HR clients
constexpr const uint8_t MAX_HR_COUNT = 8;
// Max. address (first invalid address, to be precise)
constexpr const uint8_t MAX_HR_ADDR  = 30;

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

    /// Optional user-defined thermostat names, format "ADDR:NAME,..."
    /// (e.g. "13:Kids room,15:Office"). ADDR is the numeric (1-2 digit)
    /// client address, NAME up to THERMO_NAME_MAX chars, no '"' or '\\'.
    /// Persisted by IotWebConf like the other fields.
    char thermo_names[128] = "";

    /// Parsed from thermo_names once at boot (parse_thermo_names).
    /// Offset into thermo_names; len == 0 means "no name for this addr".
    struct NameRef { uint8_t off; uint8_t len; };
    NameRef name_refs[MAX_HR_ADDR] = {};

    /// Parses thermo_names into name_refs (called after config load).
    /// Terminates every name in place (',' -> '\0'), so the buffer ends up
    /// as a set of C strings and names are guaranteed <= THERMO_NAME_MAX.
    void ICACHE_FLASH_ATTR parse_thermo_names() {
        // the buffer is a C string, no matter what got copied into it
        thermo_names[sizeof(thermo_names) - 1] = 0;
        for (auto &r : name_refs) r = NameRef{0, 0};
        for (char *p = thermo_names; *p;) {
            uint8_t a = 0;
            int nd = 0;
            while (nd < 2 && p[nd] >= '0' && p[nd] <= '9') {
                a = a * 10 + (uint8_t)(p[nd] - '0');
                ++nd;
            }
            if (nd == 0 || p[nd] != ':' || a >= MAX_HR_ADDR) break; // malformed
            p += nd + 1;
            char *delim = p;
            while (*delim && *delim != ',') ++delim;
            uint8_t full = (uint8_t)(delim - p);
            uint8_t nlen = full > THERMO_NAME_MAX ? THERMO_NAME_MAX : full;
            if (nlen) {
                name_refs[a] = NameRef{(uint8_t)(p - thermo_names), nlen};
                if (nlen < full) p[nlen] = 0; // truncate in place
                if (*delim == ',') {
                    *delim = 0; // name is a C string from now on
                    p = delim + 1;
                } else {
                    break; // end of the string, already NUL terminated
                }
            } else if (*delim == ',') {
                p = delim + 1; // empty name - skip
            } else {
                break;
            }
        }
    }

    /// Zero-copy name lookup from name_refs. Returns a C string from the
    /// config buffer (guaranteed <= THERMO_NAME_MAX), or nullptr.
    const char *ICACHE_FLASH_ATTR thermo_name(uint8_t addr) const {
        if (addr >= MAX_HR_ADDR) return nullptr;
        uint8_t len = name_refs[addr].len;
        return len ? thermo_names + name_refs[addr].off : nullptr;
    }
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

// size of eeprom image, cached
constexpr const uint16_t EEPROM_SIZE = 256;

// Max. count of HR clients (and a max addr)
#define c2temp(c) (c*2)
constexpr const uint8_t TEMP_OFF = c2temp(5) - 1; // 4.5 C is "off"
constexpr const uint8_t TEMP_OPEN = c2temp(30) + 1; // 30.5 C is "open"
constexpr const uint8_t TEMP_MIN = c2temp(5);
constexpr const uint8_t TEMP_MAX = c2temp(30);
// default setpoint used when none known yet, 21C
constexpr const uint8_t TEMP_DEFAULT = c2temp(21);

// rfm version of OpenHR20 supports 8 timers a day
constexpr const uint8_t TIMER_SLOTS_PER_DAY = 8;

// timer has 7 slots for days and 1 slot extra for repeated everyday mode
constexpr const uint8_t TIMER_DAYS = 8;

// should we, by default, retain the published topics? Only last_seen
// is forcefully retained, other topics are not.
constexpr const bool MQTT_RETAIN = true;

// Reconnect attempt every N seconds
constexpr const time_t MQTT_RECONNECT_TIME = 10;

// a client with no valid contact for this long is reported as unavailable
// (HA availability topic)
constexpr const time_t AVAIL_TIMEOUT_SECS = 15 * 60;

// Length of a log ring buffer (last N events)
constexpr const uint16_t EVENT_LOG_LEN = 64;

// Count of events per event request
constexpr const uint16_t MAX_JSON_EVENTS = 10;

// every 4 minutes NTP is updated
constexpr const int NTP_UPDATE_SECS = (4 * 60 * 1000);


} // namespace hr20
