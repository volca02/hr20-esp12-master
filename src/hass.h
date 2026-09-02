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

#include "str.h"

namespace hr20 {

/// Home Assistant autodiscovery.
///
/// For every client we publish two retained discovery configs:
///   homeassistant/climate/hr20_<ADDR>/config   - climate entity
///   homeassistant/sensor/hr20_<ADDR>_bat/config - battery sensor
/// where <ADDR> is the 2-digit client address. Both entities are grouped
/// into one HA device.
///
/// The payloads are fixed flash-resident templates with placeholders:
///   ##  - replaced in-place (same length) by the 2-digit client address
///   @   - replaced by the user-configured MQTT topic prefix (appears
///         once as the "~" value)
///   @@  - replaced by the (user-defined or default) entity name
///   %%  - replaced by the device name (user-defined or "HR20 <addr>")
/// @, @@ and %% are the variable-length substitutions.
///
/// The "~" key makes Home Assistant rewrite every topic value starting
/// with "~" to <prefix><rest>, so all the other topic values stay short.
/// ("~" base topic replacement has been supported by HA since 2018.)
namespace hass {

static const char T_CLIMATE_TOPIC[] = "homeassistant/climate/hr20_##/config";
static const char T_SENSOR_TOPIC[]  = "homeassistant/sensor/hr20_##_bat/config";

// the climate entity is just "Thermostat" - the room identity lives in
// the device name, so two entities stay distinguishable in device lists
static const char T_CLIMATE_CONF[] =
"{"
"\"~\":\"@\","
"\"unique_id\":\"hr20_##\","
"\"name\":\"Thermostat\","
"\"availability_topic\":\"~/##/avail\","
"\"mode_command_topic\":\"~/set/##/mode\","
"\"temperature_command_topic\":\"~/set/##/temp\","
"\"temperature_state_topic\":\"~/##/temp\","
"\"mode_state_topic\":\"~/##/mode\","
"\"min_temp\":5,"
"\"max_temp\":30,"
"\"temp_step\":0.5,"
"\"modes\":[\"off\",\"heat\",\"auto\"],"
"\"device\":{\"ids\":\"hr20_##\",\"name\":\"%%\",\"mf\":\"OpenHR20\",\"mdl\":\"HR20\"}"
"}";

// the battery sensor - shares the availability topic with the climate
static const char T_SENSOR_CONF[] =
"{"
"\"~\":\"@\","
"\"unique_id\":\"hr20_##_bat\","
"\"name\":\"@@\","
"\"device_class\":\"battery\","
"\"state_class\":\"measurement\","
"\"unit_of_measurement\":\"%\","
"\"availability_topic\":\"~/##/avail\","
"\"state_topic\":\"~/##/battery_pct\","
"\"device\":{\"ids\":\"hr20_##\",\"name\":\"%%\",\"mf\":\"OpenHR20\",\"mdl\":\"HR20\"}"
"}";

// buffer for one composed discovery payload - worst case: climate template
// (437) + prefix (40) + device name (24)
using ConfigBuffer = BufferHolder<512>;

/// Copies the flash template into buf, replacing the placeholders.
/// Returns an invalid (null ptr) Str if the result does not fit.
ICACHE_FLASH_ATTR inline Str compose(const char *tpl, uint8_t addr,
                                     const char *prefix, const char *name,
                                     const char *devname, Buffer b)
{
    StrMaker sm(b);

    for (const char *p = tpl; *p; ++p) {
        if (*p == '@' && p[1] == '@') {
            // variable length name - shifts the rest of the string
            sm += name;
            ++p;
        } else if (*p == '%' && p[1] == '%') {
            // variable length device name - shifts the rest of the string
            sm += devname;
            ++p;
        } else if (*p == '@') {
            // variable length prefix - shifts the rest of the string
            sm += prefix;
        } else if (*p == '#' && p[1] == '#') {
            // 2-digit zero-padded address, in-place
            sm += static_cast<char>('0' + addr / 10);
            sm += static_cast<char>('0' + addr % 10);
            ++p;
        } else {
            sm += *p;
        }

        if (sm.invalid()) return {};
    }

    return sm.str();
}

} // namespace hass
} // namespace hr20
