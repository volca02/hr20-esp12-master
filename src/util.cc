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

#include "util.h"

namespace hr20 {

ChangeCategory timer_day_2_change[8] = {
    CHANGE_TIMER_0,
    CHANGE_TIMER_1,
    CHANGE_TIMER_2,
    CHANGE_TIMER_3,
    CHANGE_TIMER_4,
    CHANGE_TIMER_5,
    CHANGE_TIMER_6,
    CHANGE_TIMER_7
};

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

int8_t ICACHE_FLASH_ATTR todigit(char ch) {
    if (ch >= '0' && ch <= '9')
        return ch - '0';

    return -1;
}

} // namespace hr20
