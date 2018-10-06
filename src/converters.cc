#include "converters.h"

namespace cvt {

char ICACHE_FLASH_ATTR int2hex(uint8_t v) {

    if (v > 15) return 0xFF;
    if (v < 10) return v + '0';
    return v + 'A' - 10;
}

int8_t ICACHE_FLASH_ATTR hex2int(char ch) {
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    return -1;
}

} // namespace cvt
