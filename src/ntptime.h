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

#ifdef NTP_CLIENT
#include "../lib/NTPClient/NTPClient.h"
#include <WiFiUdp.h>
#include <Timezone.h>
#endif

// #include <Time.h>

#include "hr-debug.h"
#include "error.h"
#include "config.h"
#include "eventlog.h"

namespace hr20 {
namespace ntptime {

struct NTPTime {
#ifdef NTP_CLIENT
    /*** time management **/
    WiFiUDP ntpUDP;

    // You can specify the time server pool and the offset (in seconds, can be
    // changed later with setTimeOffset() ). Additionaly you can specify the
    // update interval (in milliseconds, can be changed using setUpdateInterval() ).
    NTPClient timeClient;

    long cur_slew = 0;

    //Central European Time (Frankfurt, Paris, Prague)
    TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     //Central European Summer Time
    TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       //Central European Standard Time
    Timezone tz;
#endif

    NTPTime()
#ifdef NTP_CLIENT
        : ntpUDP()
        , timeClient(ntpUDP, "europe.pool.ntp.org", 0, NTP_UPDATE_SECS) // 4 minute update time
        , tz(CEST, CET)
#endif
    {}


    void begin() {
#ifdef NTP_CLIENT
        timeClient.begin();
        // initial update
        bool b;
        update(true, b);
#endif

    }

    bool isSynced() {
#ifdef NTP_CLIENT
        return timeClient.isSynced();
#else
        return true;
#endif
    }

    time_t update(bool can_update, bool &changed_time) {
#ifdef NTP_CLIENT
        static time_t last_time = 0;
        time_t now = unixTime();
        changed_time = false;

        if ((now != last_time)) {
            // only work on these updates once every second, not more
            last_time = now;

            if (can_update) {
                NTPClient::UpdateState us;
                timeClient.update(us);

                if (us.error) ERR(NTP_CANNOT_SYNC);
                if (us.updated) {
                    EVENT(NTP_SYNCHRONIZED);
                    DBG("(NTP %ld)", us.drift);
                    changed_time = true;
                }

                // that needs ntpclient
                return now;
            }

            cur_slew = timeClient.slew();
        }

        return now;
#else
        return unixTime();
#endif
    }

    time_t unixTime() {
        return timeClient.getEpochTime();
    }

    time_t localTime() {
#ifdef NTP_CLIENT
        TimeChangeRule *tcr; // pointer to the time change rule, use to get TZ abbrev
        time_t utc = timeClient.getEpochTime();
        return  tz.toLocal(utc, &tcr);
#else
        return now();
#endif
    }

     int getMillis() {
#ifdef NTP_CLIENT
        return timeClient.getMillis();
#else
       return millis()%1000;
#endif
    }

};

} // namespace ntptime
} // namespace hr20
