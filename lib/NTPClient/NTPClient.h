#pragma once

#include "Arduino.h"

#include <Udp.h>

#define SEVENZYYEARS 2208988800UL
#define NTP_PACKET_SIZE 48
#define NTP_DEFAULT_LOCAL_PORT 1337

class NTPClient {
  private:
    UDP*          _udp;
    bool          _udpSetup       = false;

    const char*   _poolServerName = "time.nist.gov"; // Default time server
    int           _port           = NTP_DEFAULT_LOCAL_PORT;
    int           _timeOffset     = 0;

    unsigned int  _updateInterval = 60000;  // In ms

    unsigned long _currentEpoc    = 0;      // In s
    unsigned long _epocMS         = 0;      // In ms - the millis() state when currentEpoc happened
    unsigned long _lastUpdate     = 0;      // In ms - when the fullUpdate last happened

    // In ms. Drift v.s. the NTP state. negative means we're behind schedule
    long _driftMS                 = 0;
    unsigned long _lastSlew       = 0;      // In ms - millis() when we last did slew() update

    byte          _packetBuffer[NTP_PACKET_SIZE];

    void          sendNTPPacket();

  public:
    NTPClient(UDP& udp);
    NTPClient(UDP& udp, int timeOffset);
    NTPClient(UDP& udp, const char* poolServerName);
    NTPClient(UDP& udp, const char* poolServerName, int timeOffset);
    NTPClient(UDP& udp, const char* poolServerName, int timeOffset, int updateInterval);

    /**
     * Starts the underlying UDP client with the default local port
     */
    void begin();

    /**
     * Starts the underlying UDP client with the specified local port
     */
    void begin(int port);


    struct UpdateState {
        bool updated; // did it update?
        bool error;   // if it tried to update and failed, this will be true
        long drift;   // current difference in miliseconds between server reported time and our time
    };

    /**
     * This should be called in the main loop of your application. By default an update from the NTP Server is only
     * made every 60 seconds. This can be configured in the NTPClient constructor.
     *
     * @return true on success, false on failure
     */
    bool update()
    {
        UpdateState s;
        update(s);
        return s.updated;
    };

    bool isSynced() {
        return _lastUpdate != 0;
    }

    /**
     *called about once in a while (perhaps every cycle if deemed needed) to slew the time diff
     * @return the current drift in ms (negative means we're behind)
    */
    long slew();

    /**
     * Full implementation of the update call - with more thorough update info.
     * implements slew as a part of the process to divert from abrupt time skips
     */
    void update(UpdateState& state);

    /**
     * This will force the update from the NTP Server.
     *
     * @return true on success, false on failure
     */
    void forceUpdate(UpdateState &state);

    int getDay();
    int getHours();
    int getMinutes();
    int getSeconds();
    int getMillis();

    /**
     * Changes the time offset. Useful for changing timezones dynamically
     */
    void setTimeOffset(int timeOffset);

    /**
     * Set the update interval to another frequency. E.g. useful when the
     * timeOffset should not be set in the constructor
     */
    void setUpdateInterval(int updateInterval);

    /**
     * @return time formatted like `hh:mm:ss`
     */
    String getFormattedTime();

    /**
     * @return time in seconds since Jan. 1, 1970
     */
    unsigned long getEpochTime();

    /**
     * Stops the underlying UDP client
     */
    void end();
};
