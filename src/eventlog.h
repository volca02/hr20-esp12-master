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

enum class EventType : uint8_t {
    EVENT = 1,
    ERROR = 2
};

// enumeration of possible EVENT type events
enum class EventCode {
    INVALID_EVENT_CODE = 0,

    // protocol. sending/receiving of packets
    PROTO_PACKET_RECEIVED = 1, // received packet, arg is addr
    PROTO_PACKET_SENDING  = 2, // packet was queued for sending, arg is addr
    PROTO_PACKET_SYNC     = 3, // sync is being sent
    PROTO_HANDLED_OPS     = 4, // handled bitmap of operations on incoming packet

    // mqtt. publishes/subscription callbacks
    MQTT_PUBLISH          = 50, // a publish was done
    MQTT_CALLBACK         = 51, // mqtt callback was called
    MQTT_CONN             = 52, // (re)connected to mqtt server
    MQTT_SUBSCRIBE        = 53, // subscribed to a topic
    // ntp
    NTP_SYNCHRONIZED      = 60,
};

// bitmap of received command responses in the received packet
enum ProtoCommand {
    PROTO_CMD_VER    = 1,
    PROTO_CMD_TMP    = 2,
    PROTO_CMD_DBG    = 4,
    PROTO_CMD_WTCH   = 8,
    PROTO_CMD_TMR    = 16,
    PROTO_CMD_EEPROM = 32,
    PROTO_CMD_LOCK   = 64,
    PROTO_CMD_REBOOT = 128
};

ICACHE_FLASH_ATTR const char * event_to_str(EventCode err);

// Event type
struct Event {
    EventType type;
    uint8_t  code  = 0; // code is severity dependent, either EventCode or ErrorCode
    uint16_t value = 0;
    time_t   time  = 0;
};

// Singleton event log ring buffer
struct EventLog {
    ICACHE_FLASH_ATTR void update(time_t now) {
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
        return {
            *this,
            static_cast<uint16_t>((pos + EVENT_LOG_LEN - 1) % EVENT_LOG_LEN)};
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

// appends events only
inline void append_event(EventCode code, int param) {
    eventLog.append(EventType::EVENT, static_cast<int>(code), param);
}

#define EVENT(CODE) \
    do { append_event(EventCode::CODE, __LINE__); } while (0)
#define EVENT_ARG(CODE, VAL) \
    do { append_event(EventCode::CODE, VAL); } while (0)

} // namespace hr20
