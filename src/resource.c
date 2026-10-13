/* SPDX-License-Identifier: MPL-2.0 */
#include "resource.h"

int decodeBar(uint32_t low, uint32_t high, uint32_t low_mask,
    uint32_t high_mask, struct PciBar* bar)
{
    if (!bar) {
        return 0;
    }

    struct PciBar result = {0};

    result.config = low;
    result.mask = low_mask;

    unsigned attributes = low & 1 ? 3 : 15;

    if ((low & attributes) != (low_mask & attributes) || (low & 3) == 3) {
        return 0;
    }

    uint64_t address_mask = UINT32_C(0xfffffff0);
    uint64_t limit = UINT32_MAX;

    if (low & 1) {
        address_mask = UINT32_C(0xfffffffc);

        // Legacy I/O BARs can implement only the low 16 address bits.
        if (!(low_mask & 0xffff0000)) {
            limit = UINT16_MAX;
            address_mask &= limit;
        }

        if (low & ~limit) {
            return 0;
        }
    } else {
        switch (low & 6) {
            case 0:
                break;
            case 2:
                limit = 0xfffff;
                address_mask &= limit;

                if (low & ~limit) {
                    return 0;
                }

                break;
            case 4:
                result.config |= (uint64_t)high << 32;
                result.mask |= (uint64_t)high_mask << 32;
                address_mask = UINT64_MAX & ~UINT64_C(15);
                limit = UINT64_MAX;
                break;
            default:
                return 0;
        }
    }

    result.address = result.config & address_mask;

    uint64_t mask = result.mask & address_mask;

    if (mask || (low_mask & 7)) {
        result.size = ((~mask) & limit) + 1;

        if (!result.size || (result.size & (result.size - 1)) ||
            (result.address & (result.size - 1))) {
            return 0;
        }
    } else if (result.address) {
        return 0;
    }

    *bar = result;

    return 1;
}

int decodeRomBar(uint32_t value, uint32_t mask, struct PciBar* bar)
{
    if (!bar) {
        return 0;
    }

    struct PciBar result = {0};

    result.config = value;
    result.mask = mask;
    result.address = value & UINT32_C(0xfffff800);

    uint32_t address_mask = mask & UINT32_C(0xfffff800);

    if (address_mask) {
        result.size = (uint64_t)(~address_mask) + 1;

        if ((result.size & (result.size - 1)) || (result.address & (result.size - 1))) {
            return 0;
        }
    } else if (result.address) {
        return 0;
    }

    *bar = result;

    return 1;
}
