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

#include "master.h"
#include "json.h"

namespace hr20 {

struct WebServer {
    WebServer(HR20Master &master) : server(80), master(master) {}

    ICACHE_FLASH_ATTR void begin() {
        server.on("/list", [&]() { handle_list(); } );
        server.on("/timer", [&]() { handle_timer(); } );
        server.on("/events", [&]() { handle_events(); } );

        server.on("/",
                  [&](){
                      String status = "Valves + temperatures\n\n";
                      unsigned cnt = 0;
                      for (unsigned i = 0; i < hr20::MAX_HR_COUNT; ++i) {
                          auto m = master.model[i];

                          if (!m) continue;
                          if (m->last_contact == 0) continue;


                          ++cnt;
                          status += "Valve ";
                          status += String(i, HEX);
                          status += " - ";
                          status += String(float(m->temp_avg.get_remote()) / 100.0f, 2);
                          status += String('\n');
                      }

                      status += String('\n');
                      status += String(cnt);
                      status += " total";

                      server.send(200, "text/plain", status);
                  });
        server.begin();
    }

    ICACHE_FLASH_ATTR void handle_list() {
        String result;

        {
            json::Object main(result);

            // iterate all clients
            for (unsigned i = 0; i < hr20::MAX_HR_COUNT; ++i) {
                auto m = master.model[i];

                if (!m) continue;
                if (m->last_contact == 0) continue;

                // append a key for this client
                main.key(i);
                // append value for this object
                json::append_client_attr(result, *m);
            }

        } // closes the curly brace

        // compose a json list of all visible clients
        server.send(200, "application/json", result);
    }

    ICACHE_FLASH_ATTR void handle_timer() {
        // did we get an argument?
        auto client = server.arg("client");

        int caddr = client.toInt();

        if (caddr == 0) {
            // compose a json list of all visible clients
            server.send(404, "text/plan", "Invalid client");
            return;
        }

        // try to fetch the client
        const auto &m = master.model[caddr];

        if (!m || m->last_contact == 0) {
            // compose a json list of all visible clients
            server.send(404, "text/plan", "Invalid client");
            return;
        }

        // okay we have a valid client
        String result;

        { // intentional brace to close the json before we send it
            json::Object obj(result);

            // spew the whole timer table
            for (uint8_t idx = 0; idx < TIMER_DAYS; ++idx) {
                obj.key(idx);
                json::append_timer_day(result, *m, idx);
            }
        }

        // compose a json list of all visible clients
        server.send(200, "application/json", result);
    }

    ICACHE_FLASH_ATTR void handle_events() {
        String result;

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

        // compose a json list of all visible clients
        server.send(200, "application/json", result);
    }


    ICACHE_FLASH_ATTR void update() {
        server.handleClient();
    }

protected:
    ESP8266WebServer server;
    HR20Master &master;
};

} // namespace hr20
