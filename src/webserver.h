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

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <FS.h>

#include "master.h"
#include "json.h"

#define LIST_MAX_SIZE (32*140)
#define TIMER_MAX_SIZE (32*8*8)
#define EVENT_MAX_SIZE (100*MAX_JSON_EVENTS)

namespace hr20 {

#define JSON200 "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n"
#define JSON200_LEN 70

struct WebServer {
    WebServer(HR20Master &master) : server(80), master(master) {}

    ICACHE_FLASH_ATTR void begin() {
        server.on("/list", [&]() { handle_list(); } );
        server.on("/timer", [&]() { handle_timer(); } );
        server.on("/events", [&]() { handle_events(); } );
        server.serveStatic("/", SPIFFS, "/index.html");
        server.begin();
    }

    ICACHE_FLASH_ATTR void handle_list() {
        // we need 32*140 for client list
        static BufferHolder<LIST_MAX_SIZE> buf;
        StrMaker result(buf);
        {
            json::Object main(result);

            // iterate all clients
            for (unsigned i = 0; i < hr20::MAX_HR_ADDR; ++i) {
                auto m = master.model[i];

                if (!m) continue;
                if (m->last_contact == 0) continue;

                // append a key for this client
                main.key(i);
                // append value for this object
                json::append_client_attr(result, *m);
            }

        } // closes the curly brace

        result += "\r\n";

        // compose a json list of all visible clients
        // send header and content separately
        server.sendContent_P(JSON200, JSON200_LEN);
        server.sendContent_P(result.data(), result.size());
    }

    ICACHE_FLASH_ATTR void handle_timer() {
        // WE NEED 32*8*8 for timers
        static BufferHolder<TIMER_MAX_SIZE> buf;
        StrMaker result(buf);
        // did we get an argument?
        auto client = server.arg("client");

        int caddr = client.toInt();

        if (caddr == 0) {
            server.send_P(404, "text/plain", "Invalid client");
            return;
        }

        // try to fetch the client
        const auto &m = master.model[caddr];

        if (!m || m->last_contact == 0) {
            server.send_P(404, "text/plain", "Invalid client");
            return;
        }

        { // intentional brace to close the json before we send it
            json::Object obj(result);

            // spew the whole timer table
            for (uint8_t idx = 0; idx < TIMER_DAYS; ++idx) {
                obj.key(idx);
                json::append_timer_day(result, *m, idx);
            }
        }

        result += "\r\n";

        server.sendContent_P(JSON200, JSON200_LEN);
        server.sendContent_P(result.data(), result.size());
    }

    ICACHE_FLASH_ATTR void handle_events() {
        static BufferHolder<EVENT_MAX_SIZE> buf;
        StrMaker result(buf);

        auto soffset = server.arg("offset");
        unsigned offset   = std::max(0L, soffset.toInt());
        unsigned counter  = MAX_JSON_EVENTS;
        {
            json::Object main(result);

            main.key("events");
            json::Array arr(main);

            // iterate all clients
            for (const auto &event : eventLog) {
                if (offset != 0) {
                    --offset;
                    continue;
                }

                if (!counter) break;
                --counter;

                // skip empty events
                if (event.time == 0) continue;

                // a comma is inserted unless this is first element
                arr.element();

                // append value itself
                json::append_event(result, event);
            }

        } // closes the curly brace

        result += "\r\n";

        server.sendContent_P(JSON200, JSON200_LEN);
        server.sendContent_P(result.data(), result.size());
    }


    ICACHE_FLASH_ATTR void update() {
        server.handleClient();
    }

protected:
    ESP8266WebServer server;
    HR20Master &master;
};

} // namespace hr20
