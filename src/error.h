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

#define ERR(CODE) do { report_error(CODE, __LINE__); } while (0)
#define ERR_ARG(CODE, VAL) do { report_error(CODE, VAL); } while (0)

namespace hr20 {

enum ErrorCode {
    INVALID_ERROR_CODE = 0,

    // ========== QUEUE ==========
    // Queue is full, can't add any more packets
    QUEUE_FULL = 1,
    // Can't prepare packet to be sent while another is being sent already
    QUEUE_PREPARE_WHILE_SEND,

    // ========== WIFI ==========
    // Bad wifi settings, starting config interface
    WIFI_CANNOT_CONNECT = 10,

    // ========== PROTOCOL ==========
    // Client address is out of range (0 or >=30)
    PROTO_BAD_CLIENT_ADDR = 20,
    PROTO_INCOMPLETE_PACKET,
    PROTO_PACKET_TOO_SHORT,
    PROTO_BAD_CMAC,
    // failed processing packet - cannot understand the contents
    PROTO_BAD_RESPONSE,
    // got command instead of reply
    PROTO_CANNOT_PROCESS,
    // Could not understand the packet contents
    PROTO_UNKNOWN_SEQUENCE,
    // Response to a command was too short
    PROTO_RESPONSE_TOO_SHORT,
    // Bad temperature - out of defined range
    PROTO_BAD_TEMP,
    // Malformed timer - slot or day out of range
    PROTO_BAD_TIMER,
    // in master, but logically protocol thing
    PROTO_PACKET_TOO_LONG,
    PROTO_EMPTY_PACKET,

    // ========== RFM ==========
    // RFM12B::begin called more than once!?
    RFM_ALREADY_INITIALIZED = 40,
    // These mean radio polling is too slow
    RFM_TX_UNDERRUN,
    RFM_RX_OVERFLOW,

    // ========== CONFIG ==========
    // Config errors
    CFG_MALFORMED_RFM_PASSWORD = 50,
    CFG_CANNOT_OPEN,
    CFG_NOT_FOUND,
    CFG_FAILED_TO_PARSE,
    CFG_INVALID_JSON,
    CFG_CANNOT_SAVE,

    // ========== CONFIG ==========
    // MQTT errors
    MQTT_CANNOT_CONNECT = 60,
    MQTT_INVALID_CLIENT,
    MQTT_INVALID_TOPIC,
    MQTT_INVALID_TIMER_TOPIC,
    MQTT_CALLBACK_BAD_ADDR,

    // ========== NTP ==========
    // NTP errors
    NTP_CANNOT_SYNC = 70
};

const char *err_to_str(ErrorCode err);

void report_error(ErrorCode err, int val = 0);

} // namespace hr20
