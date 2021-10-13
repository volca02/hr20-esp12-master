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

#include "str.h"

// minimalistic json composition utilities
namespace hr20 {

struct HR20;
struct Event;

namespace json {

template<typename T>
inline void str(StrMaker &str, const T &val) {
    // NOTE: Does not escape, so it will break on some chars!
    str += '"';
    str += val;
    str += '"';
}

struct Comma {
    inline void next(StrMaker &s) {
        if (!first) s += ',';
        first = false;
    }

    bool first = true;
};

struct Element {
    Element(StrMaker &s)  : s(s)   {}
    Element(Element &e) : s(e.s) {}
    ~Element() {}
    StrMaker &s;
};

struct Object : public Element {
    Object(StrMaker &s) : Element(s) { s += '{'; }
    Object(Object &o) : Element(o) { s += '{'; }

    inline ~Object() {
        s += '}';
    }

    template<typename T>
    inline void key(const T &name) {
        comma.next(s);
        str(s, name);
        s += ':';
    }

    Comma comma;
};

struct Array : public Element {
    Array(StrMaker &s)  : Element(s) { s += '['; }
    Array(Element &b) : Element(b) { s += '['; }

    ICACHE_FLASH_ATTR void element() {
        comma.next(s);
    }

    ~Array() {
        s += ']';
    }

    Comma comma;
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

void append_client_attr(StrMaker &str, const HR20 &client);
void append_timer_day(StrMaker &str, const HR20 &m, uint8_t day);
void append_event(StrMaker &s, const Event &ev);

} // namespace json
} // namespace hr20
