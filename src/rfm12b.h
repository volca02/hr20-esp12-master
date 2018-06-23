#pragma once

#include <Arduino.h>
#include <SPI.h>

// implements byte FIFO queue of fixed max size.
template<uint8_t LenT>
struct ShortQ {
    char buf[LenT];
    uint8_t _pos = 0, _top = 0;

    bool push(char c) {
        if (full()) return false;
        buf[_top++] = c;
        return true;
    }

    char pop() {
        char c = 0x0;

        if (_pos < _top) c = buf[_pos++];

        // reset Q if we emptied it
        if (_pos >= _top) {
            clear();
        }

        return c;
    }

    char peek() const {
        if (_pos < _top)
            return buf[_pos];

        return 0x0;
    }

    bool empty() const { return _pos == _top; }
    bool full() const  { return _top >= LenT; }

    // raw data access for packet storage
    char *data() { return buf; }
    const char *data() const { return buf; }
    size_t size() const { return _top; }
    void clear() {
        _pos = 0;
        _top = 0;
    }
    char operator[](size_t idx) const { return buf[idx]; }
};

// RFM is wired as SPI client, with GPIO2 being used as the select pin
#define RFM_SS_PIN 2

/*
 * A simple interface to RFM12B
 */
struct RFM12B {
    SPISettings spi_settings;

    enum Mode {
        NONE = 0,
        RX = 1,
        TX = 2
    };

    Mode mode;

    RFM12B()
        // we use 500kHz SPI freq. Even 2M should work, though
        : spi_settings(500000L, MSBFIRST, SPI_MODE0)
        // default to NONE, so both TX and RX operations switch the accordingly
        , mode(NONE)
    {}

    void begin();

    /// call this to reset to receiver mode, sync-word activation
    /// (after packet was sent/received whole)
    void wait_for_sync() {
        reset_fifo();

        // turn on RX
        guarantee_rx();
    }

    /// -1 if no data is present, >= 0 received byte
    int recv() {
        if (in.empty())
            return -1;

        return in.pop();
    }

    /// enqueues a character to be sent. returns false if fifo's full
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
    void poll() {
        // any data to send?
        if (!out.empty()) {
            if (send_byte(out.peek()))
                out.pop();

            // do not attempt to read if there are data to be sent.
            return;
        }

        // don't overfill the input queue!
        if (in.full()) return;

        auto r = recv_byte();
        if (r >= 0) in.push((char)r);
    }

    bool isSending() const { return mode == TX; }
    bool isReceiving() const { return mode == RX; }

protected:
    ShortQ<32> out, in;

    /// reads the status word
    uint16_t readStatus();

    // receives single byte data from the radio, or -1 if none are available
    int recv_byte();

    // sends a byte if possible, otherwise returns false
    bool send_byte(unsigned char c);

    // called before expecting to receive data
    void guarantee_rx();

    // called before expecting to send data
    void guarantee_tx();

    /// resets the fifo to sync-word activation
    void reset_fifo();

    /// does the SPI xfer for 16 bits while selecting the rfm12b chip by pulling
    /// down the RFM_SS_PIN
    uint16_t spi16(uint16_t reg);
};
