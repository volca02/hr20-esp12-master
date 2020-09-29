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

#include "converters.h"

namespace hr20 {

// read-only value that gets reported by HR20 (not set back, not changeable)
template<typename T, typename CvT = cvt::Simple>
struct CachedValue {
    using Converter = CvT;

    // bitfield flags. internal rep to save space instead of bools...
    // not using values but bit indices here!
    // maximum is 7!
    enum ValueFlags {
        REMOTE_VALID  = 1,
        PUBLISHED     = 2,
        SUBSCRIBED    = 3
    };

    CachedValue() : reread_ctr() {
        reread_ctr.resume(); // we need to know the value immediately if possible
    }

    bool ICACHE_FLASH_ATTR needs_read() {
        // TODO: Too old values could be re-read here by forcing true return val
        return (!remote_valid()) && reread_ctr.should_retry();
    }

    void ICACHE_FLASH_ATTR set_remote(T val) {
        remote = val;
        remote_valid() = true;
        published()    = false;
        reread_ctr.pause();
    }

    T ICACHE_FLASH_ATTR get_remote() const { return remote; }

    ICACHE_FLASH_ATTR Flags::accessor published() {
        return flags[PUBLISHED];
    }

    ICACHE_FLASH_ATTR Flags::const_accessor published() const {
        return flags[PUBLISHED];
    }

    ICACHE_FLASH_ATTR Flags::accessor subscribed() {
        return flags[SUBSCRIBED];
    }

    ICACHE_FLASH_ATTR Flags::const_accessor subscribed() const {
        return flags[SUBSCRIBED];
    }

    ICACHE_FLASH_ATTR Flags::accessor remote_valid() {
        return flags[REMOTE_VALID];
    };

    ICACHE_FLASH_ATTR Flags::const_accessor remote_valid() const {
        return flags[REMOTE_VALID];
    }

    ICACHE_FLASH_ATTR Str to_str(Buffer buf) const {
        return Converter::to_str(buf, remote);
    }

protected:
    Flags flags;
    RequestDelay<REREAD_CYCLES> reread_ctr;
    T remote;
};

// implements a pair of values - local/remote, that is used to detect
// changes have to be written through to client.
template<typename T, typename CvT = cvt::Simple>
struct SyncedValue : public CachedValue<T, CvT> {
    using Converter = CvT;
    using Base      = CachedValue<T, CvT>;

    enum ValueFlags {
        REQUESTED_SET = 4 // used in synced value, this means requested value was set and not yet propagated
    };

    ICACHE_FLASH_ATTR Flags::accessor is_requested_set() {
        return this->flags[REQUESTED_SET];
    };

    ICACHE_FLASH_ATTR Flags::const_accessor is_requested_set() const {
        return this->flags[REQUESTED_SET];
    }

    bool ICACHE_FLASH_ATTR needs_write() {
        return (is_requested_set()) && (resend_ctr.should_retry());
    }

    T& ICACHE_FLASH_ATTR get_requested() { return requested; }
    const T& ICACHE_FLASH_ATTR get_requested() const { return requested; }


    void ICACHE_FLASH_ATTR set_requested(T val) {
        // NOTE: Could compare to the currently set value to save roundtrip.
        // Do this later on if it proves to be robust anyway.
        requested = val;
        is_requested_set() = true;
        // resume the send requests
        resend_ctr.resume();
    }

    void ICACHE_FLASH_ATTR reset_requested() {
        this->requested = this->remote;
        is_requested_set() = false;
        resend_ctr.pause();
    }

    // override for synced values - confirmations reset req_time
    void ICACHE_FLASH_ATTR set_remote(T val) {
        Base::set_remote(val);

        // if the value reported from client is equal to the requested
        // we pull down the requested status
        if (is_requested_set() && (val == this->requested)) {
            reset_requested();
        }
    }

    /** sets requested value by parsing a string. Returns true of the parse was ok
     *  and value was set. */
    ICACHE_FLASH_ATTR bool set_requested_from_str(const Str &val) {
        T cvtd;
        if (Converter::from_str(val, cvtd)) {
            set_requested(cvtd);
            return true;
        } else {
            // we expect this to be happening from MQTT...
            ERR(MQTT_INVALID_TOPIC_VALUE);
            return false;
        }
    }

    ICACHE_FLASH_ATTR Str req_to_str(Buffer buf) const {
        return Converter::to_str(buf, requested);
    }

private:
    T requested;
    RequestDelay<RESEND_CYCLES> resend_ctr;
};

} // namespace hr20
