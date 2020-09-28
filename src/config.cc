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

} // namespace hr20
