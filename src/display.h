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

#include <Wire.h>
#include <SSD1306Wire.h>

namespace hr20 {

// FWD
struct HR20Master;

struct Display {
    Display(const HR20Master &master)
        : master(master)
        , display(0x3c, /*SDA*/D2, /*SCL*/D1)
    {}

    void begin() {
        display.init();
        display.setFont(ArialMT_Plain_10);
    }


    void update() {
        display.setTextAlignment(TEXT_ALIGN_LEFT);
        display.setFont(ArialMT_Plain_10);
        display.drawString(0, 0, "HR20 Master");
    }

protected:
    const HR20Master &master;
    SSD1306Wire display;
};


} // namespace hr20
