#pragma once

/// THIS FILE IS MOSTLY COPY OF OpenHR20's rfm.h file
/// as that's probably the best definition file for the RFM12 registers/commands

// TODO: move these into platformio.ini
// settings
#define RFM_BAUD_RATE           9600
#define RFM_FREQ_MAIN           868
#define RFM_FREQ_FINE           0.35


#define RFM_CONFIG               0x8000

#define RFM_CONFIG_EL            0x8080 // Enable TX Register
#define RFM_CONFIG_EF            0x8040 // Enable RX FIFO buffer
#define RFM_CONFIG_BAND_315      0x8000 // Frequency band
#define RFM_CONFIG_BAND_433      0x8010
#define RFM_CONFIG_BAND_868      0x8020
#define RFM_CONFIG_BAND_915      0x8030
#define RFM_CONFIG_X_8_5pf       0x8000 // Crystal Load Capacitor
#define RFM_CONFIG_X_9_0pf       0x8001
#define RFM_CONFIG_X_9_5pf       0x8002
#define RFM_CONFIG_X_10_0pf      0x8003
#define RFM_CONFIG_X_10_5pf      0x8004
#define RFM_CONFIG_X_11_0pf      0x8005
#define RFM_CONFIG_X_11_5pf      0x8006
#define RFM_CONFIG_X_12_0pf      0x8007
#define RFM_CONFIG_X_12_5pf      0x8008
#define RFM_CONFIG_X_13_0pf      0x8009
#define RFM_CONFIG_X_13_5pf      0x800A
#define RFM_CONFIG_X_14_0pf      0x800B
#define RFM_CONFIG_X_14_5pf      0x800C
#define RFM_CONFIG_X_15_0pf      0x800D
#define RFM_CONFIG_X_15_5pf      0x800E
#define RFM_CONFIG_X_16_0pf      0x800F

// Status bits ////////////////////////////////////////////////////////////////
#define RFM_STATUS_CMD  0x0000

// RX FIFO reached the progr. number of bits. Cleared by any FIFO read method
#define RFM_STATUS_FFIT 0x8000
// TX register is ready to receive. Cleared by TX write
#define RFM_STATUS_RGIT 0x8000
// Power On reset. Cleared by read status
#define RFM_STATUS_POR  0x4000
// TX register underrun, register over write. Cleared by read status
#define RFM_STATUS_RGUR 0x2000
// RX FIFO overflow. Cleared by read status
#define RFM_STATUS_FFOV 0x2000
// Wake up timer overflow. Cleared by read status
#define RFM_STATUS_WKUP 0x1000
// Interupt changed to low. Cleared by read status
#define RFM_STATUS_EXT  0x0800
// Low battery detect.
#define RFM_STATUS_LBD  0x0400
#define RFM_STATUS_FFEM 0x0200 // FIFO is empty
#define RFM_STATUS_ATS  0x0100 // TX mode: Strong enough RF signal
#define RFM_STATUS_RSSI 0x0100 // RX mode: signal strength above programmed limit
#define RFM_STATUS_DQD  0x0080 // Data Quality detector output
#define RFM_STATUS_CRL  0x0040 // Clock recovery lock
#define RFM_STATUS_ATGL 0x0020 // Toggling in each AFC cycle

///////////////////////////////////////////////////////////////////////////////
//
// 2. Power Management Command
//
///////////////////////////////////////////////////////////////////////////////

#define RFM_POWER_MANAGEMENT     0x8200

#define RFM_POWER_MANAGEMENT_ER  0x8280 // Enable receiver
#define RFM_POWER_MANAGEMENT_EBB 0x8240 // Enable base band block
#define RFM_POWER_MANAGEMENT_ET  0x8220 // Enable transmitter
#define RFM_POWER_MANAGEMENT_ES  0x8210 // Enable synthesizer
#define RFM_POWER_MANAGEMENT_EX  0x8208 // Enable crystal oscillator
#define RFM_POWER_MANAGEMENT_EB  0x8204 // Enable low battery detector
#define RFM_POWER_MANAGEMENT_EW  0x8202 // Enable wake-up timer
#define RFM_POWER_MANAGEMENT_DC  0x8201 // Disable clock output of CLK pin

///////////////////////////////////////////////////////////////////////////////
//
// 3. Frequency Setting Command
//
///////////////////////////////////////////////////////////////////////////////

#define RFM_FREQUENCY            0xA000

#define RFM_FREQ_315Band(v) (uint16_t)((v/10.0-31)*4000)
#define RFM_FREQ_433Band(v) (uint16_t)((v/10.0-43)*4000)
#define RFM_FREQ_868Band(v) (uint16_t)((v/20.0-43)*4000)
#define RFM_FREQ_915Band(v) (uint16_t)((v/30.0-30)*4000)

// helper macros to derive macro name from main frequency
#define IntRFM_FREQ_Band(v) RFM_FREQ_ ## v ## Band
#define IntRFM_CONFIG_Band(v) RFM_CONFIG_BAND_ ## v
#define RFM_FREQ_Band(v) IntRFM_FREQ_Band(v)
#define RFM_CONFIG_Band(v) IntRFM_CONFIG_Band(v)

///////////////////////////////////////////////////////////////////////////////
//
// 4. Data Rate Command
//
/////////////////////////////////////////////////////////////////////////////////

#ifndef RFM_BAUD_RATE
 #define RFM_BAUD_RATE           19200
#endif

#ifndef RFM_FREQ_MAIN
 #define RFM_FREQ_MAIN           868
#endif

#ifndef RFM_FREQ_FINE
 #define RFM_FREQ_FINE           0.35
#endif

#define RFM_FREQ_DEC            (RFM_FREQ_MAIN + RFM_FREQ_FINE)

#define RFM_DATA_RATE            0xC600

#define RFM_DATA_RATE_CS         0xC680
#define RFM_DATA_RATE_4800       0xC647
#define RFM_DATA_RATE_9600       0xC623
#define RFM_DATA_RATE_19200      0xC611
#define RFM_DATA_RATE_38400      0xC608
#define RFM_DATA_RATE_57600      0xC605

// Using this formula as specified in the datasheet results in a slightly inflated data rate due to rounding. Original: #define RFM_SET_DATARATE_ORIG(baud)		( ((baud)<5400) ? (RFM_DATA_RATE_CS|((43104/(baud))-1)) : (RFM_DATA_RATE|((344828UL/(baud))-1)) )
#define RFM_SET_DATARATE(baud)		( ((baud)<4800) ? (RFM_DATA_RATE_CS|((43104/(baud)))) : (RFM_DATA_RATE|((344828UL/(baud)))) )

///////////////////////////////////////////////////////////////////////////////
//
// 5. Receiver Control Command
//
///////////////////////////////////////////////////////////////////////////////

#define RFM_RX_CONTROL           0x9000

#define RFM_RX_CONTROL_P20_INT   0x9000 // Pin20 = ExternalInt
#define RFM_RX_CONTROL_P20_VDI   0x9400 // Pin20 = VDI out

#define RFM_RX_CONTROL_VDI_FAST  0x9000 // fast   VDI Response time
#define RFM_RX_CONTROL_VDI_MED   0x9100 // medium
#define RFM_RX_CONTROL_VDI_SLOW  0x9200 // slow
#define RFM_RX_CONTROL_VDI_ON    0x9300 // Always on

#define RFM_RX_CONTROL_BW_400    0x9020 // bandwidth 400kHz
#define RFM_RX_CONTROL_BW_340    0x9040 // bandwidth 340kHz
#define RFM_RX_CONTROL_BW_270    0x9060 // bandwidth 270kHz
#define RFM_RX_CONTROL_BW_200    0x9080 // bandwidth 200kHz
#define RFM_RX_CONTROL_BW_134    0x90A0 // bandwidth 134kHz
#define RFM_RX_CONTROL_BW_67     0x90C0 // bandwidth 67kHz

#define RFM_RX_CONTROL_GAIN_0    0x9000 // LNA gain  0db
#define RFM_RX_CONTROL_GAIN_6    0x9008 // LNA gain -6db
#define RFM_RX_CONTROL_GAIN_14   0x9010 // LNA gain -14db
#define RFM_RX_CONTROL_GAIN_20   0x9018 // LNA gain -20db

#define RFM_RX_CONTROL_RSSI_103  0x9000 // DRSSI threshold -103dbm
#define RFM_RX_CONTROL_RSSI_97   0x9001 // DRSSI threshold -97dbm
#define RFM_RX_CONTROL_RSSI_91   0x9002 // DRSSI threshold -91dbm
#define RFM_RX_CONTROL_RSSI_85   0x9003 // DRSSI threshold -85dbm
#define RFM_RX_CONTROL_RSSI_79   0x9004 // DRSSI threshold -79dbm
#define RFM_RX_CONTROL_RSSI_73   0x9005 // DRSSI threshold -73dbm
//#define RFM_RX_CONTROL_RSSI_67   0x9006 // DRSSI threshold -67dbm // RF12B reserved
//#define RFM_RX_CONTROL_RSSI_61   0x9007 // DRSSI threshold -61dbm // RF12B reserved

#define RFM_RX_CONTROL_BW(baud)		(((baud)<8000) ? \
									RFM_RX_CONTROL_BW_67 : \
									( \
										((baud)<30000) ? \
										RFM_RX_CONTROL_BW_134 : \
										RFM_RX_CONTROL_BW_200 \
									))

///////////////////////////////////////////////////////////////////////////////
//
// 6. Data Filter Command
//
///////////////////////////////////////////////////////////////////////////////

#define RFM_DATA_FILTER          0xC228

#define RFM_DATA_FILTER_AL       0xC2A8 // clock recovery auto-lock
#define RFM_DATA_FILTER_ML       0xC268 // clock recovery fast mode
#define RFM_DATA_FILTER_DIG      0xC228 // data filter type digital
#define RFM_DATA_FILTER_ANALOG   0xC238 // data filter type analog
#define RFM_DATA_FILTER_DQD(level) (RFM_DATA_FILTER | (level & 0x7))

///////////////////////////////////////////////////////////////////////////////
//
// 7. FIFO and Reset Mode Command
//
///////////////////////////////////////////////////////////////////////////////

#define RFM_FIFO                 0xCA00

#define RFM_FIFO_AL              0xCA04 // FIFO Start condition sync-word/always
#define RFM_FIFO_FF              0xCA02 // Enable FIFO fill
#define RFM_FIFO_DR              0xCA01 // Disable hi sens reset mode
#define RFM_FIFO_IT(level)       (RFM_FIFO | (( (level) & 0xF)<<4))

#define RFM_FIFO_OFF()            RFM_SPI_16(RFM_FIFO_IT(8) |               RFM_FIFO_DR)
#define RFM_FIFO_ON()             RFM_SPI_16(RFM_FIFO_IT(8) | RFM_FIFO_FF | RFM_FIFO_DR)

/////////////////////////////////////////////////////////////////////////////
//
// 8. Synchron Pattern Command
//
/////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////
//
// 9. Receiver FIFO Read
//
/////////////////////////////////////////////////////////////////////////////

#define RFM_FIFO_READ 0xB000

/////////////////////////////////////////////////////////////////////////////
//
// 10. AFC Command
//
/////////////////////////////////////////////////////////////////////////////

#define RFM_AFC                  0xC400

#define RFM_AFC_EN               0xC401
#define RFM_AFC_OE               0xC402
#define RFM_AFC_FI               0xC404
#define RFM_AFC_ST               0xC408

// Limits the value of the frequency offset register to the next values:

#define RFM_AFC_RANGE_LIMIT_NO    0xC400 // 0: No restriction
#define RFM_AFC_RANGE_LIMIT_15_16 0xC410 // 1: +15 fres to -16 fres
#define RFM_AFC_RANGE_LIMIT_7_8   0xC420 // 2: +7 fres to -8 fres
#define RFM_AFC_RANGE_LIMIT_3_4   0xC430 // 3: +3 fres to -4 fres

// fres=2.5 kHz in 315MHz and 433MHz Bands
// fres=5.0 kHz in 868MHz Band
// fres=7.5 kHz in 915MHz Band

#define RFM_AFC_AUTO_OFF         0xC400 // 0: Auto mode off (Strobe is controlled by microcontroller)
#define RFM_AFC_AUTO_ONCE        0xC440 // 1: Runs only once after each power-up
#define RFM_AFC_AUTO_VDI         0xC480 // 2: Keep the foffset only during receiving(VDI=high)
#define RFM_AFC_AUTO_INDEPENDENT 0xC4C0 // 3: Keep the foffset value independently trom the state of the VDI signal

///////////////////////////////////////////////////////////////////////////////
//
// 11. TX Configuration Control Command
//
///////////////////////////////////////////////////////////////////////////////

#define RFM_TX_CONTROL           0x9800

#define RFM_TX_CONTROL_POW_0     0x9800
#define RFM_TX_CONTROL_POW_3     0x9801
#define RFM_TX_CONTROL_POW_6     0x9802
#define RFM_TX_CONTROL_POW_9     0x9803
#define RFM_TX_CONTROL_POW_12    0x9804
#define RFM_TX_CONTROL_POW_15    0x9805
#define RFM_TX_CONTROL_POW_18    0x9806
#define RFM_TX_CONTROL_POW_21    0x9807
#define RFM_TX_CONTROL_MOD_15    0x9800
#define RFM_TX_CONTROL_MOD_30    0x9810
#define RFM_TX_CONTROL_MOD_45    0x9820
#define RFM_TX_CONTROL_MOD_60    0x9830
#define RFM_TX_CONTROL_MOD_75    0x9840
#define RFM_TX_CONTROL_MOD_90    0x9850
#define RFM_TX_CONTROL_MOD_105   0x9860
#define RFM_TX_CONTROL_MOD_120   0x9870
#define RFM_TX_CONTROL_MOD_135   0x9880
#define RFM_TX_CONTROL_MOD_150   0x9890
#define RFM_TX_CONTROL_MOD_165   0x98A0
#define RFM_TX_CONTROL_MOD_180   0x98B0
#define RFM_TX_CONTROL_MOD_195   0x98C0
#define RFM_TX_CONTROL_MOD_210   0x98D0
#define RFM_TX_CONTROL_MOD_225   0x98E0
#define RFM_TX_CONTROL_MOD_240   0x98F0
#define RFM_TX_CONTROL_MP        0x9900

#define RFM_TX_CONTROL_MOD(baud)	(((baud)<8000) ? \
									RFM_TX_CONTROL_MOD_45 : \
									( \
										((baud)<20000) ? \
										RFM_TX_CONTROL_MOD_60 : \
										( \
											((baud)<30000) ? \
											RFM_TX_CONTROL_MOD_75 : \
											( \
												((baud)<40000) ? \
												RFM_TX_CONTROL_MOD_90 : \
												RFM_TX_CONTROL_MOD_120 \
											) \
										) \
									))

/////////////////////////////////////////////////////////////////////////////
//
// 12. PLL Setting Command
//
/////////////////////////////////////////////////////////////////////////////

#define RFM_PLL					0xCC02
#define RFM_PLL_uC_CLK_10		0x70
#define RFM_PLL_uC_CLK_3_3		0x50
#define RFM_PLL_uC_CLK_2_5		0x30

#define RFM_PLL_DELAY_ON		0x80
#define RFM_PLL_DELAY_OFF		0x00
#define RFM_PLL_DITHER_ON		0x00
#define RFM_PLL_DITHER_OFF		0x40
#define RFM_PLL_BIRATE_HI		0x01
#define RFM_PLL_BIRATE_LOW		0x00

/////////////////////////////////////////////////////////////////////////////
//
// 13. Transmitter Register Write Command
//
/////////////////////////////////////////////////////////////////////////////

#define RFM_TX_WRITE_CMD 0xB800
