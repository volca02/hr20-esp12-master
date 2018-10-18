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

#include <Arduino.h>

// minimalistic json composition utilities

namespace hr20 {

struct HR20;

namespace json {

template<typename T>
inline void str(String &str, const T &val) {
    // NOTE: Does not escape, so it will break on some chars!
    str += '"';
    str += val;
    str += '"';
}

struct Object {
    ICACHE_FLASH_ATTR Object(String &s) : s(s) { s += '{'; }
    ICACHE_FLASH_ATTR Object(Object &o) : s(o.s) { s += '{'; }

    ICACHE_FLASH_ATTR ~Object() {
        s += '}';
    }


    template<typename T>
    inline void key(const T &name) {
        if (!first) s += ",";
        first = false;
        str(s, name);
        s += ":";
    }

    String &s;
    bool first = true;
};

// key value for Object
template<typename T, typename V>
inline void kv_str(Object &o, const T &name, const V &val) {
    o.key(name);
    str(o.s, val);
}

template<typename T, typename V>
inline void kv_raw(Object &o, const T &name, const V &val) {
    o.key(name);
    o.s += val;
}

void append_client_attr(String &str,
                        const HR20 &client);

void append_timer_day(String &str, const HR20 &m, uint8_t day);

} // namespace json
} // namespace hr20
