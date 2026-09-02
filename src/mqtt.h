/*
 * HR20 ESP Master
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
#include <PubSubClient.h>
#include <WiFiClient.h>

#include "config.h"
#include "hr-debug.h"
#include "error.h"
#include "master.h"
#include "util.h"
#include "json.h"
#include "str.h"
#include "hass.h"

namespace hr20 {
namespace mqtt {

// topic enum. Each has different initial letter for simple parsing
enum Topic {
    CUR_TMP = 1,
    BAT     = 2,
    ERR     = 3,
    LOCK    = 4,
    AUTO    = 5,
    TMP     = 6,
    VALVE_WTD = 7,
    WND       = 8,
    LAST_SEEN = 9,
    TIMER     = 10,
    STATE     = 11,
    EEPROM    = 12,
    MODE      = 13,
    BATT_PCT  = 14,
    AVAIL     = 15,
    INVALID_TOPIC = 255
};

enum TimerTopic {
    TIMER_NONE = 0,
    TIMER_TIME,
    TIMER_MODE,
    INVALID_TIMER_TOPIC = 255
};

enum EEPROMAccess {
    EA_READ  = 0,
    EA_WRITE = 1,
    INVALID_EEPROM_TOPIC = 255
};

enum Mode {
    MODE_OFF = 0,
    MODE_AUTO,
    MODE_MANUAL,
    MODE_OPEN,
    INVALID_MODE_TYPE = 255
};

static const char *S_CUR_TMP   = "current_temp";
static const char *S_BAT       = "battery";
static const char *S_BATT_PCT  = "battery_pct";
static const char *S_ERR       = "error";
static const char *S_EEPROM     = "eeprom";
static unsigned    S_EEPROM_LEN = 6;
static const char *S_LOCK      = "lock";
static const char *S_MODE      = "mode"; // this is a OFF/MANUAL/AUTO mode info topic
static const char *S_AUTO      = "auto";
static const char *S_TMP       = "temp";
static const char *S_VALVE_WTD = "valve_wanted";
static const char *S_WND       = "window";
static const char *S_LAST_SEEN = "last_seen";
static const char *S_STATE     = "state";
static const char *S_AVAIL     = "avail";
static const char *S_AVAIL_ON  = "online";
static const char *S_AVAIL_OFF = "offline";

static const char *S_TIMER     = "timer";
static unsigned    S_TIMER_LEN = 5;

// timer subtopics
static const char *S_TIMER_MODE = "mode";
static const char *S_TIMER_TIME = "time";

// set topic branch mid-prefix
static const char *S_SET_MODE   = "set";

// eeprom access strs
static const char *S_EA_READ  = "read";
static const char *S_EA_WRITE = "write";

// valve modes
static const char *S_MODE_OFF    = "off";
static const char *S_MODE_AUTO   = "auto";
// heat is the HA name for manual mode
static const char *S_MODE_HEAT   = "heat";
// legacy alias for heat
static const char *S_MODE_MANUAL = "manual";
static const char *S_MODE_OPEN   = "open";

constexpr const uint8_t MAX_MQTT_PATH_LENGTH = 128;
using PathBuffer = BufferHolder<MAX_MQTT_PATH_LENGTH>;

ICACHE_FLASH_ATTR static const char *topic_str(Topic topic) {
    switch (topic) {
    case CUR_TMP:   return S_CUR_TMP;
    case BAT:       return S_BAT;
    case ERR:       return S_ERR;
    case EEPROM:    return S_EEPROM;
    case LOCK:      return S_LOCK;
    case AUTO:      return S_AUTO;
    case MODE:      return S_MODE;
    case TMP:       return S_TMP;
    case VALVE_WTD: return S_VALVE_WTD;
    case WND:       return S_WND;
    case LAST_SEEN: return S_LAST_SEEN;
    case STATE:     return S_STATE;
    case TIMER:     return S_TIMER;
    case BATT_PCT:  return S_BATT_PCT;
    case AVAIL:     return S_AVAIL;
    default:
        return nullptr;
    }
}

ICACHE_FLASH_ATTR static const char *eeprom_access_str(EEPROMAccess ea) {
    switch (ea) {
    case EA_READ:   return S_EA_READ;
    case EA_WRITE:  return S_EA_WRITE;
    default:
        return nullptr;
    }
}

ICACHE_FLASH_ATTR static const char *timer_topic_str(TimerTopic sub) {
    switch (sub) {
    case TIMER_TIME: return S_TIMER_TIME;
    case TIMER_MODE: return S_TIMER_MODE;
    default:
        return nullptr;
    }
}

ICACHE_FLASH_ATTR static Topic parse_topic(const char *top) {
    switch (*top) {
    case 'a':
        if (strcmp(top, S_AUTO) == 0) return AUTO;
        if (strcmp(top, S_AVAIL) == 0) return AVAIL;
        return INVALID_TOPIC;
    case 'b':
        if (strcmp(top, S_BAT) == 0) return BAT;
        if (strcmp(top, S_BATT_PCT) == 0) return BATT_PCT;
        return INVALID_TOPIC;
    case 'c':
        if (strcmp(top, S_CUR_TMP) == 0) return CUR_TMP;
        return INVALID_TOPIC;
    case 'e':
        if (strcmp(top, S_ERR) == 0) return ERR;
        if (strncmp(top, S_EEPROM, S_EEPROM_LEN) == 0) return EEPROM;
        return INVALID_TOPIC;
    case 'l':
        if (strcmp(top, S_LOCK) == 0) return LOCK;
        if (strcmp(top, S_LAST_SEEN) == 0) return LAST_SEEN;
        return INVALID_TOPIC;
    case 'm':
        if (strcmp(top, S_MODE) == 0) return MODE;
        return INVALID_TOPIC;
    case 's':
        if (strcmp(top, S_STATE) == 0) return STATE;
        return INVALID_TOPIC;
    case 't':
        if (strcmp(top, S_TMP) == 0) return TMP;
        if (strncmp(top, S_TIMER, S_TIMER_LEN) == 0) return TIMER;
        return INVALID_TOPIC;
    case 'v':
        if (strcmp(top, S_VALVE_WTD) == 0) return VALVE_WTD;
        return INVALID_TOPIC;
    case 'w':
        if (strcmp(top, S_WND) == 0) return WND;
        return INVALID_TOPIC;
    default:
        return INVALID_TOPIC;
    }
}

ICACHE_FLASH_ATTR static TimerTopic parse_timer_topic(const char *top) {
    if (!top) return INVALID_TIMER_TOPIC;

    if (top[0] == 't') {
        if (strcmp(top, S_TIMER_TIME) == 0) return TIMER_TIME;
        return INVALID_TIMER_TOPIC;
    }

    if (top[0] == 'm') {
        if (strcmp(top, S_TIMER_MODE) == 0) return TIMER_MODE;
        return INVALID_TIMER_TOPIC;
    }

    return INVALID_TIMER_TOPIC;
}

ICACHE_FLASH_ATTR static EEPROMAccess parse_eeprom_access(const char *top) {
    if (!top) return INVALID_EEPROM_TOPIC;

    if (top[0] == 'r') {
        if (strcmp(top, S_EA_READ) == 0) return EA_READ;
        return INVALID_EEPROM_TOPIC;
    }

    if (top[0] == 'w') {
        if (strcmp(top, S_EA_WRITE) == 0) return EA_WRITE;
        return INVALID_EEPROM_TOPIC;
    }

    return INVALID_EEPROM_TOPIC;
}

ICACHE_FLASH_ATTR static Mode parse_mode(const Str &top) {
    if (top == "") return INVALID_MODE_TYPE;

    if (top[0] == 'o') {
        if (top == S_MODE_OFF) return MODE_OFF;
        if (top == S_MODE_OPEN) return MODE_OPEN;
    }

    if (top[0] == 'a') {
        if (top == S_MODE_AUTO) return MODE_AUTO;
    }

    // heat is the HA name of manual mode, manual is the legacy alias
    if (top[0] == 'h') {
        if (top == S_MODE_HEAT) return MODE_MANUAL;
    }

    if (top[0] == 'm') {
        if (top == S_MODE_MANUAL) return MODE_MANUAL;
    }

    return INVALID_MODE_TYPE;
}

// mqtt path parser/composer
struct Path {
    static const char SEPARATOR = '/';
    static const char WILDCARD  = '#';
    static const char *prefix;

    // static method that overrides prefix
    ICACHE_FLASH_ATTR static void begin(const char *pfx) {
        prefix = pfx;
    }

    Path() {}

    // constructor for normal or timer topics
    Path(uint8_t addr,
         Topic t,
         bool set_mode = false,
         TimerTopic st = TIMER_NONE,
         uint8_t day   = 0,
         uint8_t slot  = 0)
        : addr(addr),
          day(day),
          slot(slot),
          topic(t),
          timer_topic(st),
          set_mode(set_mode)
    {}

    // this constructs eeprom access topics
    Path(uint8_t addr,
         bool set_mode,
         EEPROMAccess ea,
         uint8_t ee_address)
        : addr(addr),
          day(0),
          slot(0),
          topic(EEPROM),
          timer_topic(TIMER_NONE),
          set_mode(set_mode),
          eeprom_access(ea),
          eeprom_address(ee_address)
    {}

    ICACHE_FLASH_ATTR static Str compose_set_prefix_wildcard(Buffer b) {
        StrMaker rv(b);

        rv += prefix;
        rv += SEPARATOR;
        rv += S_SET_MODE;
        rv += SEPARATOR;
        rv += WILDCARD;

        return rv.str();
    }


    ICACHE_FLASH_ATTR Str compose(Buffer b) const {
        StrMaker rv(b);

        rv += prefix;
        rv += SEPARATOR;

        if (set_mode) {
            rv += S_SET_MODE;
            rv += SEPARATOR;
        }

        // 2-digit zero-padded address - keeps autodiscovery lengths fixed
        rv += static_cast<char>('0' + addr / 10);
        rv += static_cast<char>('0' + addr % 10);
        rv += SEPARATOR;
        rv += topic_str(topic);

        if (topic == EEPROM) {
            rv += SEPARATOR;
            rv += eeprom_address;

            // In set mode we include read/write op. specifier
            if (set_mode) {
                rv += SEPARATOR;
                rv += eeprom_access_str(eeprom_access);
            }
        } else if (topic == TIMER) {
            rv += SEPARATOR;
            rv += day;
            rv += SEPARATOR;
            rv += slot;
            rv += SEPARATOR;
            rv += timer_topic_str(timer_topic);
        }

        return rv.str();
    }

    ICACHE_FLASH_ATTR static Path parse(const char *p) {
        // compare prefix first
        const char *pos = p;
        bool set_mode = false;

        // skips the prefix path and compares if it equals
        // also skips leading separators
        // skip the prefix (1..n tokens)
        pos = skip_prefix(pos, prefix);

        // prefix does not match!
        if (!pos) return {};

        // premature end (just the prefix)
        if (!*pos) return {};

        // tokenize the address
        auto addr = token(pos);

        static const Token S_SET_TOK{S_SET_MODE, strlen(S_SET_MODE)};

        // is it by chance a set sub_branch?
        if (cmp_tokens(addr, S_SET_TOK)) {
            pos = skip_token(addr);
            set_mode = true;

            if (!pos) return {};

            // re-read the token for address
            addr = token(pos);
        }

        // convert to number
        uint8_t address = to_num(&pos, addr.second);

        // is the next char a separator? if not then it wasn't a valid path
        if (*pos != Path::SEPARATOR) return {};

        ++pos;

        // now follows the ending element. Parse via parse_topic
        Topic top = parse_topic(pos);

        if (top == INVALID_TOPIC) return {};

        if (top == EEPROM) {
            // eeprom is a sub-tree
            // here we see these
            // set/.../eeprom/addr/read  - read request to an address (value sent is ignored)
            // set/.../eeprom/addr/write - write request with value for addr written to this topic
            // .../eeprom/addr - value as gathered from client with specified topic

            // skip the token 'eeprom'
            auto ee_topic_t = token(pos);
            pos = ee_topic_t.first + ee_topic_t.second;
            if (*pos != Path::SEPARATOR) return {};
            ++pos;

            // unless read mode is set, we expect an address as a continuation
            auto addr_t = token(pos);
            uint8_t ee_addr = to_num(&pos, addr_t.second);

            // in set mode we expect either read/write tokens next
            EEPROMAccess ea = EA_READ;
            if (set_mode) {
                // is the next char a separator? if not then it wasn't a valid path
                if (*pos != Path::SEPARATOR) return {};
                ++pos;

                ea = parse_eeprom_access(pos);

                if (ea == INVALID_EEPROM_TOPIC) return {};
            }

            return {address, set_mode, ea, ee_addr};
        } else if (top == TIMER) {
            // timer subtree... .../timer/day/slot/[mode/time]
            // skip the 'timer' token
            auto tt_topic_t = token(pos);
            pos = tt_topic_t.first + tt_topic_t.second;

            if (*pos != Path::SEPARATOR) return {};
            ++pos;

            // day
            auto d_t = token(pos);
            uint8_t d = to_num(&pos, d_t.second);

            // is the next char a separator? if not then it wasn't a valid path
            if (*pos != Path::SEPARATOR) return {};
            ++pos;

            auto s_t = token(pos);
            uint8_t s = to_num(&pos, s_t.second);

            // is the next char a separator? if not then it wasn't a valid path
            if (*pos != Path::SEPARATOR) return {};
            ++pos;

            // now timer topic
            auto tt = parse_timer_topic(pos);

            if (tt == INVALID_TIMER_TOPIC) return {};

            // whole timer specification is okay
            return {address, top, set_mode, tt, d, s};
        }

        return {address, top, set_mode};
    }

    ICACHE_FLASH_ATTR static uint8_t to_num(const char **p, uint8_t cnt) {
        uint8_t res = 0;
        for (;cnt--;++(*p)) {
            if (**p == 0) return res;

            switch (**p) {
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                    res *= 10;
                    res += static_cast<uint8_t>(**p - '0');
                    continue;
            default:
                return res;
            }
        }
        return res;
    }

    using Token = std::pair<const char*, uint8_t>;

    ICACHE_FLASH_ATTR static const char *skip_token(const Token &t) {
        // skips the token chars plus optionally a separator
        const char *end = t.first + t.second;
        return skip_separator(end);
    }

    // skips a separator, and if there is nothing past it, it returns a nullptr
    // returns `other` if the separator is missing (defaults to nullptr)
    ICACHE_FLASH_ATTR static const char *skip_separator(
        const char *p, const char *other = nullptr)
    {
        // skip initial SEPARATOR if present
        if (!p) return nullptr;
        // this might not be obvious, but it includes a trailing '\0'
        if (*p != SEPARATOR) return other;
        ++p;
        if (*p == 0) return nullptr;
        return p;
    }


    ICACHE_FLASH_ATTR static const char *skip_prefix(const char *p, const char *pfx) {
        // also skip any initial separators in prefix path
        // the second arg is there to ignore if the separator is missing
        p   = skip_separator(p, p);
        pfx = skip_separator(pfx, pfx);

        // do we still have something to process?
        while (p != nullptr && pfx != nullptr) {
            auto p_t    = token(p);
            auto pfx_t  = token(pfx);

            // is the first token same as prefix?
            p = cmp_tokens(p_t, pfx_t);

            // differing tokens?
            if (!p) return nullptr;

            // and onto the next part of the path
            p   = skip_token(p_t);
            pfx = skip_token(pfx_t);
        }

        // still some prefix left? if so we didn't eat all tokens of it and have
        // to bail
        if (pfx) return nullptr;

        return p;
    }

    ICACHE_FLASH_ATTR static const char *cmp_tokens(const Token &a, const Token &b) {
        if (a.second != b.second) return nullptr;
        return (strncmp(a.first, b.first, a.second) == 0) ? a.first + a.second : nullptr;
    }

    // returns separator pos (or zero byte) and number of bytes that it took to
    // get there
    ICACHE_FLASH_ATTR static Token token(const char *p) {
        const char *pos = p;
        for(;*p;++p) {
            if (*p == Path::SEPARATOR) {
                break;
            }
        }

        return {pos, p - pos};
    }

    ICACHE_FLASH_ATTR bool valid() { return addr != 0; }

    // compressed topic code for debugging (addr 5 bits, topic 4 bits, timer topic 2 bits)
    ICACHE_FLASH_ATTR uint16_t as_uint() const {
        return addr | ((uint16_t)topic << 5) | ((uint16_t)timer_topic << 9);
    }

    // client ID. 0 means invalid path!
    uint8_t addr = 0;
    uint8_t day  = 0;
    uint8_t slot = 0;
    Topic topic            = INVALID_TOPIC;
    TimerTopic timer_topic = TIMER_NONE;
    bool    set_mode = false; // true in the S_SET_MODE sub-branch

    EEPROMAccess eeprom_access = EA_READ;
    uint8_t eeprom_address = 0; // eeprom address in case topic is EEPROM
};

// remembers a usable (5..30C) setpoint so it can be restored when
// leaving off/open mode - the modes themselves don't carry a setpoint
ICACHE_FLASH_ATTR static void remember_setpoint(HR20 *hr) {
    if (hr->temp_wanted.remote_valid()) {
        auto t = hr->temp_wanted.get_remote();
        if (t >= TEMP_MIN && t <= TEMP_MAX) hr->last_setpoint = t;
    }
}

// if the current setpoint is the off/open magic (or unknown), put a
// usable one back so that switching to heat/auto doesn't leave the
// valve at the off position
ICACHE_FLASH_ATTR static void restore_setpoint(HR20 *hr) {
    bool valid = hr->temp_wanted.remote_valid();
    uint8_t cur = valid ? hr->temp_wanted.get_remote() : 0;

    if (!valid || cur == TEMP_OFF || cur == TEMP_OPEN) {
        auto last = hr->last_setpoint;
        hr->temp_wanted.set_requested(
            (last >= TEMP_MIN && last <= TEMP_MAX) ? last : TEMP_DEFAULT);
    }
}

/// Publishes/receives topics in mqtt
struct MQTTPublisher {
    // TODO: Make this configurable!
    MQTTPublisher(Config &config,
                  HR20Master &master)
        : config(config),
          master(master),
          wifiClient(),
          client(wifiClient)
    {
        for (uint8_t i = 0; i < MAX_HR_ADDR; ++i) states[i] = 0;
    }

    ICACHE_FLASH_ATTR void begin() {
        client.setServer(config.mqtt_server, atoi(config.mqtt_port));
        client.setCallback([&](char *topic, byte *payload, unsigned int length)
                           {
                               callback(topic, payload, length);
                           });
        // every change gets a bitmask info here
        master.proto.set_callback([&](uint8_t addr, uint32_t mask) {
                                      states[addr] |= mask;
                                  });
    }

    ICACHE_FLASH_ATTR bool reconnect(time_t now) {
        if (!client.connected()) {
            if ((now - last_conn) <  MQTT_RECONNECT_TIME)
                return false;
            last_conn = now;

            DBG("(MQTT CONN)");

            char *user = nullptr;
            char *pass = nullptr;

            size_t unl = ::strnlen(config.mqtt_user, sizeof(config.mqtt_user));

            if ((unl > 0) && (unl < sizeof(config.mqtt_user))) {
                user = config.mqtt_user;
                pass = config.mqtt_pass;
            }

            if (!client.connect(config.mqtt_client_id, user, pass)) {
                ERR(MQTT_CANNOT_CONNECT);
                return false;
            } else {
                EVENT(MQTT_CONN);

                // subscribe to the set sub-branch
                PathBuffer pb;
                auto path = Path::compose_set_prefix_wildcard(pb);
                client.subscribe(path.c_str());

                // (re)announce all known clients to the HA autodiscovery
                for (uint8_t a = 1; a < MAX_HR_ADDR; ++a) {
                    auto *hr = master.model[a];
                    if (hr && hr->last_contact) publish_discovery(a);
                }
            }
        }

        return true;
    }

    enum StateMajor {
        STM_FREQ        = 0,
        STM_TIMER       = 1,
        STM_EEPROM      = 2,
        STM_NEXT_CLIENT = 3
    };

    ICACHE_FLASH_ATTR void update(time_t now) {
        // TODO: Try to reconnect in intervals. Don't block the main loop too
        // often
        if (!reconnect(now)) return;

        client.loop();

        // publish HA autodiscovery for a client we haven't announced yet
        if ((announced & (1u << addr)) == 0) {
            auto *hr = master.model[addr];
            if (hr && hr->last_contact) publish_discovery(addr);
        }

        // going stale is not an event - check it on every visit. Only the
        // offline direction lives here; "online" is published by the state
        // machine together with last_seen on every data burst
        if (addr) {
            auto *hr = master.model[addr];
            if (hr && hr->last_contact && avail_up.get(addr) &&
                (now - hr->last_contact) >= AVAIL_TIMEOUT_SECS)
            {
                avail_up.clear(addr);

                Path p{addr, mqtt::AVAIL};
                BufferHolder<8> b;
                StrMaker sm{b};
                sm += S_AVAIL_OFF;
                publish(p, sm.str());
            }
        }

        if (!states[addr]) {
            // no changes for this client
            // switch to next one and check here next loop
            next_client();
            return;
        }

        switch (state_maj) {
        case STM_FREQ:
            publish_frequent();
            break;
        case STM_TIMER:
            publish_timers();
            break;
        case STM_EEPROM:
            publish_eeprom();
            break;
        default:
            next_client();
        }
    }

    ICACHE_FLASH_ATTR void next_client() {
        // process one client per loop call (i.e. per second)
        ++addr;

        // wraparound
        if (addr >= MAX_HR_ADDR) addr = 0;

        // reset the major/minor state indicators
        state_maj = STM_FREQ;
        state_min = 0;
    }

    ICACHE_FLASH_ATTR void next_major() {
        state_min = 0;
        ++state_maj;
        if (state_maj >= STM_NEXT_CLIENT) next_client();
    }

    template <typename T, typename CvT>
    ICACHE_FLASH_ATTR void publish(const Str &path,
                                   CachedValue<T, CvT> &val,
                                   uint16_t hint,
                                   bool retain = MQTT_RETAIN) const
    {
        if (val.published() || !val.remote_valid())
            return;

        cvt::ValueBuffer vb;
        auto vstr = val.to_str(vb);

        if (client.publish(path.c_str(),
                           reinterpret_cast<const uint8_t *>(vstr.c_str()),
                           vstr.length(),
                           // retained
                           retain))
        {
            EVENT_ARG(MQTT_PUBLISH, hint);
        } else {
            ERR_ARG(MQTT_CANT_PUBLISH, hint);
        }

        val.published() = true;
    }

    /// publish for synced values - the per-topic source of truth is the
    /// requested value while a write to the device is pending (optimistic
    /// fiction, so MQTT clients react immediately), and the remote value
    /// once the device has reported - set_remote() clears published(), so
    /// the real value is emitted then, rolling the fiction back if the
    /// device did not take it (the request keeps retrying meanwhile)
    template <typename T, typename CvT>
    ICACHE_FLASH_ATTR void publish(const Str &path,
                                   SyncedValue<T, CvT> &val,
                                   uint16_t hint,
                                   bool retain = MQTT_RETAIN) const
    {
        if (val.published() || (!val.remote_valid() && !val.is_requested_set()))
            return;

        cvt::ValueBuffer vb;
        auto vstr =
            CvT::to_str(vb, val.is_requested_set() ? val.get_requested()
                                                   : val.get_remote());

        if (client.publish(path.c_str(),
                           reinterpret_cast<const uint8_t *>(vstr.c_str()),
                           vstr.length(),
                           // retained
                           retain))
        {
            EVENT_ARG(MQTT_PUBLISH, hint);
        } else {
            ERR_ARG(MQTT_CANT_PUBLISH, hint);
        }

        val.published() = true;
    }

    template <typename T, typename CvT>
    ICACHE_FLASH_ATTR void publish(const Path &p,
                                   CachedValue<T, CvT> &val) const
    {
        PathBuffer pb;
        auto path = p.compose(pb);
        publish(path.c_str(), val, p.as_uint());
    }

    ICACHE_FLASH_ATTR void publish(const Path &p,
                                   const Str &val,
                                   bool retain = MQTT_RETAIN) const
    {
        PathBuffer pb;
        auto path = p.compose(pb);

        if (client.publish(path.c_str(),
                           reinterpret_cast<const uint8_t *>(val.c_str()),
                           val.length(),
                           retain))
        {
            EVENT_ARG(MQTT_PUBLISH, p.as_uint());
        } else {
            ERR_ARG(MQTT_CANT_PUBLISH, p.as_uint());
        }
    }

    template <typename T, typename CvT>
    ICACHE_FLASH_ATTR void publish_synced(const Path &p,
                                          SyncedValue<T, CvT> &val) const
    {
        PathBuffer pb;
        auto path = p.compose(pb);

        publish(path.c_str(), val, p.as_uint());
    }

    ICACHE_FLASH_ATTR void publish_timer_slot(const Path &p, TimerSlot &val) const
    {
        PathBuffer pb;

        // clone paths and set the two possile endings for them
        Path mode_path{p};
        Path time_path{p};

        mode_path.timer_topic = mqtt::TIMER_MODE;
        time_path.timer_topic = mqtt::TIMER_TIME;

        if (!val.published() &&
            (val.remote_valid() || val.is_requested_set())) {
            // pending requests are the source of truth (optimistic)
            const auto &remote =
                val.is_requested_set() ? val.get_requested() : val.get_remote();

            // holds the converted value between to_str and publish
            cvt::ValueBuffer vb;
            auto mode = cvt::Simple::to_str(vb, remote.mode());
            auto path = mode_path.compose(pb);
            bool err =
                client.publish(path.c_str(),
                               reinterpret_cast<const uint8_t *>(mode.c_str()),
                               mode.length(),
                               /*retained*/ MQTT_RETAIN);

            path = time_path.compose(pb);
            // overwrites the old vb content!
            auto time = cvt::TimeHHMM::to_str(vb, remote.time());
            bool err1 =
                client.publish(path.c_str(),
                               reinterpret_cast<const uint8_t *>(time.c_str()),
                               time.length(),
                               /*retained*/ MQTT_RETAIN);


            if (!err || !err1) {
                ERR_ARG(MQTT_CANT_PUBLISH, p.as_uint());
            } else {
                EVENT_ARG(MQTT_PUBLISH, p.as_uint());
            }

            val.published() = true;
        }
    }

    /// publishes a retained discovery message, logs an error on failure
    ICACHE_FLASH_ATTR bool publish_raw(const Str &topic, const Str &payload) {
        if (!topic.c_str() || !payload.c_str()) {
            ERR(MQTT_HASS_OVERFLOW);
            return false;
        }

        if (client.publish(topic.c_str(),
                           reinterpret_cast<const uint8_t *>(payload.c_str()),
                           payload.length(),
                           /*retained*/ true))
        {
            EVENT(MQTT_PUBLISH);
            return true;
        }

        ERR(MQTT_CANT_PUBLISH);
        return false;
    }

    /// publishes the retained HA autodiscovery configs (climate entity and
    /// battery sensor) for one client and marks it as announced
    ICACHE_FLASH_ATTR void publish_discovery(uint8_t addr) {
        static hass::ConfigBuffer cb;
        PathBuffer tb;

        const char *pfx = config.mqtt_topic_prefix;

        // user-defined name (if any), else "HR20 <addr>"
        char nm[THERMO_NAME_MAX + 1];
        const char *un = config.thermo_name(addr);
        if (un) {
            // parser guarantees nlen <= THERMO_NAME_MAX and NUL termination
            strcpy(nm, un);
        } else {
            memcpy(nm, "HR20 ", 5);
            nm[5] = '0' + addr / 10;
            nm[6] = '0' + addr % 10;
            nm[7] = 0;
        }

        auto topic = hass::compose(hass::T_CLIMATE_TOPIC, addr, pfx, "", "", tb);
        auto conf  = hass::compose(hass::T_CLIMATE_CONF,  addr, pfx, "", nm, cb);
        bool ok = publish_raw(topic, conf);

        // the battery sensor gets the same name plus a suffix
        char snm[THERMO_NAME_MAX + 9];
        unsigned l = strlen(nm);
        memcpy(snm, nm, l);
        memcpy(snm + l, " battery", 9); // NUL included

        topic = hass::compose(hass::T_SENSOR_TOPIC, addr, pfx, "", "", tb);
        conf  = hass::compose(hass::T_SENSOR_CONF,  addr, pfx, snm, nm, cb);
        ok = publish_raw(topic, conf) && ok;

        if (ok) announced |= 1u << addr;
    }

    ICACHE_FLASH_ATTR void publish_eeprom() {
        if ((states[addr] & CHANGE_EEPROM) == 0) {
            // no changes. advance...
            next_major();
            return;
        }

#ifdef VERBOSE
        // TOO VERBOSE
        DBG("(ME %u)", addr);
#endif
        auto *hr = master.model[addr];
        if (!hr) {
            ERR(MQTT_INVALID_CLIENT);
            return;
        }

        // playloads of 16 addresses
        for (unsigned cnt = 0; cnt < 16; ++cnt) {
            if (state_min >= EEPROM_SIZE) {
                DBG("(PUB E)");
                // whatever happens, we transition to next state
                states[addr] &= ~CHANGE_EEPROM;
                next_major(); // moves to next major state
                break;
            }

            Path p{addr, false, mqtt::EA_READ, state_min};

            auto &rec = hr->eeprom[state_min];

            // only publish remote-valid values
            publish(p, rec);
            ++state_min;
        }
    }


    ICACHE_FLASH_ATTR void publish_frequent() {
        if ((states[addr] & CHANGE_FREQUENT) == 0) {
            // no changes. advance...
            next_major();
            return;
        }

#ifdef VERBOSE
        // TOO VERBOSE
        DBG("(MF %u)", addr);
#endif

        auto *hr = master.model[addr];
        if (!hr) {
            ERR(MQTT_INVALID_CLIENT);
            return;
        }

        Path p{addr, mqtt::INVALID_TOPIC};

#define STATE(ST) case ST
#define NEXT_MIN_STATE ++state_min; break;

        switch (state_min) {
        STATE(0):
            p.topic = mqtt::AUTO;
            publish_synced(p, hr->auto_mode);
            NEXT_MIN_STATE;
        STATE(1):
            p.topic = mqtt::LOCK;
            publish_synced(p, hr->menu_locked);
            NEXT_MIN_STATE;
        STATE(2):
            p.topic = mqtt::WND;
            publish(p, hr->mode_window);
            NEXT_MIN_STATE;
        STATE(3):
            // TODO: this is in 0.01 of C, change it to float?
            p.topic = mqtt::CUR_TMP;
            publish(p, hr->temp_avg);
            NEXT_MIN_STATE;
        STATE(4):
            // TODO: Battery is in 0.01 of V, change it to float?
            p.topic = mqtt::BAT;
            publish(p, hr->bat_avg);
            NEXT_MIN_STATE;
        STATE(5):
            // TODO: Fix formatting for temp_wanted - float?
            // temp_wanted is in 0.5 C
            p.topic = mqtt::TMP;
            publish_synced(p, hr->temp_wanted);
            NEXT_MIN_STATE;
        STATE(6):
            p.topic = mqtt::VALVE_WTD;
            publish(p, hr->cur_valve_wtd);
            NEXT_MIN_STATE;
        // TODO: test_auto
        STATE(7):
            p.topic = mqtt::ERR;
            publish(p, hr->ctl_err);
            NEXT_MIN_STATE;
        STATE(8): {
            p.topic = mqtt::LAST_SEEN;
            cvt::ValueBuffer vb;
            StrMaker sm{vb};
            sm += hr->last_contact;
            publish(p, sm.str(), /*ratain*/true);

            // fresh data means the device is available - publish on the
            // offline->online transition only
            if (!avail_up.get(addr)) {
                avail_up.set(addr);

                p.topic = mqtt::AVAIL;
                BufferHolder<8> b;
                StrMaker sm2{b};
                sm2 += S_AVAIL_ON;
                publish(p, sm2.str());
            }
            NEXT_MIN_STATE;
        }
        STATE(9): {
            p.topic = mqtt::MODE;
            cvt::ValueBuffer vb;
            StrMaker sm{vb};

            // Combining off/open and auto/heat(manual) for HA climate.
            // heat is what HA knows, manual is not a HA climate mode.
            // Pending requests are the source of truth (optimistic).
            uint8_t tw = hr->temp_wanted.is_requested_set()
                             ? hr->temp_wanted.get_requested()
                             : hr->temp_wanted.get_remote();
            bool am = hr->auto_mode.is_requested_set()
                          ? hr->auto_mode.get_requested()
                          : hr->auto_mode.get_remote();

            if (tw == TEMP_OFF)
                sm += S_MODE_OFF;
            else if (tw == TEMP_OPEN)
                sm += S_MODE_OPEN;
            else if (am)
                sm += S_MODE_AUTO;
            else
                sm += S_MODE_HEAT;

            publish(p, sm.str());
            NEXT_MIN_STATE;
        }
        STATE(10):
            // battery charge - 2x AA rechargeable: 2.0V (0%) .. 2.8V (100%)
            if (hr->bat_avg.remote_valid()) {
                p.topic = mqtt::BATT_PCT;
                cvt::ValueBuffer vb;
                StrMaker sm{vb};
                int pct = (int)hr->bat_avg.get_remote() - 2000;
                pct = pct * 100 / 800;
                if (pct < 0) pct = 0;
                if (pct > 100) pct = 100;
                sm += pct;
                publish(p, sm.str());
            }
            NEXT_MIN_STATE;
#ifdef MQTT_JSON
        STATE(11): {
            p.topic = mqtt::STATE;
            BufferHolder<160> buf;
            StrMaker sm{buf};
            json::append_client_attr(sm, *hr);
            publish(p, sm.str());
            NEXT_MIN_STATE;
        }
#endif

        default:
            DBG("(PUB F)");
            // clear out the change bit
            states[addr] &= ~CHANGE_FREQUENT;
            next_major(); // moves to next major state
            break;
        }
#undef STATE
#undef NEXT_MIN_STATE
    }

    ICACHE_FLASH_ATTR void publish_timers() {
        auto *hr = master.model[addr];
        if (!hr) {
            ERR(MQTT_INVALID_CLIENT);
            return;
        }

        if ((states[addr] & CHANGE_TIMER_MASK) == 0) {
            next_major();
            return;
        }

        // the minor state encodes day/slot
        uint8_t day  = state_min >> 3;
        uint8_t slot = state_min  & 0x7;
        ++state_min;

        uint8_t mask = change_get_timer_mask(states[addr]);

        // if we overshot the day counter, we switch to next
        // major slot (or client in this case)
        if (day >= 8) {
            DBG("(PUB T)");
            next_major();
            return;
        }

        // the current day/slot is not changed?
        // visit the next day/slot next time
        if (!((1 << day) & mask)) {
            return;
        }

        // clear out the mask bit for the particular day if the last slot is hit
        if (slot == TIMER_SLOTS_PER_DAY - 1) states[addr] &= ~timer_day_2_change[day];

#ifdef VERBOSE
        // TOO VERBOSE
        DBG("(MT %u %u %u)", addr, day, slot);
#endif
        // only publish timers that have bit set in mask
        Path p{addr, mqtt::TIMER, false, mqtt::TIMER_NONE, day, slot};

        // TODO: Rework this to implicit conversion system
        publish_timer_slot(p, hr->timers[day][slot]);
    }

    ICACHE_FLASH_ATTR void callback(char *topic, byte *payload,
                                    unsigned int length)
    {
        // only allowed on some endpoints. will switch through
        Path p = Path::parse(topic);

        if (!p.valid()) {
            ERR(MQTT_INVALID_TOPIC);
            return;
        }

        if (!p.set_mode) {
            ERR(MQTT_INVALID_TOPIC);
            return;
        }

        auto *hr = master.model[p.addr];
        if (!hr) {
            ERR(MQTT_CALLBACK_BAD_ADDR);
            return;
        }

        bool ok = true; // false indicates an invalid value was encountered
        // Arduino string does NOT have char*+len concat/ctor...
        Str val{(const char *)payload, length};

        switch (p.topic) {
        case mqtt::TMP: ok = hr->temp_wanted.set_requested_from_str(val); break;
        case mqtt::AUTO: ok = hr->auto_mode.set_requested_from_str(val); break;
        case mqtt::MODE: {
            // off will set temp to TEMP_OFF
            auto mode = parse_mode(val);
            bool restore = false;

            switch (mode) {
            case MODE_OFF: // off sets manual and 4.5 degrees
                remember_setpoint(hr);
                hr->auto_mode.set_requested(false);
                hr->temp_wanted.set_requested(TEMP_OFF);
                ok = true;
                break;
            case MODE_OPEN: // open sets over 30C and manual
                remember_setpoint(hr);
                hr->auto_mode.set_requested(false);
                hr->temp_wanted.set_requested(TEMP_OPEN);
                ok = true;
                break;
            case MODE_AUTO:
                ok = true;
                hr->auto_mode.set_requested(true);
                restore = true;
                break;
            case MODE_MANUAL: // also the target of the HA "heat" mode
                ok = true;
                hr->auto_mode.set_requested(false);
                restore = true;
                break;
            default:
                ERR(MQTT_INVALID_TOPIC_VALUE);
                ok = false;
            }

            if (restore) restore_setpoint(hr);
            break;
        }
        case mqtt::LOCK: ok = hr->menu_locked.set_requested_from_str(val); break;
        case mqtt::EEPROM: {
            //            ok = hr->eeprom;
            // we're in set mode here. for reads we invalidate the remote and let it be read again
            if (p.eeprom_access == EA_WRITE) {
                int ival = 0;
                if (!val.toInt(ival)) {
                    ERR(MQTT_INVALID_TOPIC_VALUE);
                    ok = false;
                    break;
                }
                hr->eeprom[p.eeprom_address].set_requested(ival);
            } else if (p.eeprom_access == EA_READ) {
                // we got a re-read request, we do it without questioning
                // we unmask the value in case it was not seen since reboot
                // and also invalidate remote in case it was already read...
                hr->eeprom[p.eeprom_address].masked() = false;
                hr->eeprom[p.eeprom_address].remote_valid() = false;
            } else {
                ERR(MQTT_INVALID_TOPIC);
                ok = false;
            }

            break;
        }
        case mqtt::TIMER: {
            // check day/slot first
            if (p.day >= TIMER_DAYS) {
                ERR_ARG(MQTT_INVALID_TIMER_TOPIC, p.day | 0x10);
                break;
            }

            if (p.slot >= TIMER_SLOTS_PER_DAY) {
                ERR_ARG(MQTT_INVALID_TIMER_TOPIC, p.slot | 0x20);
                break;
            }

            // subswitch based on the timer topic
            switch (p.timer_topic) {
            case mqtt::TIMER_MODE: ok = hr->set_timer_mode(p.day, p.slot, val); break;
            case mqtt::TIMER_TIME: ok = hr->set_timer_time(p.day, p.slot, val); break;
            default: ERR(MQTT_INVALID_TIMER_TOPIC);
            }
            break;
        }
        default: ERR(MQTT_INVALID_TOPIC); return;
        }

        // not an error. change happened on the client model, sync is lost
        if (ok) {
            hr->synced = false;
            // run the publish state machine on the next pass - the pending
            // request is now the source of truth for its topic
            states[p.addr] |= CHANGE_FREQUENT;
        }

        EVENT_ARG(MQTT_CALLBACK, p.as_uint());
        DBG("(MQTT %d %d %d)", p.addr, p.as_uint(), ok ? 1 : 0);

        // conversion went sideways
        if (!ok) {
            ERR_ARG(MQTT_INVALID_TOPIC_VALUE, p.as_uint());
#ifdef VERBOSE
            DBG("(MQ ERR %d %d %s)", p.addr, p.topic, val.c_str());
#endif
        }

#ifdef VERBOSE
        DBG("(MQC %d %d)", p.addr, p.topic);
#endif
    }

    Config &config;
    HR20Master &master;
    WiFiClient wifiClient;
    /// seriously, const correctness anyone? PubSubClient does not have single const method...
    mutable PubSubClient client;
    uint32_t states[MAX_HR_ADDR];

    // HA autodiscovery - bit per already announced client address
    uint32_t announced = 0;

    // device availability - clients currently reported online. "online"
    // is published by the state machine with last_seen (fresh data),
    // "offline" by the stale check in update()
    AddrFlags avail_up;

    // Publisher state machine
    uint8_t addr = 0;
    uint8_t  state_maj = 0; // state category (FREQUENT, CALENDAR)
    uint16_t state_min = 0; // state detail (depends on major state)
    time_t   last_conn = 0; // last connection attempt
};

} // namespace mqtt
} // namespace hr20
