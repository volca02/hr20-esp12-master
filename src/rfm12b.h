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

#include <Arduino.h>
#include <SPI.h>

#include "debug.h"
#include "queue.h"

namespace hr20 {

// RFM is wired as SPI client, with GPIO2 being used as the select pin
constexpr const uint8_t RFM_SS_PIN = 2;
// GPIO5 is connected to NIRQ to push/pull bytes
constexpr const uint8_t RFM_NIRQ_PIN = 5;

/*
 * A simple interface to RFM12B
 */
struct RFM12B {
    SPISettings spi_settings;

    enum Mode {
        NONE = 0,
        IDLE = 1,
        RX   = 2,
        TX   = 3
    };

    volatile Mode mode;

    RFM12B()
        : spi_settings(2000000L, MSBFIRST, SPI_MODE0)
        // default to NONE, so both TX and RX operations switch the accordingly
        , mode(NONE)
    {}

    void begin();

    /// call this to reset to receiver mode, sync-word activation
    /// (after packet was sent/received whole)
    void wait_for_sync() {
        switch_to_idle();
    }

    /// -1 if no data is present, >= 0 received byte
    int recv() {
        if (in.empty())
            return -1;

        return in.pop();
    }

    /// enqueues a character to be sent. returns false if fifo's full
    /// @note FILL the whole buffer in one go, or at least enough
    /// for the ISR based sending routine not to underrun. poll() call
    /// *will* switch to TX when IDLE and there are bytes to send.
    bool send(char c) {
        if (out.full())
            return false;

        out.push(c);
        return true;
    }

    /// true if the output queue is empty
    bool sent() {
        return out.empty();
    }

    /// synchronous polling routine that sends and/or receives one byte a time
    /// to be called from the main loop
    void poll();

    bool is_idle() const { return mode == IDLE; }
    bool is_sending() const { return mode == TX; }
    bool is_receiving() const { return mode == RX; }

protected:
    // TODO: Align this with PacketQ's SENT_PACKET_LEN
    // we have to be able to hold the whole packet at once,
    // as the naiive implementation of ShortQ does not allow for data appends
    // while being emptied (would need a circular buffer for that)
    ShortQ<84> out, in;
    uint8_t limit = 0; // read limit, decoded from the first byte
    uint8_t counter = 0; // envent counter - read/written bytes, reset on switch_*

    /// reads the status word
    uint16_t read_status();

    // receives single byte data from the radio, or -1 if none are available
    int recv_byte();

    // sends a byte if possible, otherwise returns false
    bool send_byte(unsigned char c);

    // called before expecting to receive data
    void switch_to_rx();

    // called before expecting to send data
    void switch_to_tx();

    /// resets the fifo to sync-word activation
    void switch_to_idle();

    /// does the SPI xfer for 16 bits while selecting the rfm12b chip by pulling
    /// down the RFM_SS_PIN
    uint16_t spi16(uint16_t reg);

#ifndef RFM_POLL_MODE
    // callback from interrupt that handles the TX/RX as needed
    void on_interrupt();

    // TODO: make a singleton out of this class, since we're singleton on irq
    // anyway
    static RFM12B *irq_instance;
    static void rfm_interrupt_handler();
#endif
};


} // namespace hr20
