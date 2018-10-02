#pragma once

#include <string>

namespace cvt {

// Simple integer converter
struct Simple {
    ICACHE_FLASH_ATTR static String to_str(uint8_t val) {
        return String{(int)val};
    }

    ICACHE_FLASH_ATTR static uint8_t from_str(const String &dta) {
        return dta.toInt();
    }
};

/** converts to/from string for 0.5 degree temperature format
 *  Format: [X].H - H being 0 or 5
 */
struct TempHalfC {
    ICACHE_FLASH_ATTR static String to_str(uint8_t temp) {
        return String{float(temp) / 2, 1}; // 1 == decimal places
    }

    ICACHE_FLASH_ATTR static uint8_t from_str(const String &val) {
        return val.toFloat() * 2;
    }
};

/** converts to/from string for 0.01 degree temperature format
 *  Format: X.XX
 */
struct Temp001C {
    ICACHE_FLASH_ATTR static String to_str(uint16_t temp) {
        return String{float(temp) / 100, 2}; // 2 == decimal places
    }

    ICACHE_FLASH_ATTR static uint16_t from_str(const String &val) {
        return val.toFloat() * 100;
    }
};

/** converts to/from string for 0.01 degree temperature format
 *  Format: X.XX
 */
struct Voltage0001V {
    ICACHE_FLASH_ATTR static String to_str(uint16_t temp) {
        return String{float(temp) / 1000, 3}; // 3 == decimal places
    }

    ICACHE_FLASH_ATTR static uint16_t from_str(const String &val) {
        return val.toFloat() * 1000;
    }
};


} // namespace cvt
