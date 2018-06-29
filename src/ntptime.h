#pragma once

#ifdef NTP_CLIENT
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Timezone.h>
#endif

#include <Time.h>

namespace ntptime {

struct NTPTime {
#ifdef NTP_CLIENT
    /*** time management **/
    WiFiUDP ntpUDP;

    // You can specify the time server pool and the offset (in seconds, can be
    // changed later with setTimeOffset() ). Additionaly you can specify the
    // update interval (in milliseconds, can be changed using setUpdateInterval() ).
    NTPClient timeClient;

    //Central European Time (Frankfurt, Paris, Prague)
    TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     //Central European Summer Time
    TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       //Central European Standard Time
    Timezone tz;
    bool timeInSync = false;
#endif


    NTPTime()
#ifdef NTP_CLIENT
        : ntpUDP()
        , timeClient(ntpUDP, "europe.pool.ntp.org", 0, 60000)
        , tz(CEST, CET)
#endif
    {}


    void begin() {
#ifdef NTP_CLIENT
        timeClient.begin();
        timeInSync = timeClient.update();
#endif
    }

    bool update() {
#ifdef NTP_CLIENT
        if (!timeInSync) {
            timeInSync = timeClient.update();
            return timeInSync;
        }
#endif
        return false;
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
};

} // namespace ntptime
