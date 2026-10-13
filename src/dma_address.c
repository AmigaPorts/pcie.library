/* SPDX-License-Identifier: MPL-2.0 */
#include "dma.h"

BOOL validateDmaRanges(const struct PciDmaRange* ranges, ULONG count)
{
    if (!ranges || !count) {
        return FALSE;
    }

    for (ULONG i = 0; i < count; ++i) {
        const struct PciDmaRange* range = ranges + i;

        if (!range->logical || !range->size || range->size - 1 > UINT32_MAX - range->logical ||
            range->size - 1 > UINT64_MAX - range->bus) {
            return FALSE;
        }

        for (ULONG j = 0; j < i; ++j) {
            const struct PciDmaRange* other = ranges + j;

            if ((range->logical >= other->logical ? range->logical - other->logical < other->size :
                other->logical - range->logical < range->size) ||
                (range->bus >= other->bus ? range->bus - other->bus < other->size :
                other->bus - range->bus < range->size)) {
                return FALSE;
            }
        }
    }

    return TRUE;
}

BOOL dmaToBus(const struct PciDmaRange* ranges, ULONG count, ULONG address, ULONG size, uint64_t* result)
{
    if (!result || !size || !validateDmaRanges(ranges, count)) {
        return FALSE;
    }

    for (ULONG i = 0; i < count; ++i) {
        const struct PciDmaRange* range = ranges + i;

        if (address >= range->logical && address - range->logical < range->size &&
            size <= range->size - (address - range->logical)) {
            *result = range->bus + (address - range->logical);

            return TRUE;
        }
    }

    return FALSE;
}

BOOL dmaToLogical(const struct PciDmaRange* ranges, ULONG count, uint64_t address, ULONG size, ULONG* result)
{
    if (!result || !size || !validateDmaRanges(ranges, count)) {
        return FALSE;
    }

    for (ULONG i = 0; i < count; ++i) {
        const struct PciDmaRange* range = ranges + i;

        if (address >= range->bus && address - range->bus < range->size &&
            size <= range->size - (address - range->bus)) {
            *result = range->logical + (ULONG)(address - range->bus);

            return TRUE;
        }
    }

    return FALSE;
}
