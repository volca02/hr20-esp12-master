#pragma once

#include "value.h"
#include "timer.h"

using TimerSlot = SyncedValue<Timer>;

// models a single HR20 client
struct HR20 {
    time_t last_contact = 0;  // last contact
    bool synced = false;      // we have fully populated copy of values if true
    /** set to true means we have quite a few things to talk about with the
     * client and would appreciate a more frequent packet exchgange.
     * frequent comms with the client.
     */
    bool need_fat_comms = false;

    // == Controllable values ==
    // these are mirrored values - we sync them to HR20 when a change is requested
    // but only after we verify (read_time != 0) that we know them to differ

    // temperature wanted - update A[XX] (0.5 degres accuracy)
    SyncedValue<uint8_t, cvt::TempHalfC>  temp_wanted;
    // automatic/manual temperature selection - M[01]/M[00]
    SyncedValue<bool>     auto_mode;
    // false unlocked, true locked - L[01]/L[00]
    SyncedValue<bool>     menu_locked;

    // Timers - 8*8 16 bit values total - 128 bytes per HR20
    // R [Xx]
    // W [Xx][MT][TT]
    // X - Day of week 1-7 OR 0 - everyday timer
    // x - slot (0-7)
    // T - timer mode
    // T - 12 bit TIME of day
    // TIME is encoded in serial minutes since midnight (H*60+M)
    // here, we're storing the MTTT sequence in compatible format
    // (M being the highest 4 bits of the 16bit value, 12 lowest bits are time)
    TimerSlot timers[TIMER_DAYS][TIMER_SLOTS_PER_DAY];

    // == Just read from HR20 - not controllable ==
    // true means auto mode with temperature equal to requested
    CachedValue<bool>     test_auto;
    // true means open window
    CachedValue<bool>     mode_window;
    // average temperature [/0.01]C
    CachedValue<uint16_t, cvt::Temp001C> temp_avg;
    // average battery - [/0.001]V
    CachedValue<uint16_t, cvt::Voltage0001V> bat_avg;
    // desired valve position
    CachedValue<uint8_t>  cur_valve_wtd;
    // controller error
    CachedValue<uint8_t>  ctl_err;

    ICACHE_FLASH_ATTR void set_timer_mode(uint8_t day, uint8_t slot, const char *val) {
        if (day >= TIMER_DAYS) return;
        if (slot >= TIMER_SLOTS_PER_DAY) return;

        timers[day][slot].get_requested().set_mode(atoi(val) & 0x0F);
    }

    ICACHE_FLASH_ATTR void set_timer_time(uint8_t day, uint8_t slot, const char *val) {
        if (day >= TIMER_DAYS) return;
        if (slot >= TIMER_SLOTS_PER_DAY) return;

        uint16_t cvtd;
        if (cvt::TimeHHMM::from_str(val, cvtd))
            timers[day][slot].get_requested().set_time(cvtd);
    }
};

// Holds all clients in one array
struct Model {
    HR20 * ICACHE_FLASH_ATTR operator[](uint8_t idx) {
        if (idx >= MAX_HR_COUNT) {
            ERR("Client address out of range");
            return nullptr;
        }
        return &clients[idx];
    }

protected:
    HR20 clients[MAX_HR_COUNT];
};