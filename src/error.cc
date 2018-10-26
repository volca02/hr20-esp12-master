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

#include <Arduino.h>

#include "error.h"
#include "eventlog.h"

namespace hr20 {

ICACHE_FLASH_ATTR const char * err_to_str(ErrorCode err) {
#define HANDLE(CODE) case CODE: return #CODE;
    switch (err) {
        HANDLE(QUEUE_FULL);
        HANDLE(QUEUE_PREPARE_WHILE_SEND);

        HANDLE(WIFI_CANNOT_CONNECT);

        HANDLE(PROTO_BAD_CLIENT_ADDR);
        HANDLE(PROTO_INCOMPLETE_PACKET);
        HANDLE(PROTO_PACKET_TOO_SHORT);
        HANDLE(PROTO_BAD_CMAC);
        HANDLE(PROTO_BAD_RESPONSE);
        HANDLE(PROTO_CANNOT_PROCESS);
        HANDLE(PROTO_UNKNOWN_SEQUENCE);
        HANDLE(PROTO_RESPONSE_TOO_SHORT);
        HANDLE(PROTO_BAD_TEMP);
        HANDLE(PROTO_BAD_TIMER);
        HANDLE(PROTO_PACKET_TOO_LONG);
        HANDLE(PROTO_EMPTY_PACKET);

        HANDLE(RFM_ALREADY_INITIALIZED);
        HANDLE(RFM_TX_UNDERRUN);
        HANDLE(RFM_RX_OVERFLOW);

        HANDLE(CFG_MALFORMED_RFM_PASSWORD);
        HANDLE(CFG_CANNOT_OPEN);
        HANDLE(CFG_NOT_FOUND);
        HANDLE(CFG_FAILED_TO_PARSE);
        HANDLE(CFG_INVALID_JSON);
        HANDLE(CFG_CANNOT_SAVE);

        HANDLE(MQTT_CANNOT_CONNECT);
        HANDLE(MQTT_INVALID_CLIENT);
        HANDLE(MQTT_INVALID_TOPIC);
        HANDLE(MQTT_INVALID_TIMER_TOPIC);
        HANDLE(MQTT_CALLBACK_BAD_ADDR);
        HANDLE(MQTT_CANT_PUBLISH);
        HANDLE(MQTT_INVALID_TOPIC_VALUE);

        HANDLE(NTP_CANNOT_SYNC);

    default:
        return "INVALID_ERROR_CODE";
    }
#undef HANDLE
}

void ICACHE_FLASH_ATTR report_error(ErrorCode err, int val) {
    Serial.write("(!ERR ");
//    Serial.print(err_to_str(err));
    Serial.print(err);
    Serial.print(" ");
    Serial.print(val);
    Serial.println("!)");

    eventLog.append(EventType::ERROR, err, val);
}

} // namespace hr20
