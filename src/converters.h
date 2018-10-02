#pragma once

#include <string>

namespace cvt {

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
        return String{(int)val};
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
 *  Format: [X].H - H being 0 or 5
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

        if (colon_pos >= 0 && ((unsigned)colon_pos < (val.length() - 1))) {
            tgt = val.substring(0, colon_pos).toInt() * 60 +
                val.substring(colon_pos + 1).toInt();
            return true;
        }
        return false;
    }
};

} // namespace cvt
