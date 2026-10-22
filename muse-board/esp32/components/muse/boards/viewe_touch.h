/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Packet layouts in VIEWE's CST9217 and CST816S drivers. Reject malformed
 * coordinates instead of sending a phantom press into the settings UI. */
static inline bool viewe_touch_decode(uint8_t address, const uint8_t *p, int *x, int *y)
{
    if (address == 0x5a) {
        if ((p[5] & 15) != 1) return false;
        *x = (p[1] << 4) | (p[3] >> 4);
        *y = (p[2] << 4) | (p[3] & 15);
    } else if (address == 0x15) {
        if ((p[0] & 15) != 1 || (p[1] >> 6) == 1) return false;
        *x = ((p[1] & 15) << 8) | p[2];
        *y = ((p[3] & 15) << 8) | p[4];
    } else {
        return false;
    }
    return *x < 466 && *y < 466;
}
