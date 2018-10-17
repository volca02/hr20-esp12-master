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

#include "util.h"
#include "protocol.h"
#include "model.h"
#include "json.h"

namespace hr20 {
namespace json {

ICACHE_FLASH_ATTR void append_client_attr(String &str, const HR20 &client) {
    json::Object obj(str);

    // attributes follow.
    json::kv(obj, "mode", client.auto_mode.to_str());
    json::kv(obj, "lock", client.menu_locked.to_str());
    json::kv(obj, "window", client.mode_window.to_str());
    json::kv(obj, "temp", client.temp_avg.to_str());
    json::kv(obj, "battery", client.bat_avg.to_str());
    json::kv(obj, "temp_wanted", client.temp_wanted.to_str());
    json::kv(obj, "valve_wanted", client.cur_valve_wtd.to_str());
    json::kv(obj, "error", client.ctl_err.to_str());
    json::kv(obj, "last_contact", String(client.last_contact));
}


ICACHE_FLASH_ATTR void append_timer_day(String &str, const HR20 &m, uint8_t day)
{
    json::Object day_obj(str);

    for (uint8_t idx = 0; idx < TIMER_SLOTS_PER_DAY; ++idx) {
        // skip timer we don't know yet
        if (!m.timers[day][idx].remote_valid()) continue;

        // this index is synced.
        day_obj.key(idx);

        // remote timer is read
        const auto &remote = m.timers[day][idx].get_remote();

        // value is an object
        json::Object slot(day_obj);


        slot.key("time");
        json::str(str, cvt::TimeHHMM::to_str(remote.time()));
        slot.key("mode");
        json::str(str, cvt::Simple::to_str(remote.mode()));
    }
}

} // namespace json
} // namespace hr20
