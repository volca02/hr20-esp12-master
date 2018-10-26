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

#include "config.h"

namespace hr20 {

#define EVENT(CODE) \
    do { eventLog.append(EventType::EVENT, CODE, __LINE__); } while (0)
#define EVENT_ARG(CODE, VAL) \
    do { eventLog.append(EventType::EVENT, CODE, VAL); } while (0)

enum class EventType {
    EVENT = 1,
    ERROR = 2
};

// enumeration of possible EVENT type events
enum EventCode {
    INVALID_EVENT_CODE = 0

    // TODO: more codes
};

ICACHE_FLASH_ATTR const char * event_to_str(EventCode err);

// Event type
struct Event {
    EventType type;
    int code    = 0; // code is severity dependent, either EventCode or ErrorCode
    int value   = 0;
    time_t time = 0;
};

// Singleton event log ring buffer
struct EventLog {
    ICACHE_FLASH_ATTR void loop(time_t now) {
        this->now = now;
    }

    ICACHE_FLASH_ATTR void append(EventType type, int code, int val = 0) {
        auto &slot = events[pos];

        slot.type  = type;
        slot.code  = code;
        slot.value = val;
        slot.time  = now;

        pos = (pos + 1) % EVENT_LOG_LEN;
    }

    struct const_iterator {
        const_iterator(const EventLog &owner, uint16_t pos)
            : owner(owner), pos(pos)
        {}

        const Event &operator *() const {
            return owner.events[pos % EVENT_LOG_LEN];
        }

        const Event *operator->() const {
            return &owner.events[pos % EVENT_LOG_LEN];
        }

        bool operator==(const_iterator &o) {
            return pos % EVENT_LOG_LEN == o.pos % EVENT_LOG_LEN;
        }

        bool operator!=(const_iterator &o) {
            return pos % EVENT_LOG_LEN != o.pos % EVENT_LOG_LEN;
        }

        const_iterator &operator++() {
            --pos;
            return *this;
        }

        const EventLog &owner;
        uint8_t  pos;
    };

    const_iterator begin() const {
        return {*this, (pos + EVENT_LOG_LEN - 1) % EVENT_LOG_LEN};
    }

    const_iterator end() const {
        uint16_t endpos = pos;
        return {*this, endpos};
    }

protected:
    Event events[EVENT_LOG_LEN];
    uint16_t pos = 0;
    time_t now;
};

// global event log instance
extern EventLog eventLog;

} // namespace hr20
