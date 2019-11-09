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

#include <FS.h>
#include <jsmn.h>

#include "config.h"
#include "converters.h"
#include "util.h"
#include "error.h"

namespace hr20 {
namespace {
static bool ICACHE_FLASH_ATTR jsoneq(const char *json,
                                     jsmntok_t *tok,
                                     const char *s)
{
  if (tok->type == JSMN_STRING && (int)strlen(s) == tok->end - tok->start &&
      strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
    return true;
  }
  return false;
}

} // namespace

bool ICACHE_FLASH_ATTR Config::begin(const char *fname)
{
#ifdef MQTT
    sprintf(mqtt_client_id, "%s%08x", mqtt_client_id_prefix, ESP.getChipId());
#endif
//    return load(fname);
    return true;
}

bool ICACHE_FLASH_ATTR Config::rfm_pass_to_binary(unsigned char *target) {
    for (int j = 0; j < 8; ++j)
    {
        int8_t x0 = hex2int(rfm_pass_hex[j * 2    ]);
        int8_t x1 = hex2int(rfm_pass_hex[j * 2 + 1]);

        if ((x0 < 0) || (x1 < 0)) {
            // happens even for short passwords
            ERR(CFG_MALFORMED_RFM_PASSWORD);
            return false;
        }

        target[j] = (x0 << 4) | x1;
    }
    return true;
}

bool ICACHE_FLASH_ATTR Config::save(const char *filename)
{
    SPIFFS.begin();
    File file = SPIFFS.open(filename, "w");
    if (!file) {
        ERR(CFG_CANNOT_OPEN);
        SPIFFS.end();
        return false;
    } else {
        DBG("writing to file: %s", filename);
        file.printf("{"
#ifdef NTP_CLIENT
                    "\"ntp_server\":\"%s\","
#endif
#ifdef MQTT
                    "\"mqtt_server\":\"%s\","
                    "\"mqtt_port\":%s,"
                    "\"mqtt_user\":\"%s\","
                    "\"mqtt_pass\":\"%s\","
                    "\"mqtt_topic_prefix\":\"%s\","
#endif
                    "\"rfm_pass\":\"%s\""
                    "}\n",
#ifdef NTP_CLIENT
                    ntp_server,
#endif
#ifdef MQTT
                    mqtt_server, mqtt_port, mqtt_user, mqtt_pass, mqtt_topic_prefix,
#endif
                    rfm_pass_hex);

        DBG("Configuration written: {"
#ifdef NTP_CLIENT
            "\"ntp_server\":\"%s\","
#endif
#ifdef MQTT
            "\"mqtt_server\":\"%s\","
            "\"mqtt_port\":%s,"
            "\"mqtt_user\":\"%s\","
            "\"mqtt_pass\":\"%s\","
            "\"mqtt_topic_prefix\":\"%s\","
#endif
            "\"rfm_pass\":\"%s\""
            "}",
#ifdef NTP_CLIENT
            ntp_server,
#endif
#ifdef MQTT
            mqtt_server, mqtt_port, mqtt_user, mqtt_pass, mqtt_topic_prefix,
#endif
            rfm_pass_hex);

        file.close();
        SPIFFS.end();
    }

    return true;
}

bool ICACHE_FLASH_ATTR Config::load(const char *filename)
{
    const char *JSON_STRING;
    String json;

    DBG("Reading config");

    SPIFFS.begin();

    File file = SPIFFS.open(filename, "r");
    if (file)
    {
        json = file.readStringUntil('\n');
        file.close();
        SPIFFS.end();
    }
    else
    {
        ERR(CFG_NOT_FOUND);
        SPIFFS.end();
        return false;
    }

    DBG("Parsing JSON");
    JSON_STRING = json.c_str();
    int i;
    int r;
    jsmn_parser p;
    jsmntok_t t[32]; /* We expect no more than 32 tokens */

    jsmn_init(&p);
    r = jsmn_parse(&p, JSON_STRING, strlen(JSON_STRING), t, sizeof(t) / sizeof(t[0]));
    if (r < 0)
    {
        ERR_ARG(CFG_FAILED_TO_PARSE, r);
        return false;
    }
    /* Assume the top-level element is an object */
    if (r < 1 || t[0].type != JSMN_OBJECT)
    {
        ERR(CFG_INVALID_JSON);
        return false;
    }

    /* Loop over all keys of the root object */
    for (i = 1; i < r; i++)
    {
        uint16_t offset = t[i + 1].start;
        uint16_t len = t[i + 1].end - t[i + 1].start;
        if (jsoneq(JSON_STRING, &t[i], "rfm_pass"))
        {
            if (len != 16)
            {
                ERR(CFG_MALFORMED_RFM_PASSWORD);
                return false;
            }

            strncpy(rfm_pass_hex, JSON_STRING + offset, len);

            i++;
        }
#ifdef NTP_CLIENT
        else if (jsoneq(JSON_STRING, &t[i], "ntp_server"))
        {
            strncpy(ntp_server, JSON_STRING + offset, len);
            ntp_server[len] = 0;
            i++;
        }
#endif
#ifdef MQTT
        else if (jsoneq(JSON_STRING, &t[i], "mqtt_server"))
        {
            strncpy(mqtt_server, JSON_STRING + offset, len);
            mqtt_server[len] = 0;

            i++;
        }
        else if (jsoneq(JSON_STRING, &t[i], "mqtt_port"))
        {
            strncpy(mqtt_port, JSON_STRING + offset, len);
            mqtt_port[len] = 0;

            i++;
        }
        else if (jsoneq(JSON_STRING, &t[i], "mqtt_user"))
        {
            strncpy(mqtt_user, JSON_STRING + offset, len);
            mqtt_user[len] = 0;
            i++;
        }
        else if (jsoneq(JSON_STRING, &t[i], "mqtt_pass"))
        {
            strncpy(mqtt_pass, JSON_STRING + offset, len);
            mqtt_pass[len] = 0;
            i++;
        }
        else if (jsoneq(JSON_STRING, &t[i], "mqtt_topic_prefix"))
        {
            strncpy(mqtt_topic_prefix, JSON_STRING + offset, len);
            mqtt_topic_prefix[len] = 0;
            i++;
        }
#endif
        else
        {
            DBG("Unexpected key: %.*s", len, JSON_STRING + offset);
        }
    }
    DBG("Configuration loaded");
    return true;
}

} // namespace hr20
