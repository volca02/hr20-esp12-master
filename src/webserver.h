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


namespace hr20 {

struct WebServer {
    WebServer(HR20Master &master) : server(80), master(master) {}

    ICACHE_FLASH_ATTR void begin() {
        server.on("/list", [&]() { handle_list(); } );

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
            js::Object main(result);

            // iterate all clients
            for (unsigned i = 0; i < hr20::MAX_HR_COUNT; ++i) {
                auto m = master.model[i];

                if (!m) continue;
                if (m->last_contact == 0) continue;

                // append a key for this client
                main.key(i);
                // append value for this object
                append_client_attr(result, m);
            }

        } // closes the curly brace

        // compose a json list of all visible clients
        server.send(200, "application/javascript", result);
    }

    ICACHE_FLASH_ATTR void append_client_attr(String &str, const HR20 *client) {
        js::Object obj(str);

        // attributes follow.
        obj.key("temp");
        js::str(str, client->temp_avg.to_str());
    }

    ICACHE_FLASH_ATTR void update() {
        server.handleClient();
    }

protected:
    ESP8266WebServer server;
    HR20Master &master;
};

} // namespace hr20
