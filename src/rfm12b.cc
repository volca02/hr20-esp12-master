// #include <interrupts.h>

#include "rfm12b.h"
#include "rfmdef.h"
#include "debug.h"

RFM12B *RFM12B::irq_instance = nullptr;

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
    SPI.begin();

    // set GPIO2 to be our NSEL pin
    pinMode(RFM_SS_PIN, OUTPUT);
    digitalWrite(RFM_SS_PIN, HIGH);

    // prepare the NIRQ pin
    pinMode(RFM_NIRQ_PIN, INPUT_PULLUP);

    if (irq_instance != nullptr) {
        ERR("Multiple registrations for RFM");
    } else {
        irq_instance = this;

        // attach the interrupt
        attachInterrupt(
                digitalPinToInterrupt(RFM_NIRQ_PIN),
                &RFM12B::rfm_interrupt_handler,
                FALLING);
    }

    // the rest of this ctor is just stock initialization as
    // copied from OpenHR20's rfm.c initialization routine
    readStatus();
    readStatus();

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

    // default is to be waiting for sync word
    guarantee_idle();
}

uint16_t ICACHE_FLASH_ATTR RFM12B::readStatus() {
    return spi16(0x00);
}


int ICACHE_FLASH_ATTR RFM12B::recv_byte() {
    // can't receive in TX mode
    if (mode == TX) return -2;

    auto st = readStatus();
    if (st & RFM_STATUS_FFIT) {
        // forced RX as the radio woken up from IDLE for sure
        mode = RX;
        auto b = spi16(RFM_FIFO_READ);
        return ((uint16_t)b & 0x00FF);
    }

    return -1;
}

bool ICACHE_FLASH_ATTR RFM12B::send_byte(unsigned char c) {
    guarantee_tx();
    spi16(RFM_TX_WRITE_CMD | ((c) & 0xFF));
    return true;
}

void RFM12B::guarantee_rx() {
    if (mode != RX) {
        spi16(RFM_POWER_MANAGEMENT_DC  |
              RFM_POWER_MANAGEMENT_ER  |
              RFM_POWER_MANAGEMENT_EBB |
              RFM_POWER_MANAGEMENT_ES  |
              RFM_POWER_MANAGEMENT_EX);
        mode = RX;
    }
}

void RFM12B::guarantee_tx() {
    if (out.empty()) {
        ERR("SND NO DATA");
        return;
    }

    if (mode != TX) {
        spi16(RFM_POWER_MANAGEMENT_DC |
              RFM_POWER_MANAGEMENT_ET |
              RFM_POWER_MANAGEMENT_ES |
              RFM_POWER_MANAGEMENT_EX);
        mode = TX;
    }
}

void RFM12B::guarantee_idle() {
    if (mode != IDLE) {
        spi16(RFM_POWER_MANAGEMENT_DC  |
              RFM_POWER_MANAGEMENT_ER  |
              RFM_POWER_MANAGEMENT_EBB |
              RFM_POWER_MANAGEMENT_ES  |
              RFM_POWER_MANAGEMENT_EX);

        // (re)turn on FIFO to sync-word activation
        spi16(RFM_FIFO_IT(8) | RFM_FIFO_DR);
        spi16(RFM_FIFO_IT(8) | RFM_FIFO_FF | RFM_FIFO_DR);

        in.clear();
        mode = IDLE;
    }
}

uint16_t RFM12B::spi16(uint16_t reg) {
    SPIScope scope(spi_settings);
    uint16_t res = SPI.transfer16(reg);
    return res;
}

// NOTE: Not using ICACHE_RAM_ATTR as it seems to cause Exception 0
// Just skipping the ICACHE_FLASH_ATTR is enough for this to work
void RFM12B::rfm_interrupt_handler() {
    if (irq_instance) irq_instance->on_interrupt();
}

void RFM12B::on_interrupt() {
    // Interrupt handler
    auto st = spi16(RFM_STATUS_CMD);

    if (mode == TX) {
        // this might happen, just send dummy trailing data
        if (out.empty()) {
            // just send some dummy data
            spi16(RFM_TX_WRITE_CMD | 0xAA);
            // and switch to idle
            guarantee_idle();
            return;
        }

        // ready to get send another byte
        if (st & RFM_STATUS_RGIT) {
            auto c = out.pop();
            spi16(RFM_TX_WRITE_CMD | (c & 0xFF));
        }

        if (out.empty()) {
            guarantee_idle();
            return;
        }
    } else { // RX or IDLE
        if (st & RFM_STATUS_FFIT) {
            mode = RX; // if it were IDLE, it's not any more
            auto b = spi16(RFM_FIFO_READ);
            in.push(b & 0x00FF);
        }
    }
}
