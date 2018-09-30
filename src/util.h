#pragma once

char ICACHE_FLASH_ATTR int2hex(uint8_t v) {
    static const char *tbl = "0123456789ABCDEF";

    if (v > 15) return 0xFF;

    return tbl[v];
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
