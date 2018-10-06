#pragma once

#include <Arduino.h>

#include "config.h"

/// Delays re-requests a number of skips. Used to delay re-requests/re-submits of values
template<int8_t RETRY_SKIPS>
struct RequestDelay {
    // starts paused
    RequestDelay() : counter(-1) {}

    bool should_retry() {
        if (counter < 0)
            return false;

        if (counter == 0) {
            counter = RETRY_SKIPS;
            return true;
        }

        counter--;
        return false;
    }

    void pause() { counter = -1; }
    void resume() { counter = 0; }
    bool is_paused() const { return counter < 0; }
private:
    int8_t counter;
};

// 8bit flags packed in uint8_t
struct Flags {
    class const_accessor;

    struct accessor {
        friend class const_accessor;

        accessor(Flags &f, uint8_t idx) : f(f), idx(idx)
        {}

        operator bool() const {
            return f.val & (1 << idx);
        }

        bool operator=(bool b) {
            auto m = mask();
            f.val = (f.val & ~m) | (b ? m : 0);
            return operator bool();
        }

    protected:
        uint8_t mask() const { return 1 << idx; }
        Flags &f;
        uint8_t idx;
    };

    struct const_accessor {
        const_accessor(accessor &a) : f(a.f), idx(a.idx) {}
        const_accessor(const Flags &f, uint8_t idx) : f(f), idx(idx)
        {}

        operator bool() const {
            return f.val & (1 << idx);
        }

    protected:
        uint8_t mask() const { return 1 << idx; }
        const Flags &f;
        uint8_t idx;
    };

    const_accessor operator[](uint8_t flag) const {
        return {*this, flag};
    }

    accessor operator[](uint8_t flag) {
        return {*this, flag};
    }

private:
    uint8_t val = 0;
};

/// Accumulates non-synced client addrs for sync force flags/addrs
struct ForceFlags {

    /** pushes a new client that we need to talk with.
     * @param fat_comms This set to true means we need intense comm. when possible
     */
    ICACHE_FLASH_ATTR void push(uint8_t addr, bool fat_comms) {
        if (addr >= MAX_HR_COUNT) return;

        if (ctr < 2) {
            small[ctr] = addr;
        }

        ctr++;
        big += 1 << addr;
        fat |= fat_comms;
    }

    template<typename SyncP>
    ICACHE_FLASH_ATTR void write(SyncP &p) const {
        DBG("(FORCE %04X)", big);
        // do not force 2 client see-saw when we're not setting fat data
        if (ctr <= 2 && fat) {
            p->push(small[0]);
            p->push(small[1]);
        } else {
            // ATMega should be little endian
            p->push( big        & 0x0FF);
            p->push((big >>  8) & 0x0FF);
            p->push((big >> 16) & 0x0FF);
            p->push((big >> 24) & 0x0FF);
        }
    }

    uint8_t count() const { return ctr; }

    uint8_t ctr = 0;
    uint8_t small[2] = {0,0};
    uint32_t big = 0;
    bool fat = false;
};

// categorizes the changes in HR20 model
enum ChangeCategory {
    CHANGE_FREQUENT = 1,
    CHANGE_TIMER_MASK = 0x1FE, // mask of timer changes
    CHANGE_TIMER_0  = 2, // 8 bits of timers correspond to 8 days
    CHANGE_TIMER_1  = 4,
    CHANGE_TIMER_2  = 8,
    CHANGE_TIMER_3  = 16,
    CHANGE_TIMER_4  = 32,
    CHANGE_TIMER_5  = 64,
    CHANGE_TIMER_6  = 128,
    CHANGE_TIMER_7  = 256
};

extern ChangeCategory timer_day_2_change[8];

ICACHE_FLASH_ATTR inline uint8_t change_get_timer_mask(uint16_t change) {
    return (change & CHANGE_TIMER_MASK) >> 1;
}
