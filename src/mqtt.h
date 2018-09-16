#pragma once

#include <PubSubClient.h>

namespace mqtt {
// topic enum. Each has different initial letter for simple parsing
enum Topic {
    AVG_TMP,
    BAT,
    ERR,
    LOCK,
    MODE,
    REQ_TMP,
    VALVE_WTD,
    WND,
    INVALID_TOPIC = 255
};

static const char *S_AVG_TMP   = "average_temp";
static const char *S_BAT       = "battery";
static const char *S_ERR       = "error";
static const char *S_LOCK      = "lock";
static const char *S_MODE      = "mode";
static const char *S_REQ_TMP   = "requested_temp"; // 14
static const char *S_VALVE_WTD = "valve_wanted";
static const char *S_WND       = "window";

ICACHE_FLASH_ATTR static const char *topicStr(Topic topic) {
    switch (topic) {
    case AVG_TMP:   return S_AVG_TMP;
    case BAT:       return S_BAT;
    case ERR:       return S_ERR;
    case LOCK:      return S_LOCK;
    case MODE:      return S_MODE;
    case REQ_TMP:   return S_REQ_TMP;
    case VALVE_WTD: return S_VALVE_WTD;
    case WND:       return S_WND;
    default:
        return "invalid!";
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
        return INVALID_TOPIC;
    case 'm':
        if (strcmp(top, S_MODE) == 0) return MODE;
        return INVALID_TOPIC;
    case 'r':
        if (strcmp(top, S_REQ_TMP) == 0) return REQ_TMP;
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

//
struct Path {
    static const char SEPARATOR = '/';
    static const constexpr char *prefix = "hr20";


    ICACHE_FLASH_ATTR Path() {}
    ICACHE_FLASH_ATTR Path(uint8_t addr, Topic t) : addr(addr), topic(t) {}

    // target has to be able to hold prefix len + / + addr(2 bytes) + '/' + 14
    // UGLY INEFFECTIVE STRING APPEND FOLLOWS
    ICACHE_FLASH_ATTR String compose() const {
        String rv = prefix;
        rv += SEPARATOR;
        rv += addr;
        rv += SEPARATOR;
        rv += topicStr(topic);
        return rv;
    }

    ICACHE_FLASH_ATTR static Path parse(const char *p) {
        // compare prefix first
        const char *pos = p;

        // is the first token same as prefix?
        pos = cmp_token(p, prefix);

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
    Topic topic  = INVALID_TOPIC;
};

} // namespace mqtt
