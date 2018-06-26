#pragma once

#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Timezone.h>
#include <Time.h>

namespace ntptime {

struct NTPTime {
#ifndef CLIENT_MODE
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
#endif

    NTPTime()
#ifndef CLIENT_MODE
        : ntpUDP()
        , timeClient(ntpUDP, "europe.pool.ntp.org", 0, 60000)
        , tz(CEST, CET)
#endif
    {}

    bool timeInSync = false;

    void begin() {
        timeClient.begin();
        timeInSync = timeClient.update();
    }

    bool update() {
        if (!timeInSync) {
            timeInSync = timeClient.update();
            return timeInSync;
        }
        return false;
    }

    time_t localTime() {
#ifdef CLIENT_MODE
        return now();
#else
        TimeChangeRule *tcr; // pointer to the time change rule, use to get TZ abbrev
        time_t utc = timeClient.getEpochTime();
        return  tz.toLocal(utc, &tcr);
#endif
    }

    void printRTC(){
        time_t local = localTime();

        Serial.printf("RTC: %02d.%02d.%02d %02d:%02d:%02d\r\n",
                      day(local), month(local), year(local)-2000,
                      hour(local), minute(local), second(local));
    }
};

} // namespace ntptime
