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

namespace hr20 {
namespace cvt {

// should be enough for all the values we use here (long int is ~20 chars)
using ValueBuffer = BufferHolder<32>;

// Simple integer converter
struct Simple {
    ICACHE_FLASH_ATTR static Str to_str(Buffer buf, uint8_t val) {
        StrMaker sm{buf};
        sm += val;
        return sm.str();
    }

    ICACHE_FLASH_ATTR static Str to_str(Buffer buf, bool val) {
        const char * rvbuf = val ? "true" : "false";
        // construct an immutable string directly from flash mem.
        return {rvbuf, strlen(rvbuf)};
    }

    ICACHE_FLASH_ATTR static Str to_str(Buffer buf, uint16_t val) {
        StrMaker sm{buf};
        sm += val;
        return sm.str();
    }

    ICACHE_FLASH_ATTR static Str to_str(Buffer buf, int val) {
        StrMaker sm{buf};
        sm += val;
        return sm.str();
    }

    ICACHE_FLASH_ATTR static Str to_str(Buffer buf, unsigned val) {
        StrMaker sm{buf};
        sm += (int)val;
        return sm.str();
    }

    ICACHE_FLASH_ATTR static Str to_str(Buffer buf, time_t val) {
        StrMaker sm{buf};
        sm += (int)val;
        return sm.str();
    }

    ICACHE_FLASH_ATTR static bool from_str(const Str &dta, uint8_t &tgt) {
        return dta.toInt(tgt);
    }


    ICACHE_FLASH_ATTR static bool from_str(const Str &dta, uint16_t &tgt) {
        return dta.toInt(tgt);
    }

    ICACHE_FLASH_ATTR static bool from_str(const Str &dta, bool &tgt) {
        if (dta.equalsIgnoreCase("true")
            || dta.equalsIgnoreCase("on"))
        {
            tgt = true;
            return true;
        }

        if (dta.equalsIgnoreCase("false")
            || dta.equalsIgnoreCase("off"))
        {
            tgt = false;
            return true;
        }

        // integer
        int val;
        if (dta.toInt(val)) {
            tgt = val != 0;
            return true;
        }
        return false;
    }

};


/** converts to/from string for 0.5 degree temperature format
 *  Format: [X].H - H being 0 or 5, X between 5 and 30
 */
struct TempHalfC {
    ICACHE_FLASH_ATTR static Str to_str(Buffer buf, uint8_t temp) {
        StrMaker sm{buf};
        sm.append(float(temp) / 2, 1);
        return sm.str();
    }

    ICACHE_FLASH_ATTR static bool from_str(const Str &val, uint8_t &tgt) {
        float f;
        if (val.toFloat(f)) {
            tgt = f * 2;
            return true;
        }
        return false;
    }
};

/** converts to string for 0.01 degree temperature format
 *  Format: X.XX
 */
struct Temp001C {
    ICACHE_FLASH_ATTR static Str to_str(Buffer buf, uint16_t temp) {
        StrMaker sm{buf};
        sm.append(float(temp) / 100, 2);
        return sm.str();
    }
};

/** converts to string for 0.001 voltage format
 *  Format: X.XXX
 */
struct Voltage0001V {
    ICACHE_FLASH_ATTR static Str to_str(Buffer buf, uint16_t volt) {
        StrMaker sm{buf};
        sm.append(float(volt) / 1000, 3);
        return sm.str();
    }
};

/** converts to/from string in HH:MM format
 */
struct TimeHHMM {
    ICACHE_FLASH_ATTR static Str to_str(Buffer buf, uint16_t time) {
        StrMaker sm{buf};

        sm += time / 60;
        sm += ':';
        uint8_t min = time % 60;

        if (min < 10)
            sm += '0';

        sm += min;
        return sm.str();
    }

    ICACHE_FLASH_ATTR static bool from_str(const Str &val, uint16_t &tgt) {
        auto colon_pos = val.indexOf(':');

        // allow for minute-only linear counter here too
        if (colon_pos < 0) {
            return Simple::from_str(val, tgt);
        }

        // valid colon position?
        if (colon_pos >= 0 && ((unsigned)colon_pos < (val.length() - 1))) {
            uint16_t hr, min;
            if (val.substring(0, colon_pos).toInt(hr) &&
                val.substring(colon_pos + 1).toInt(min))
            {
                tgt = hr * 60 + min;
                return true;
            }
        }

        return false;
    }
};

} // namespace cvt
} // namespace hr20
