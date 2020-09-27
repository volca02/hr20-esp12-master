# hr20-esp12-master

Re-implementation (with different features) of the master portion of OpenHR20 wireless project.

This project enables a network of RFM12 modified HR20 (Honeywell Rondostat thermostat) units to be
controlled and monitored via network.

It is a complete clean reimplementation of the master portion of the RFM12 based OpenHR20 project branch.

Compared to the rfmsrc variant of OpenHR20, this implementation enables direct control (via MQTT protocol)
of the HR20 clients using only ESP8266 and a connected radio module RFM12, with no additional computer
necessary.

To use this project, you will need a set of 1-N HR20 Rondostat units modified with RFM12 radio modules 
(and programmed with rfmsrc variant of OpenHR20), ESP8266 and a RFM12b module for the master.

The project is now able to serve as a full master on the wireless network.

Small things are still missing before the project is completely finished - notably a reconfiguration button 
handling and a convenience Android client (setting timers via mqtt would be a nightmare).

## HW configuration
The RFM12 is connected to ESP via the SPI interface, with SS/NSEL being connected to GPIO2.

So this is the wiring between ESP-12 and RFM12b:

* D7/HMOSI/GPIO13 - SDI
* D6/HMISO/GPIO12 - SDO
* D5/HSCLK/GPIO14 - SCK
* D4/GPIO2        - NSEL
* D1/GPIO5        - nIRQ

And VDD/GND as usual (3.3V).

## License
The whole project - all files included is/are licensed with GPL2 license. A part (rfmdef.h for RFM12 settings) is a modified copy of parts of a file from OpenHR20 project.

## Building
The project is based around platformio - so please use that to build it.
