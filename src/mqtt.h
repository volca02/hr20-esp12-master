#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>

#include "config.h"
#include "ntptime.h"
#include "master.h"
#include "util.h"
#include "wifi.h"

namespace mqtt {

// topic enum. Each has different initial letter for simple parsing
enum Topic {
    AVG_TMP = 1,
    BAT,
    ERR,
    LOCK,
    MODE,
    REQ_TMP,
    VALVE_WTD,
    WND,
    LAST_SEEN,
    TIMER,
    INVALID_TOPIC = 255
};

enum TimerTopic {
    TIMER_NONE = 0,
    TIMER_TIME,
    TIMER_MODE,
    INVALID_TIMER_TOPIC = 255
};

static const char *S_AVG_TMP   = "average_temp";
static const char *S_BAT       = "battery";
static const char *S_ERR       = "error";
static const char *S_LOCK      = "lock";
static const char *S_MODE      = "mode";
static const char *S_REQ_TMP   = "requested_temp"; // 14
static const char *S_VALVE_WTD = "valve_wanted";
static const char *S_WND       = "window";
static const char *S_LAST_SEEN = "last_seen";

static const char *S_TIMER     = "timer";
static unsigned    S_TIMER_LEN = 5;

// timer subtopics
static const char *S_TIMER_MODE = "mode";
static const char *S_TIMER_TIME = "time";

ICACHE_FLASH_ATTR static const char *topic_str(Topic topic) {
    switch (topic) {
    case AVG_TMP:   return S_AVG_TMP;
    case BAT:       return S_BAT;
    case ERR:       return S_ERR;
    case LOCK:      return S_LOCK;
    case MODE:      return S_MODE;
    case REQ_TMP:   return S_REQ_TMP;
    case VALVE_WTD: return S_VALVE_WTD;
    case WND:       return S_WND;
    case LAST_SEEN: return S_LAST_SEEN;
    case TIMER:     return S_TIMER;
    default:
        return "invalid!";
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
        if (strcmp(top, S_AVG_TMP) == 0) return AVG_TMP;
        return INVALID_TOPIC;
    case 'b':
        if (strcmp(top, S_BAT) == 0) return BAT;
        return INVALID_TOPIC;
    case 'e':
        if (strcmp(top, S_ERR) == 0) return ERR;
        return INVALID_TOPIC;
    case 'l':
        if (strcmp(top, S_LOCK) == 0) return LOCK;
        if (strcmp(top, S_LAST_SEEN) == 0) return LAST_SEEN;
        return INVALID_TOPIC;
    case 'm':
        if (strcmp(top, S_MODE) == 0) return MODE;
        return INVALID_TOPIC;
    case 'r':
        if (strcmp(top, S_REQ_TMP) == 0) return REQ_TMP;
        return INVALID_TOPIC;
    case 't':
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

// mqtt path parser/composer
struct Path {
    static const char SEPARATOR = '/';
    static const char *prefix;

    // static method that overrides prefix
    ICACHE_FLASH_ATTR static void begin(const char *pfx) {
        prefix = pfx;
    }

    ICACHE_FLASH_ATTR Path() {}
    ICACHE_FLASH_ATTR Path(uint8_t addr, Topic t, TimerTopic st = TIMER_NONE,
                           uint8_t day = 0, uint8_t slot = 0)
        : addr(addr), day(day), slot(slot), topic(t), timer_topic(st)
    {}

    // UGLY INEFFECTIVE STRING APPEND FOLLOWS
    ICACHE_FLASH_ATTR String compose() const {
        String rv;
        rv += SEPARATOR;
        rv += prefix;
        rv += SEPARATOR;
        rv += addr;
        rv += SEPARATOR;
        rv += topic_str(topic);

        if (topic == TIMER) {
            rv += SEPARATOR;
            rv += day;
            rv += SEPARATOR;
            rv += slot;
            rv += SEPARATOR;
            rv += timer_topic_str(timer_topic);
        }

        return rv;
    }

    ICACHE_FLASH_ATTR static Path parse(const char *p) {
        // compare prefix first
        const char *pos = p;

        // skip initial SEPARATOR if present
        if (*pos == SEPARATOR) ++pos;

        // is the first token same as prefix?
        pos = cmp_token(pos, prefix);

        // not prefix!
        if (!pos) return {};

        // premature end
        if (!*pos) return {};

        // slash is skipped
        ++pos;

        // tokenize the address
        auto addr = token(pos);

        // convert to number
        uint8_t address = to_num(&pos, addr.second);

        // is the next char a separator? if not then it wasn't a valid path
        if (*pos != Path::SEPARATOR) return {};

        ++pos;

        // now follows the ending element. Parse via parse_topic
        Topic top = parse_topic(pos);

        if (top == INVALID_TOPIC) return {};

        if (top == TIMER) {
            // skip the timer topic
            pos = token(pos).first;

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
            return {address, top, tt, d, s};
        }

        return {address, top};
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

    ICACHE_FLASH_ATTR static const char *cmp_token(const char *p, const char *tok) {
        auto t = token(p);
        return (strncmp(p, tok, t.second) == 0) ? p + t.second : nullptr;
    }

    // returns separator pos (or zero byte) and number of bytes that it took to
    // get there
    ICACHE_FLASH_ATTR static std::pair<const char*, uint8_t> token(const char *p) {
        const char *pos = p;
        for(;*p;++p) {
            if (*p == Path::SEPARATOR) {
                break;
            }
        }

        return {p, p - pos};
    }

    ICACHE_FLASH_ATTR bool valid() { return addr != 0; }

    // client ID. 0 means invalid path!
    uint8_t addr = 0;
    uint8_t day  = 0;
    uint8_t slot = 0;
    Topic topic            = INVALID_TOPIC;
    TimerTopic timer_topic = TIMER_NONE;
};

/// Publishes/receives topics in mqtt
struct MQTTPublisher {
    // TODO: Make this configurable!
    ICACHE_FLASH_ATTR MQTTPublisher(Config &config, ntptime::NTPTime &time,
                                    HR20Master &master)
        : config(config), time(time), master(master), wifiClient(),
          client(wifiClient)
    {
      for (uint8_t i = 0; i < MAX_HR_COUNT; ++i)
        states[i] = 0;
    }

    ICACHE_FLASH_ATTR void begin() {
        client.setServer(config.mqtt_server, config.mqtt_port);
        client.setCallback([&](char *topic, byte *payload, unsigned int length)
                           {
                               callback(topic, payload, length);
                           });
        // every change gets a bitmask info here
        master.proto.set_callback([&](uint8_t addr, uint32_t mask) {
                                      states[addr] |= mask;
                                  });
    }

    ICACHE_FLASH_ATTR bool reconnect() {
        if (!client.connected()) {
            DBG("(MQTT CONN)");
            // TODO: only try this once a few seconds or so
            // just store last time we did it and retry
            // if we get over retry time
            if (!client.connect(config.mqtt_client_id)) {
                ERR("MQTT conn. err.");
                return false;
            }
        }
        return true;
    }

    enum StateMajor {
        STM_FREQ        = 0,
        STM_TIMER       = 1,
        STM_NEXT_CLIENT = 2
    };

    ICACHE_FLASH_ATTR void update() {
        client.loop();

        // TODO: Try to reconnect in intervals. Don't block the main loop too
        // often
        if (!reconnect()) return;

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
        default:
            next_client();
        }
    }

    ICACHE_FLASH_ATTR void next_client() {
        // process one client per loop call (i.e. per second)
        ++addr;

        // wraparound
        if (addr >= MAX_HR_COUNT) addr = 0;

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
    ICACHE_FLASH_ATTR void publish_subscribe(const Path &p,
                                             CachedValue<T, CvT> &val) const
    {
        String path = p.compose();
        if (!val.published() && val.remote_valid()) {
            client.publish(path.c_str(), val.to_str().c_str());
            val.published() = true;
        }

        if (!val.subscribed()) {
            client.subscribe(path.c_str());
            val.subscribed() = true;
        }
    }

    // timer publishes and subscribes two topics, so we overload for it...
    ICACHE_FLASH_ATTR void publish_subscribe(const Path &p, CachedValue<Timer> &val) const
    {
        // clone paths and set the two possile endings for them
        Path mode_path{p};
        Path time_path{p};

        mode_path.timer_topic = mqtt::TIMER_MODE;
        time_path.timer_topic = mqtt::TIMER_TIME;

        // format the paths to strings
        String mode_p_str = mode_path.compose();
        String time_p_str = time_path.compose();

        if (!val.published() && val.remote_valid()) {
            client.publish(mode_p_str.c_str(), cvt::Simple::to_str(val.get_remote().mode()).c_str());
            client.publish(time_p_str.c_str(), cvt::TimeHHMM::to_str(val.get_remote().time()).c_str());
            val.published() = true;
        }

        if (!val.subscribed()) {
            client.subscribe(mode_p_str.c_str());
            client.subscribe(time_p_str.c_str());
            val.subscribed() = true;
        }
    }

    template <typename T, typename CvT>
    ICACHE_FLASH_ATTR void publish(const Path &p,
                                   CachedValue<T, CvT> &val) const
    {
        if (val.published())
            return;

        String path = p.compose();
        client.publish(path.c_str(), val.to_str().c_str());
        val.published() = true;
    }

    template <typename T>
    ICACHE_FLASH_ATTR void publish_subscribe(const Path &p, const T &val,
                                             bool is_published,
                                             bool is_subscribed) const
    {
        String sv(val);
        String path = p.compose();
        if (!is_published)  client.publish(path.c_str(), sv.c_str());
        if (!is_subscribed) client.subscribe(path.c_str());
    }

    template<typename T>
    ICACHE_FLASH_ATTR void publish(const Path &p, const T &val) const {
        String sv(val);
        String path = p.compose();
        client.publish(path.c_str(), sv.c_str());
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
            ERR("Change on invalid client");
            return;
        }

        Path p{addr, mqtt::INVALID_TOPIC};

        switch (state_min) {
        case 0:
            p.topic = mqtt::MODE;
            publish_subscribe(p, hr->auto_mode);
            ++state_min;
            break;
        case 1:
            p.topic = mqtt::LOCK;
            publish_subscribe(p, hr->menu_locked);
            ++state_min;
            break;
        case 2:
            p.topic = mqtt::WND;
            publish(p, hr->mode_window);
            ++state_min;
            break;
        case 3:
            // TODO: this is in 0.01 of C, change it to float?
            p.topic = mqtt::AVG_TMP;
            publish(p, hr->temp_avg);
            ++state_min;
            break;
        case 4:
            // TODO: Battery is in 0.01 of V, change it to float?
            p.topic = mqtt::BAT;
            publish(p, hr->bat_avg);
            ++state_min;
            break;
        case 5:
            // TODO: Fix formatting for temp_wanted - float?
            // temp_wanted is in 0.5 C
            p.topic = mqtt::REQ_TMP;
            publish_subscribe(p, hr->temp_wanted);
            ++state_min;
            break;
        case 6:
            p.topic = mqtt::VALVE_WTD;
            publish(p, hr->cur_valve_wtd);
            ++state_min;
            break;
        // TODO: test_auto
        case 7:
            p.topic = mqtt::ERR;
            publish(p, hr->ctl_err);
            ++state_min;
            break;
        case 8:
            p.topic = mqtt::LAST_SEEN;
            publish(p, hr->last_contact);
            // fallthrough!
        case 9:
            // clear out the change bit
            states[addr] &= ~CHANGE_FREQUENT;
            next_major(); // moves to next major state
            break;
        }
    }

    ICACHE_FLASH_ATTR void publish_timers() {
        auto *hr = master.model[addr];
        if (!hr) {
            ERR("Change on invalid client");
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
            next_major();
            return;
        }

        // the current day/slot is not changed?
        // visit the next day/slot next time
        if (!((1 << day) & mask)) {
            return;
        }

        // clear out the mask bit for the particular day if the last slot is hit
        if (slot == 7) states[addr] &= ~timer_day_2_change[day];

#ifdef VERBOSE
        // TOO VERBOSE
        DBG("(MT %u %u %u)", addr, day, slot);
#endif
        // only publish timers that have bit set in mask
        Path p{addr, mqtt::TIMER, mqtt::TIMER_NONE, day, slot};

        // TODO: Rework this to implicit conversion system
        publish_subscribe(p, hr->timers[day][slot]);
    }

    ICACHE_FLASH_ATTR void callback(char *topic, byte *payload,
                                    unsigned int length)
    {
        // only allowed on some endpoints. will switch through
        Path p = Path::parse(topic);

        if (!p.valid()) {
            ERR("Invalid topic %s", topic);
            return;
        }

        auto *hr = master.model[p.addr];
        if (!hr) {
            ERR("Callback to set on invalid client");
            return;
        }

        const char *val = reinterpret_cast<const char*>(payload);
        switch (p.topic) {
        case mqtt::REQ_TMP: hr->temp_wanted.set_requested_from_str(val); break;
        case mqtt::MODE: hr->auto_mode.set_requested_from_str(val); break;
        case mqtt::LOCK: hr->menu_locked.set_requested_from_str(val); break;
        case mqtt::TIMER: {
            // subswitch based on the timer topic
            switch (p.timer_topic) {
            case mqtt::TIMER_MODE: hr->set_timer_mode(p.day, p.slot, val); break;
            case mqtt::TIMER_TIME: hr->set_timer_time(p.day, p.slot, val); break;
            default: ERR("Non-settable timer topic change requested: %s", topic);
            }
            break;
        }
        default: ERR("Non-settable topic change requested: %s", topic);
        }

#ifdef VERBOSE
        DBG("(MQC %d %d)", p.addr, p.topic);
#endif
    }

    Config &config;
    ntptime::NTPTime &time;
    HR20Master &master;
    WiFiClient wifiClient;
    /// seriously, const correctness anyone? PubSubClient does not have single const method...
    mutable PubSubClient client;
    uint32_t states[MAX_HR_COUNT];

    // Publisher state machine
    uint8_t addr = 0;
    uint8_t state_maj = 0; // state category (FREQUENT, CALENDAR)
    uint8_t state_min = 0; // state detail (depends on major state)
};

} // namespace mqtt
