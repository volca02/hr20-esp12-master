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

## First run
The project starts a Wifi AP every time it reboots, so configuration is possible via a mobile phone. Settings are also available by clicking the "configuration" link in project's webserver page.

## MQTT Structure
The project defaults to a mqtt interface. The whole subtree starts with a user specified prefix (settable in configuration).

```
Read only subtree:

/PREFIX/ADDRESS/average_temp
...            /battery         - battery voltage
...            /error           - error code, as received from client
...            /lock            - button lock status
...            /mode            - automatic/manual mode
...            /requested_temp  - requested temperature XX.X
...            /valve_wanted    - current setting of the valve
...            /window          - window detection status
...            /state           - json structure with common values (auto, lock, window, temp, bat,...)
                                - {"auto":false,"lock":false,"window":false,"temp":21.46,"bat":2.575,"temp_wtd":21.0,"temp_wset":0.0,"valve_wtd":43,"error":0,"last_seen":1607780775,"st":2}
...            /last_seen       - unix time of the last incoming data from the client

...            /eeprom/ADDR     - subtree containing read values from the settings EEPROM after issuing read/write commands

...            /timers/DAY/SLOT/time - time for the set slot
...                            /mode - mode for the selected slot 0-3

Settings subtree: These are write-only values:

/PREFIX/set/ADDRESS/requested_temp
...                /mode
...                /lock
...                /eeprom/EEPROM_ADDR/read   - ignores the topic contents, requests eeprom read for the specified address, after success sets the eeprom/ADDR topic in read subtree with set value
...                                   /write  - writes the set decadic value (topic content) to specified eeprom settings memory address, after success sets the eeprom/ADDR topic in read subtree with set value
...                /timers/DAY/SLOT/time      - sets time for given DAY/SLOT
...                                /mode      - sets mode for given DAY/SLOT

```
