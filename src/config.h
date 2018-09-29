#pragma once

#include "FS.h"
#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jsmn.h>
#include "debug.h"

struct Config
{
    uint8_t rfm_pass[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0x01, 0x23, 0x45};
#ifdef NTP_CLIENT
    char ntp_server[41] = "2.europe.pool.ntp.org";
#endif
#ifdef MQTT
    char mqtt_client_id[20];
    char mqtt_server[41] = "";
    uint16_t mqtt_port = 1883;
    char mqtt_user[21] = "";
    char mqtt_pass[21] = "";
    char mqtt_topic_prefix[41] = "hr20";
#endif

    bool ICACHE_FLASH_ATTR save(const char *filename);
    bool ICACHE_FLASH_ATTR load(const char *filename);

    char *ICACHE_FLASH_ATTR get_rfm_pass_value();
    bool ICACHE_FLASH_ATTR set_rfm_pass(const char *pass);
    void ICACHE_FLASH_ATTR begin();

  private:
    char rfmPassValue[17];
    bool ICACHE_FLASH_ATTR jsoneq(const char *json, jsmntok_t *tok, const char *s);
    uint8_t ICACHE_FLASH_ATTR hex2int(char ch);

#ifdef MQTT
    const char *mqtt_client_id_prefix = "OpenHR20_";
#endif
};
