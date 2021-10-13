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

#include "rfm12b.h"
#include "rfmdef.h"
#include "hr-debug.h"
#include "error.h"

namespace hr20 {

// None, idle, rx, tx
static const char *MODE = "NIRT";

#ifndef RFM_POLL_MODE
RFM12B *RFM12B::irq_instance = nullptr;
#endif

// scoped object setting the correct SPI parameters and selecting our radio
struct SPIScope {
    SPIScope(const SPISettings &s) {
        // this is the setting to use with our RFM12 client
        SPI.beginTransaction(s);
        // pull SS pin low to select it, too
        digitalWrite(RFM_SS_PIN, LOW);
    }

    ~SPIScope() {
        digitalWrite(RFM_SS_PIN, HIGH);
        SPI.endTransaction();
    }
};

void RFM12B::begin() {
    if (init) {
        ERR(RFM_ALREADY_INITIALIZED);
        return;
    }

    init = true;

    SPI.begin();

    // set GPIO2 to be our NSEL pin
    pinMode(RFM_SS_PIN, OUTPUT);
    digitalWrite(RFM_SS_PIN, HIGH);

    // prepare the NIRQ pin
    pinMode(RFM_NIRQ_PIN, INPUT_PULLUP);

    // the rest of this ctor is just stock initialization as
    // copied from OpenHR20's rfm.c initialization routine
    read_status();
    read_status();

    // write some bogus data to fill the TX input while we set the radio up
    spi16(RFM_TX_WRITE_CMD | 0xAA);
    spi16(RFM_TX_WRITE_CMD | 0xAA);

    spi16(
            RFM_CONFIG_EL                  |
            RFM_CONFIG_EF                  |
            RFM_CONFIG_Band(RFM_FREQ_MAIN) |
            RFM_CONFIG_X_12_0pf
    );

    int8_t adjust = 0;

    spi16(
            RFM_FREQUENCY            |
            (RFM_FREQ_Band(RFM_FREQ_MAIN)(RFM_FREQ_DEC) + adjust)
    );

    // 4. Data Rate Command
    spi16(RFM_SET_DATARATE(RFM_BAUD_RATE));

    // 5. Receiver Control Command
    spi16(
            RFM_RX_CONTROL_P20_VDI  |   // 0x9400
            RFM_RX_CONTROL_VDI_FAST |
            RFM_RX_CONTROL_BW(RFM_BAUD_RATE) |
            RFM_RX_CONTROL_GAIN_14   |
            RFM_RX_CONTROL_RSSI_97
    );

    // 6. Data Filter Command
    spi16(
            RFM_DATA_FILTER_AL      |
            RFM_DATA_FILTER_ML      |
            RFM_DATA_FILTER_DQD(3)
    );

    // 7. FIFO and Reset Mode Command
    spi16(
            RFM_FIFO_IT(8) |
            RFM_FIFO_DR
    );

    // 8. Synchron Pattern Command

    // 9. Receiver FIFO Read

    // 10. AFC Command
    spi16(
            RFM_AFC_AUTO_VDI        |
            RFM_AFC_RANGE_LIMIT_7_8 |
            RFM_AFC_EN              |
            RFM_AFC_OE              |
            RFM_AFC_FI
    );

    // 11. TX Configuration Control Command
    spi16(
            RFM_TX_CONTROL_MOD(RFM_BAUD_RATE) |
            RFM_TX_CONTROL_POW_0
    );

    // 12. PLL Setting Command
    spi16(
            RFM_PLL                 |
            RFM_PLL_uC_CLK_10       |
            RFM_PLL_DELAY_OFF       |
            RFM_PLL_DITHER_OFF      |
            RFM_PLL_BIRATE_LOW
    );

#ifndef RFM_POLL_MODE
    // 13. Switch off, attach interrupt
    spi16(RFM_POWER_MANAGEMENT_EX);

    if (irq_instance != nullptr) {
        ERR(RFM_ALREADY_INITIALIZED);
    } else {
        irq_instance = this;

        // attach the interrupt
        attachInterrupt(
                digitalPinToInterrupt(RFM_NIRQ_PIN),
                &RFM12B::rfm_interrupt_handler,
                FALLING);

        DBG("(RFM ISR SET)");

        bool isr_low = digitalRead(RFM_NIRQ_PIN) == LOW;
        if (isr_low) {
            // WHY IS THE ISR ALREADY LOW IN SOME CASES? Initialization process screws us up?
            auto st = spi16(RFM_STATUS_CMD);
            DBG("(ISR SET ALREADY LOW! ST=%X)", st);
        }
    }
#endif

    // default is to be waiting for sync word
    switch_to_idle();
}

#ifdef DEBUG_RFM
static volatile uint16_t isr_status = 0xFFFF;
static volatile uint16_t isr_ctr    = 0;
#endif

// status and counters from isr
static volatile uint16_t isr_txb = 0;
static volatile uint16_t isr_rxb = 0;
static volatile bool isr_underrun = false;

void ICACHE_FLASH_ATTR RFM12B::update() {
#ifdef DEBUG_RFM
    static uint16_t ctr = 0x0;

    // interrupt(s) happened in the meantime
    if (ctr != isr_ctr) {
        DBG("(ISR %u: %X TX %d RX %d O %d I %d/%d)",
            isr_ctr,
            isr_status,
            isr_txb,
            isr_rxb,
            out.rest_size(),
            in.rest_size(),
            in.size());

        isr_status = 0x0FFFF;
        ctr = isr_ctr;
    }
#endif

#ifndef RFM_POLL_MODE
    // handle underrun reporting
    if (isr_underrun) {
        isr_underrun = false;
        ERR(RFM_TX_UNDERRUN); // TX underrun, otherwise we don't care
        return;
    }
#endif

    // we need a polling routine called anyway, for situations
    // when send was called while we were still RX...

    // if there are data in the out queue, we switch to TX
    if (!out.empty() && (mode != TX)) {
        switch_to_tx();
    }

#ifdef RFM_POLL_MODE
    if (mode == TX) {
        // no more data in queue means we can switch to idle now
        if (out.empty()) {
            // bogus data. just to be sure there's something in the buffers
            // before switching to idle
            spi16(RFM_TX_WRITE_CMD | 0xAA);
            switch_to_idle();
            return;
        } else {
            // any data to send? Check if we're done receiving first
            if (send_byte(out.peek()))
                out.pop();

            // switch to idle - wait for incoming data now
            if (out.empty()) switch_to_idle();
            return;
        }
    } else {
        // don't overfill the input queue!
        if (in.full()) return;

        // this will poll the radio if it has any data. it will be silent in IDLE
        // mode. recv_byte will switch to RX after it gets some.
        auto r = recv_byte();
        if (r >= 0) in.push((char)r);
    }
#endif
}

uint16_t ICACHE_FLASH_ATTR RFM12B::read_status() {
    return spi16(RFM_STATUS_CMD);
}

int ICACHE_FLASH_ATTR RFM12B::recv_byte() {
    // can't receive in TX mode
    if (mode == TX) return -2;

    auto st = read_status();

    if (st & RFM_STATUS_RGUR) {
        // ERR(RFM_RX_OVERFLOW); // RX overflow
        // Not reporting, happens too often when we're not interested in
        // incoming data any more, but didn't manage to close the RX
        // in time.
    }

    if (st & RFM_STATUS_FFIT) {
        // forced RX as the radio woken up from IDLE for sure
#ifdef DEBUG_RFM
        Mode last_mode = mode;
#endif
        mode = RX;
#ifdef DEBUG_RFM
        if (last_mode != RX) { DBG("(R %c %c)", MODE[last_mode], MODE[mode]); }
#endif
        auto b = spi16(RFM_FIFO_READ);
        ++counter;
        return ((uint16_t)b & 0x00FF);
    }

    return -1;
}

bool ICACHE_FLASH_ATTR RFM12B::send_byte(unsigned char c) {
    if (mode != TX) return false;

    auto st = read_status();

    if (st & RFM_STATUS_RGUR) {
        ERR(RFM_TX_UNDERRUN); // TX underrun
        out.clear();
        switch_to_idle();
    }

    if (st & RFM_STATUS_RGIT) {
        spi16(RFM_TX_WRITE_CMD | ((c) & 0xFF));
        ++counter;
        return true;
    }

    return false;
}

void RFM12B::switch_to_rx() {
    if (mode != RX) {
#ifdef DEBUG_RFM
        Mode last_mode = mode;
#endif
        mode = RX;
#ifdef DEBUG_RFM
        DBG("(R %c %c)", MODE[last_mode], MODE[mode]);
#endif

        spi16(RFM_POWER_MANAGEMENT_DC  |
              RFM_POWER_MANAGEMENT_ER  |
              RFM_POWER_MANAGEMENT_EBB |
              RFM_POWER_MANAGEMENT_ES  |
              RFM_POWER_MANAGEMENT_EX);
#ifdef DEBUG_RFM
        if (last_mode == TX && counter) { DBG("(SNT %u)", counter); }
#endif
        counter = 0;
    }
}

void RFM12B::switch_to_tx() {
    // ignore TX request if there's no data to be sent
    if (out.empty()) return;

    if (mode != TX) {
#ifdef DEBUG_RFM
        Mode last_mode = mode;
#endif
        mode = TX;
#ifdef DEBUG_RFM
        DBG("(R %c %c)", MODE[last_mode], MODE[mode]);
#endif
        // bogus before switching
        spi16(RFM_TX_WRITE_CMD | 0xAA);
        spi16(RFM_TX_WRITE_CMD | 0xAA);

        spi16(RFM_POWER_MANAGEMENT_DC |
              RFM_POWER_MANAGEMENT_ET |
              RFM_POWER_MANAGEMENT_ES |
              RFM_POWER_MANAGEMENT_EX);

        in.clear();
        counter = 0;
    }
}

void RFM12B::switch_to_idle() {
    if (mode != IDLE) {
#ifdef DEBUG_RFM
        // Called in ISR, don't mess with timing
        Mode last_mode = mode;
#endif
        mode = IDLE;
#ifdef DEBUG_RFM
        DBG("(R %c %c)", MODE[last_mode], MODE[mode]);
#endif

        spi16(RFM_POWER_MANAGEMENT_DC  |
              RFM_POWER_MANAGEMENT_ER  |
              RFM_POWER_MANAGEMENT_EBB |
              RFM_POWER_MANAGEMENT_ES  |
              RFM_POWER_MANAGEMENT_EX);

        // (re)turn on FIFO to sync-word activation
        spi16(RFM_FIFO_IT(8) | RFM_FIFO_DR);
        spi16(RFM_FIFO_IT(8) | RFM_FIFO_FF | RFM_FIFO_DR);

// Why is this here?!
//        read_status();
#ifdef DEBUG_RFM
        if (last_mode == TX && counter) { DBG("(SNT %u)", counter); }
#endif
        counter = 0;

        out.clear();
    }
}

uint16_t RFM12B::spi16(uint16_t reg) {
    SPIScope scope(spi_settings);
    uint16_t res = SPI.transfer16(reg);
    return res;
}

#ifndef RFM_POLL_MODE
// NOTE: Not using ICACHE_RAM_ATTR as it seems to cause Exception 0
// Just skipping the ICACHE_FLASH_ATTR is enough for this to work
void ICACHE_RAM_ATTR RFM12B::rfm_interrupt_handler() {
    if (irq_instance) irq_instance->on_interrupt();
}

void ICACHE_RAM_ATTR RFM12B::on_interrupt() {
    // Interrupt handler
    auto st = spi16(RFM_STATUS_CMD);

#ifdef DEBUG_RFM
    isr_status = st;
    isr_ctr++;
#endif

    if (mode == TX) {
        if (st & RFM_STATUS_RGUR) {
            isr_underrun = true;
            switch_to_idle();
            return;
        }

        if (st & RFM_STATUS_RGIT) {
            // ready to send... what do we have?
            if (out.empty()) {
                // just send some dummy data
                spi16(RFM_TX_WRITE_CMD | 0xAA);
                // and switch to idle
                switch_to_idle();
            } else {
                auto c = out.pop();
                spi16(RFM_TX_WRITE_CMD | (c & 0xFF));
                ++counter;
                isr_txb++;

                if (out.empty()) {
                    // just send some dummy data to prevent possible underruns
                    spi16(RFM_TX_WRITE_CMD | 0xAA);
                    switch_to_idle();
                    return;
                }
            }
        }
    } else {
        if (st & RFM_STATUS_FFIT) {
            // FIFO is 16 bit!
            auto b = spi16(RFM_FIFO_READ);

            if (mode == IDLE) {
                mode = RX; // if it were IDLE, it's not any more
                limit = b & 0x7F;
                in.clear();
            }

            if (limit) {
                in.push(b & 0x00FF);
                limit--;
                isr_rxb++;
            } else {
                switch_to_idle();
            }
        }
    }
}
#endif

} // namespace hr20
