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

#include "value.h"
#include "timer.h"
#include "str.h"

namespace hr20 {

using TimerSlot = SyncedValue<Timer>;

// models a single HR20 client
struct HR20 {
    HR20() {}

    HR20(const HR20 &) = delete; // not copyable
    const HR20 &operator = (const HR20 &) = delete;

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

    ICACHE_FLASH_ATTR bool set_timer_mode(uint8_t day,
                                          uint8_t slot,
                                          const Str &val)
    {
        if (day >= TIMER_DAYS) return false;
        if (slot >= TIMER_SLOTS_PER_DAY) return false;

        uint8_t cvtd;

        if (cvt::Simple::from_str(val, cvtd)) {
            timers[day][slot].get_requested().set_mode(cvtd & 0x0F);
            return true;
        }

        return false;
    }

    ICACHE_FLASH_ATTR bool set_timer_time(uint8_t day,
                                          uint8_t slot,
                                          const Str &val)
    {
        if (day >= TIMER_DAYS) return false;
        if (slot >= TIMER_SLOTS_PER_DAY) return false;

        uint16_t cvtd;
        if (cvt::TimeHHMM::from_str(val, cvtd)) {
            timers[day][slot].get_requested().set_time(cvtd);
            return true;
        }

        return false;
    }
};

// Holds all clients in one array
struct Model {
    Model() : index() {};

    ICACHE_FLASH_ATTR HR20 * operator[](uint8_t addr) {
        if (addr >= MAX_HR_ADDR) {
            ERR(PROTO_BAD_CLIENT_ADDR);
            return nullptr;
        }

        auto slot = index[addr];

        // do we have the client?
        if (slot > 0) {
            return &clients[slot - 1];
        }

        return nullptr;
    }

    // called in the discovery section, guarantees a slot.
    ICACHE_FLASH_ATTR HR20 *prepare_client(uint8_t addr) {
        if (addr >= MAX_HR_ADDR) {
            ERR(PROTO_BAD_CLIENT_ADDR);
            return nullptr;
        }

        auto slot = index[addr];

        // do we have the client?
        if (slot == 0) {
            if (cidx >= MAX_HR_COUNT) {
                ERR(PROTO_TOO_MANY_CLIENTS);
                return nullptr;
            }

            slot        = ++cidx;
            index[addr] = slot;
        }

        return &clients[slot - 1];
    }

protected:
    Model(const Model &) = delete;

    uint8_t index[MAX_HR_ADDR];
    uint8_t cidx = 0;
    HR20 clients[MAX_HR_COUNT];
};

} // namespace hr20
