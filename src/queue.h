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

#include <cstdint>

namespace hr20 {

// implements byte FIFO queue of fixed max size.
// NOTE: Not using ICACHE_FLASH_ATTR here as this gets used from ISR too
template<uint8_t LenT>
struct ShortQ {
    uint8_t buf[LenT];
    // volatile as we're handling these from interrupt too
    volatile uint8_t _pos = 0, _top = 0;

    bool push(uint8_t c) {
        if (full()) return false;
        buf[_top++] = c;
        return true;
    }

    uint8_t pos() const {
        return _pos;
    }

    uint8_t pop() {
        uint8_t c = 0x0;

        if (_pos < _top) c = buf[_pos++];

        // reset Q if we emptied it
        if (_pos >= _top) {
            clear();
        }

        return c;
    }

    uint8_t peek() const {
        if (_pos < _top)
            return buf[_pos];

        return 0x0;
    }

    bool empty() const { return _pos == _top; }
    bool full() const  { return _top >= LenT; }

    // raw data access for packet storage
    uint8_t *data() { return buf; }
    const uint8_t *data() const { return buf; }
    // remaining data size
    uint8_t rest_size() const { return _top - _pos; }
    // size from the begining of the buffer
    uint8_t size() const { return _top; }
    void clear() {
        _pos = 0;
        _top = 0;
    }

    uint8_t free_size() const {
        if (_top > LenT)
            return 0;
        return LenT - _top;
    }

    // trims extra bytes from queue end
    bool trim(uint8_t count) {
        if (count >= _top || (_pos + count) >= _top) {
            clear();
            return false;
        }

        _top -= count;
        return true;
    }

    uint8_t operator[](uint8_t idx) const { return buf[idx]; }
};

} // namespace hr20
