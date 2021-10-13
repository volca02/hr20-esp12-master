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

#include "util.h"

namespace hr20 {

struct Buffer {
    friend struct StrMaker;

    Buffer(char *ptr = nullptr, unsigned len = 0)
        : ptr(ptr), len(len)
    {}

    template<unsigned LEN>
    Buffer(char buf[LEN])
        : ptr(buf), len(len)
    {}

    char *ptr;
    unsigned len;
};

template<unsigned LEN>
struct BufferHolder {
    operator Buffer() { return {buf, LEN}; }
protected:
    friend struct StrMaker;
    char buf[LEN];
};

/// immutable string that's using a borrowed buffer
struct Str {
    Str(const char *ptr = nullptr, unsigned len = 0)
        : ptr(ptr), len(len)
    {}

    ICACHE_FLASH_ATTR Str substring(unsigned offset, int count = -1) const {
        if (offset >= len) return {ptr + len, 0};
        int rest = len - offset;

        if (count >= 0 && rest > count) {
            rest = count;
        }

        return {ptr + offset, static_cast<unsigned>(rest)};
    }

    int indexOf(char c) const {
        for (unsigned i = 0; i < len; ++i) {
            if (ptr[i] == c)
                return i;
        }

        return -1;
    }

    bool operator==(const Str &other) const {
        return equals(other.ptr, other.len);
    }

    bool operator!=(const Str &other) const {
        return !equals(other.ptr, other.len);
    }

    bool equalsIgnoreCase(const char *buf) const {
        const char *p1 = ptr, *p2 = ptr;
        unsigned l1 = len, l2 = ::strlen(buf);
        for (;l1 && l2; --l1, --l2, ++p1, ++p2) {
            if (::tolower(*p1) != tolower(*p2))
                return false;
        }

        // one of the strings ended. return true if both did
        return l1 == l2;
    }

    ICACHE_FLASH_ATTR bool toFloat(float &tgt) const {
        // custom parser, no big deal
        if (!ptr) return false;

        float result = 0;
        float digp = 0.1;
        bool dot = false;
        const char *p = ptr;
        bool neg = false;
        uint8_t digits = 0;

        if (*p == '-') {
            ++p;
            neg = true;
        }

        for (const char *end = ptr + len;
             p < end;
             ++p)
        {
            if (*p =='.' && !dot) {
                dot = true;
                continue;
            }

            int8_t digit = todigit(*p);
            if (digit == -1) return false;

            if (!dot) {
                result *= 10;
                result += digit;
                ++digits;
            } else {
                result += digp * digit;
                digp   /= 10;
                ++digits;
            }
        }

        if (!digits) return false;

        tgt = result;
        if (neg) tgt = -tgt;
        return true;
    }

    ICACHE_FLASH_ATTR bool toInt(uint8_t &tgt) const {
        int temp;
        if (toInt(temp) && temp >= 0) {
            tgt = temp;
            return true;
        }
        return false;
    }

    ICACHE_FLASH_ATTR bool toInt(uint16_t &tgt) const {
        int temp;
        if (toInt(temp) && temp >= 0) {
            tgt = temp;
            return true;
        }
        return false;
    }

    ICACHE_FLASH_ATTR bool toInt(int &tgt) const {
        // custom parser, no big deal
        if (!ptr) return false;

        int result = 0;
        const char *p = ptr;

        bool neg = false;

        if (*p == '-') {
            ++p;
            neg = true;
        }

        for (const char *end = ptr + len;
             p < end;
             ++p)
        {
            int8_t digit = todigit(*p);
            if (digit == -1) return false;

            result *= 10;
            result += digit;
        }

        tgt = result;
        if (neg) tgt = -tgt;
        return true;
    }

    inline const char *c_str() const { return ptr; }
    inline const char *data() const { return ptr; }
    inline unsigned length() const  { return len; }

protected:
    bool equals(const char *p2, unsigned l2) const {
        const char *p1 = ptr;
        unsigned l1 = len;
        for (;l1 && l2; --l1, --l2, ++p1, ++p2) {
            if (*p1 != *p2)
                return false;
        }

        // one of the strings ended. return true if both did
        return l1 == l2;

    }

    const char *ptr = nullptr;
    unsigned len = 0;
};

/// appendable string composition helper
struct StrMaker {
    StrMaker(Buffer buf)
        : ptr(buf.ptr), capacity(buf.len), pos(ptr)
    {}

    ICACHE_FLASH_ATTR Str str() {
        if (invalid()) return {};
        // zero terminate the string to be compatible with C APIs
        if (!terminate()) return {};
        return Str{data(), size()};
    }

    ICACHE_FLASH_ATTR StrMaker & operator += (char c) {
        append_char(c);
        return *this;
    }

    ICACHE_FLASH_ATTR StrMaker & operator += (const char *c) {
        for (;*c;++c) {
            append_char(*c);
        }
        return *this;
    }

    ICACHE_FLASH_ATTR StrMaker & operator += (const Str &str) {
        const char *d = str.data();
        unsigned len = str.length();
        for (;len;--len,++d) {
            append_char(*d);
        }
        return *this;
    }

    ICACHE_FLASH_ATTR StrMaker & operator += (long int i);

    ICACHE_FLASH_ATTR StrMaker & operator += (time_t time) {
        return this->operator+=((long int)time);
    }

    ICACHE_FLASH_ATTR StrMaker & operator += (int i) {
        return this->operator+=((long int)i);
    }

    ICACHE_FLASH_ATTR StrMaker & operator += (unsigned u) {
        return this->operator+=((long int)u);
    }

    // appends float with rounding to default num. of decimal places
    ICACHE_FLASH_ATTR StrMaker & operator += (float f) {
        append(f);
        return *this;
    }

    void append(float f, unsigned decimals = 2);

    inline bool invalid() const {
        return pos == nullptr;
    };

    inline char *data() { return ptr; }
    inline const char *data() const { return ptr; }
    inline unsigned size() const  { return pos != nullptr ? pos - ptr : 0; }

protected:

    inline void append_char(char c) {
        if (full()) return;
        *pos = c;
        ++pos;
    }

    bool terminate() {
        if (full()) return false;
        *pos = '\0';
        return true;
    }

    bool full() {
        if (pos == nullptr)
            return true;

        if (pos >= ptr + capacity) {
            pos = nullptr;
            return true;
        }

        return false;
    }

    char *ptr = nullptr;
    unsigned capacity = 0;
    char *pos = nullptr;
};

} // namespace hr20
