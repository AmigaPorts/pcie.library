/* SPDX-License-Identifier: MPL-2.0 */
#include "msi.h"

BOOL maskMsiVector(const struct PciConfig* config, ULONG device, const struct PciMsi* capability,
    ULONG vector, BOOL mask)
{
    if (!capability || !(capability->control & 0x100) || !capability->vectors ||
        capability->vectors > 32 || (capability->vectors & (capability->vectors - 1)) ||
        vector >= capability->vectors || capability->offset < 0x40 || (capability->offset & 3)) {
        return FALSE;
    }

    unsigned relative = capability->control & 0x80 ? 16 : 12;

    if (capability->offset > 256 - relative - 8 ||
        capability->mask_offset != capability->offset + relative) {
        return FALSE;
    }

    ULONG current, actual;
    ULONG bit = 1UL << vector;

    if (!readConfig(config, device, capability->mask_offset, 4, &current)) {
        return FALSE;
    }

    ULONG requested = mask ? current | bit : current & ~bit;

    if (requested == current) {
        return TRUE;
    }

    return writeConfig(config, device, capability->mask_offset, 4, requested) &&
        readConfig(config, device, capability->mask_offset, 4, &actual) && ((actual ^ requested) & bit) == 0;
}

static uint32_t littleLong(uint32_t value)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return __builtin_bswap32(value);
#else
    return value;
#endif
}

BOOL maskMsixVector(const struct PciMsixTables* tables, ULONG vector, BOOL mask)
{
    if (!tables || !tables->table || (tables->table & 7) || !tables->table_size ||
        (tables->table_size & 15) || tables->table_size > 2048 * 16 ||
        vector >= tables->table_size / 16 ||
        (uint64_t)tables->table + tables->table_size > UINT64_C(0x100000000)) {
        return FALSE;
    }

    volatile uint32_t* control = (volatile uint32_t*)(uintptr_t)(tables->table + vector * 16 + 12);
    uint32_t current = littleLong(*control);
    uint32_t requested = mask ? current | 1 : current & ~UINT32_C(1);

    if (requested == current) {
        return TRUE;
    }

    *control = littleLong(requested);

    // A read of the same register completes the posted write before returning.
    return ((littleLong(*control) ^ requested) & 1) == 0;
}
