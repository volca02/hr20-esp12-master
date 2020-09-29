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

#include "util.h"
#include "protocol.h"
#include "model.h"
#include "error.h"
#include "eventlog.h"
#include "json.h"
#include "mqtt.h"
#include "converters.h"

namespace hr20 {
namespace json {

static const char *S_AUTO      PROGMEM = "auto";
static const char *S_LOCK      PROGMEM = "lock";
static const char *S_WINDOW    PROGMEM = "window";
static const char *S_TEMP      PROGMEM = "temp";
static const char *S_BAT       PROGMEM = "bat";
static const char *S_TEMP_WTD  PROGMEM = "temp_wtd";
static const char *S_TEMP_WSET PROGMEM = "temp_wset";
static const char *S_VALVE_WTD PROGMEM = "valve_wtd";
static const char *S_ERROR     PROGMEM = "error";

static const char *S_LAST_SEEN  PROGMEM = "last_seen";
static const char *S_STATE      PROGMEM = "st";

void append_client_attr(StrMaker &str,
                        const HR20 &client)
{
    // this is intentionally cryptic as we have to fit in 128 bytes
    // for PubSubClient to be able to handle us
    json::Object obj(str);

    // attributes follow.
    cvt::ValueBuffer vb;
    json::kv_raw(obj, S_AUTO, client.auto_mode.to_str(vb));
    json::kv_raw(obj, S_LOCK, client.menu_locked.to_str(vb));
    json::kv_raw(obj, S_WINDOW, client.mode_window.to_str(vb));
    json::kv_raw(obj, S_TEMP, client.temp_avg.to_str(vb));
    json::kv_raw(obj, S_BAT, client.bat_avg.to_str(vb));
    json::kv_raw(obj, S_TEMP_WTD, client.temp_wanted.to_str(vb));
    json::kv_raw(obj, S_TEMP_WSET, client.temp_wanted.req_to_str(vb));
    json::kv_raw(obj, S_VALVE_WTD, client.cur_valve_wtd.to_str(vb));
    json::kv_raw(obj, S_ERROR, client.ctl_err.to_str(vb));

    // just for the info
    StrMaker sm{vb};
    sm += client.last_contact;
    json::kv_raw(obj, S_LAST_SEEN, sm.str());

    // trying to compress in a bit more extra info
    // bit 1 - needs basic values set on client (requested over mqtt)
    // bit 2 - needs to read more data from the client to be synced
    int state = (client.needs_basic_value_sync() ? 1 : 0)
                | (client.synced ? 0 : 2);

    {
        StrMaker sm1{vb};
        sm1 += state;
        json::kv_raw(obj, S_STATE, sm1.str());
    }
}

void append_timer_day(StrMaker &str,
                      const HR20 &m,
                      uint8_t day)
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


        cvt::ValueBuffer vb;
        slot.key(timer_topic_str(mqtt::TIMER_TIME));
        json::str(str, cvt::TimeHHMM::to_str(vb, remote.time()));
        slot.key(timer_topic_str(mqtt::TIMER_MODE));
        json::str(str, cvt::Simple::to_str(vb, remote.mode()));
    }
}

void append_event(StrMaker &str, const Event &ev) {
    json::Object obj(str);

    // attributes follow.
    cvt::ValueBuffer vb;
    json::kv_raw(obj, "type",  cvt::Simple::to_str(vb, (uint8_t)ev.type));
    switch (ev.type) {
    case EventType::EVENT:
        json::kv_str(
            obj, "name", event_to_str(static_cast<EventCode>(ev.code)));
        break;
    case EventType::ERROR:
        json::kv_str(obj, "name", err_to_str(static_cast<ErrorCode>(ev.code)));
        break;
    default:
        break;
    }
    json::kv_raw(obj, "value", cvt::Simple::to_str(vb, ev.value));
    json::kv_raw(obj, "time",  cvt::Simple::to_str(vb, ev.time));
}


} // namespace json
} // namespace hr20
