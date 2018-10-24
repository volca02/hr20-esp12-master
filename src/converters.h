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
#include <string>

namespace cvt {

char ICACHE_FLASH_ATTR int2hex(uint8_t v);
int8_t ICACHE_FLASH_ATTR hex2int(char ch);

// Simple integer converter
struct Simple {
    ICACHE_FLASH_ATTR static String to_str(uint8_t val) {
        return String{(int)val};
    }

    ICACHE_FLASH_ATTR static bool from_str(const String &dta, uint8_t &tgt) {
        tgt = dta.toInt();
        return tgt != 0 || dta == "0";
    }

    ICACHE_FLASH_ATTR static String to_str(bool val) {
        return val ? "true" : "false";
    }

    ICACHE_FLASH_ATTR static String to_str(uint16_t val) {
        return String{val};
    }

    ICACHE_FLASH_ATTR static String to_str(int val) {
        return String{val};
    }

    ICACHE_FLASH_ATTR static String to_str(unsigned val) {
        return String{val};
    }

    ICACHE_FLASH_ATTR static String to_str(time_t val) {
        return String{val};
    }

    ICACHE_FLASH_ATTR static bool from_str(const String &dta, uint16_t &tgt) {
        tgt = dta.toInt();
        return tgt != 0 || dta == "0";
    }


    ICACHE_FLASH_ATTR static bool from_str(const String &dta, bool &tgt) {
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
        tgt = dta.toInt() != 0;
        return tgt || dta == "0";
    }

};


/** converts to/from string for 0.5 degree temperature format
 *  Format: [X].H - H being 0 or 5, X between 5 and 30
 */
struct TempHalfC {
    ICACHE_FLASH_ATTR static String to_str(uint8_t temp) {
        return String{float(temp) / 2, 1}; // 1 == decimal places
    }

    ICACHE_FLASH_ATTR static bool from_str(const String &val, uint8_t &tgt) {
        tgt = val.toFloat() * 2;
        return tgt != 0; // zero is not allowed in the value range anyway...
    }
};

/** converts to string for 0.01 degree temperature format
 *  Format: X.XX
 */
struct Temp001C {
    ICACHE_FLASH_ATTR static String to_str(uint16_t temp) {
        return String{float(temp) / 100, 2}; // 2 == decimal places
    }
};

/** converts to string for 0.001 voltage format
 *  Format: X.XXX
 */
struct Voltage0001V {
    ICACHE_FLASH_ATTR static String to_str(uint16_t temp) {
        return String{float(temp) / 1000, 3}; // 3 == decimal places
    }
};

/** converts to/from string in HH:MM format
 */
struct TimeHHMM {
    ICACHE_FLASH_ATTR static String to_str(uint16_t time) {
        String tm{time / 60};

        tm += ':';

        uint8_t min = time % 60;

        if (min < 10)
            tm += '0';

        tm += min;
        return tm;
    }

    ICACHE_FLASH_ATTR static bool from_str(const String &val, uint16_t &tgt) {
        auto colon_pos = val.indexOf(':');

        // allow for minute-only linear counter here too
        if (colon_pos < 0) {
            return Simple::from_str(val, tgt);
        }

        if (colon_pos >= 0 && ((unsigned)colon_pos < (val.length() - 1))) {
            tgt = val.substring(0, colon_pos).toInt() * 60 +
                val.substring(colon_pos + 1).toInt();
            return true;
        }
        return false;
    }
};

} // namespace cvt
