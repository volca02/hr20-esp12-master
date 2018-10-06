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

struct Timer {
    Timer(uint16_t val = 0) : timer(val) {}

    uint8_t hour() const {
        return time() / 60;
    }

    uint8_t min() const {
        return time() % 60;
    }

    uint16_t time() const {
        return (timer & 0x0fff);
    }

    uint8_t mode() const {
        return timer >> 12;
    }

    void set_hour(uint8_t shour) {
        set(mode(), shour, min());
    }

    void set_min(uint8_t smin) {
        set(mode(), hour(), smin);
    }

    void set_mode(uint8_t smode) {
        set(smode, hour(), min());
    }

    void set_time(uint8_t shour, uint8_t smin) {
        set(mode(), shour, smin);
    }

    void set_time(uint16_t time) {
        set(mode(), time / 60, time % 60);
    }

    void set(uint8_t shour, uint8_t smin, uint8_t smode) {
        // some mandatory fixes to stop producing crap timers
        shour = shour % 24; // wraparound in one day

        if (smin >= 60) // no good way to handle this...
            smin = 0;

        timer = smode << 12 | (shour * 60 + smin);
    }

    uint16_t raw() const { return timer; }

    uint16_t operator=(uint16_t val) {
        return timer = val;
    }

    bool operator==(const Timer &other) const { return timer == other.timer; }
    bool operator!=(const Timer &other) const { return timer != other.timer; }

protected:
    uint16_t timer;
};
