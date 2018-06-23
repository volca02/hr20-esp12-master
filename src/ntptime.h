#pragma once

#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Timezone.h>
#include <Time.h>

namespace ntptime {

struct NTPTime {
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

    NTPTime()
        : ntpUDP()
        , timeClient(ntpUDP, "europe.pool.ntp.org", 0, 60000)
        , tz(CEST, CET)
    {}

    bool timeInSync = false;

    void begin() {
        timeClient.begin();
        timeInSync = timeClient.update();
    }

    void update() {
        if (!timeInSync) {
            timeInSync = timeClient.update();
        }
    }

    time_t localTime() {
        // TODO: in client_mode we'd use now(), in master mode we'll use NTPClient
/*        TimeChangeRule *tcr; // pointer to the time change rule, use to get TZ abbrev
        time_t utc = timeClient.getEpochTime();
        return  tz.toLocal(utc, &tcr);*/
        return now();
    }

    void printRTC(){
        time_t local = localTime();

/*
        static char sDate[9];
        static char sTime[11];
        int mil = timeClient.getMillis();

        sprintf(sDate, "Y%02x%02x%02x", year(local)-2000, month(local), day(local));
        sprintf(sTime, "H%02x%02x%02x%02x", hour(local), minute(local), second(local), mil/10);

        Serial.printf("RTC: %s %s\n",sDate, sTime);*/

        Serial.printf("RTC: %02d.%02d.%02d %02d:%02d:%02d\r\n",
                      day(local), month(local), year(local)-2000,
                      hour(local), minute(local), second(local));
    }
};

} // namespace ntptime
