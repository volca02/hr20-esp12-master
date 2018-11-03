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

#include "str.h"

namespace hr20 {

#define LONG_INT_DIGITS 20

StrMaker & StrMaker::operator += (long int i) {
    char buf[LONG_INT_DIGITS];

    bool neg = i < 0;
    if (neg) i = -i;

    uint8_t p;

    // reverse temporary conversion
    for (p = 0; p < LONG_INT_DIGITS; ++p) {
        buf[p] = '0' + (i % 10);
        i = i / 10;
        if (!i) break;
    }

    if (neg) {
        if (full()) return *this;
        *pos = '-';
        ++pos;
    }

    const char *b = &buf[p];
    while (b > buf && *b == '0') --b;
    for (;b > buf; --b) append_char(*b);
    append_char(*b);

    return *this;
}

ICACHE_FLASH_ATTR void StrMaker::append(float f, unsigned decimals) {
    // split to two integral parts, append both using integral append
    int pre = f;
    int post = (f - pre) * pow(10, decimals);
    (*this) += pre;
    (*this) += '.';
    (*this) += post;
}


} // namespace hr20
