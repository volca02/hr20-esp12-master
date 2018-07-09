# hr20-esp12-master

Re-implementation (with different features) of the master portion of OpenHR20 wireless project.

This project aims to be a reimplementation of the master portion of the RFM12 based OpenHR20 variant.
Compared to that project's master implementation, this one aims to connect RFM12 directly to ESP8266
and thus enable the network of HR20s to be visible on MQTT or via WEB interface, without the need of
a PC or other computer connected to the master arduino.

The project is now able to serve as a (so far quite primitive) master on the HR20 network. 

## TODO

Missing parts for complete implementation are:

* weekly calendar plan persistence and diffing/change upload
* active (re-)request of values that are too old in the cached representation
* MQTT/Web control interface
* Aliases (name the individual clients)
* Persistence - persist parts/complete model of the network in case of power outage
* De-sync resolution (which is also a problem in original implementation) - could be fixed by sending a sync packet as a response to a packet with correct length but bad CMAC. This would need a more accurate sync packet with 1s accuracy (currently sync is :00 or :30)
* Some nice-to-have features, such as support for a small display/buttons for local control, etc.

## HW configuration
The RFM12 is connected to ESP via the SPI interface, with SS/NSEL being connected to GPIO2 (and NIRQ to GPIO5 in case RFM polling is disabled).

So this is the wiring between ESP-12 and RFM12b:

* D7/HMOSI/GPIO13 - SDI
* D6/HMISO/GPIO12 - SDO
* D5/HSCLK/GPIO14 - SCK
* D4/GPIO2        - NSEL

Optionally (For interrupt based RFM control):
* D1/GPIO5        - NIRQ

And VDD/GND as usual (3.3V).

## Building
The project is based around platformio - so please use that to build it.
