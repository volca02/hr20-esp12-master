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
    enum ValueFlags {
        REMOTE_VALID = 1,
        PUBLISHED    = 2,
        SUBSCRIBED   = 4
    };

    CachedValue() : reread_ctr() {
        reread_ctr.resume(); // we need to know the value immediately if possible
    }

    bool ICACHE_FLASH_ATTR needs_read() {
        // TODO: Too old values could be re-read here by forcing true return val
        return (!remote_valid()) && reread_ctr.should_retry();
    }

    void ICACHE_FLASH_ATTR set_remote(T val) {
        remote_valid() = true;
        if (remote != val) published() = false;
        remote = val;
        reread_ctr.pause();
    }

    T ICACHE_FLASH_ATTR get_remote() const { return remote; }

    Flags::accessor published() {
        return flags[PUBLISHED];
    }

    Flags::const_accessor published() const {
        return flags[PUBLISHED];
    }

    Flags::accessor subscribed() {
        return flags[SUBSCRIBED];
    }

    Flags::const_accessor subscribed() const {
        return flags[SUBSCRIBED];
    }

    Flags::accessor remote_valid() {
        return flags[REMOTE_VALID];
    };

    Flags::const_accessor remote_valid() const {
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

    bool ICACHE_FLASH_ATTR needs_write() {
        return (!is_synced()) && (resend_ctr.should_retry());
    }

    T& ICACHE_FLASH_ATTR get_requested() { return requested; }
    const T& ICACHE_FLASH_ATTR get_requested() const { return requested; }

    void ICACHE_FLASH_ATTR set_requested(T val) {
        requested = val;

        // don't set requested if we already hold the same value
        if (!this->needs_read() && is_synced()) return;

        // resume the send requests
        resend_ctr.resume();
    }

    void ICACHE_FLASH_ATTR reset_requested() {
        this->requested = this->remote;
        resend_ctr.pause();
    }

    // override for synced values - confirmations reset req_time
    void ICACHE_FLASH_ATTR set_remote(T val) {
        // set over a known prev. value should reset set requests
        bool diff = (this->remote != val) && this->remote_valid();

        Base::set_remote(val);

        // if none was requested in the meantime, also set requested
        if (this->resend_ctr.is_paused()) {
            this->requested = this->remote;
        } else {
            // set went through or was overwritten
            if (is_synced() || diff) {
                // don't want to set any more
                this->resend_ctr.pause();
                // sync the value just in case it was a diff
                this->requested = this->remote;
            }
        }
    }

    // returns true for values that don't have newer change request
    bool ICACHE_FLASH_ATTR is_synced() const {
        return this->remote == this->requested;
    }

    /** sets requested value by parsing a string. Returs true of the parse was ok
     *  and value was set. */
    ICACHE_FLASH_ATTR bool set_requested_from_str(const Str &val) {
        T cvtd;
        if (Converter::from_str(val, cvtd)) {
            set_requested(cvtd);
            return true;
        }

        return false;
    }

private:
    T requested;
    RequestDelay<RESEND_CYCLES> resend_ctr;
};

} // namespace hr20
