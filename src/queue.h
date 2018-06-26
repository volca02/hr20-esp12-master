#pragma once

#include <cstdint>

// implements byte FIFO queue of fixed max size.
template<uint8_t LenT>
struct ShortQ {
    uint8_t buf[LenT];
    uint8_t _pos = 0, _top = 0;

    bool push(uint8_t c) {
        if (full()) return false;
        buf[_top++] = c;
        return true;
    }

    uint8_t pop() {
        uint8_t c = 0x0;

        if (_pos < _top) c = buf[_pos++];

        // reset Q if we emptied it
        if (_pos >= _top) {
            clear();
        }

        return c;
    }

    uint8_t peek() const {
        if (_pos < _top)
            return buf[_pos];

        return 0x0;
    }

    bool empty() const { return _pos == _top; }
    bool full() const  { return _top >= LenT; }

    // raw data access for packet storage
    uint8_t *data() { return buf; }
    const uint8_t *data() const { return buf; }
    size_t size() const { return _top; }
    void clear() {
        _pos = 0;
        _top = 0;
    }

    // trims extra bytes from queue end
    bool trim(uint8_t count) {
        if (count >= _top || (_pos + count) >= _top) {
            clear();
            return false;
        }

        _top -= count;
        return true;
    }

    uint8_t operator[](size_t idx) const { return buf[idx]; }
};

// packet definition...
// we have 32 bytes for packets. more than enough
using Packet = ShortQ<32>;

// implements a command queue
template<uint8_t LenT, typename PacketT>
struct PacketQ {
    PacketQ() : que() {}

    /// insert into queue or return nullptr if full
    /// returns packet structure to be filled with data
    PacketT *put(uint8_t addr) {
        for (unsigned i = 0; i < LenT; ++i) {
            if (que[i].addr == 0) {
                que[i].addr = addr;
                return &que[i].packet;
            }
        }

        // full
        return nullptr;
    }

    /// destructive get - retrieves packet and marks position free
    PacketT *get(uint8_t addr) {
        for (unsigned i = 0; i < LenT; ++i) {
            if (que[i].addr == addr) {
                que[i].addr = 0;
                return &que[i].packet;
            }
        }

        // full
        return nullptr;
    }

    struct Item {
        uint8_t addr;
        PacketT packet;
    };

    Item que[LenT];
};
