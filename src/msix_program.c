/* SPDX-License-Identifier: MPL-2.0 */
#include "msi.h"
#include <libraries/interrupt.h>

static uint32_t littleLong(uint32_t value)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return __builtin_bswap32(value);
#else
    return value;
#endif
}

BOOL programMsixVector(const struct PciMsixTables* tables, ULONG vector, const struct InterruptMessage* message)
{
    if (!message || (message->address_low & 3) || !tables || !tables->table || (tables->table & 7) ||
        !tables->table_size || (tables->table_size & 15) || tables->table_size > 2048 * 16 ||
        vector >= tables->table_size / 16 ||
        (uint64_t)tables->table + tables->table_size > UINT64_C(0x100000000)) {
        return FALSE;
    }

    volatile uint32_t* entry = (volatile uint32_t*)(uintptr_t)(tables->table + vector * 16);

    if (!(littleLong(entry[3]) & 1)) {
        return FALSE;
    }

    // Each entry has an independent full-width message. Leave its mask and all
    // neighboring entries/PBA bits untouched; readback completes posted writes.
    entry[0] = littleLong(message->address_low);
    entry[1] = littleLong(message->address_high);
    entry[2] = littleLong(message->data);

    return littleLong(entry[0]) == message->address_low && littleLong(entry[1]) == message->address_high &&
        littleLong(entry[2]) == message->data;
}

BOOL enableMsix(const struct PciConfig* config, ULONG device, const struct PciMsix* capability,
    BOOL enabled, BOOL masked)
{
    if (!capability || capability->offset < 0x40 || capability->offset > 244 || (capability->offset & 3) ||
        !capability->vectors || capability->vectors > 2048 ||
        capability->vectors != (capability->control & 0x7ffUL) + 1) {
        return FALSE;
    }

    ULONG control, actual;

    if (!readConfig(config, device, capability->offset + 2, 2, &control) ||
        (control & 0x7ff) != capability->vectors - 1) {
        return FALSE;
    }

    ULONG requested = (control & ~0xc000UL) | (enabled ? 0x8000 : 0) | (masked ? 0x4000 : 0);

    if (requested == control) {
        return TRUE;
    }

    return writeConfig(config, device, capability->offset + 2, 2, requested) &&
        readConfig(config, device, capability->offset + 2, 2, &actual) && actual == requested;
}
