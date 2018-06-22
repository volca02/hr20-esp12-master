# hr20-esp12-master

Re-implementation (with different features) of the master portion of OpenHR20 wireless project.

This project aims to be a reimplementation of the master portion of the RFM12 based OpenHR20 variant. 
Compared to that project's master implementation, this one aims to connect RFM12 directly to ESP8266
and thus enable the network of HR20s to be visible on MQTT or via WEB interface, without the need of
a PC or other computer connected to the master arduino.


So far, the project is able to read the communication between master and HR20s, and there is code ready
to encrypt and sign the packets. In the near future, the project will gain the ability to decode and send
HR20 messages, and become the master in the network. After that, the current plan is to persist parts of
the HR20 settings locally in RAM of the ESP master, and enable IOT like control of the HR20 network.

## HW configuration
The RFM12 is connected to ESP via the SPI interface, with SS/NSEL being connected to GPIO2.

So this is the wiring between ESP-12 and RFM12b:

* D7 - SDI
* D6 - SDO
* D5 - SCK
* D4 - NSEL

And VDD/GND as usual.

## Building
The project is based around platformio - so please use that to build it.
