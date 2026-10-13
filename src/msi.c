/* SPDX-License-Identifier: MPL-2.0 */
#include "msi.h"

BOOL readMsiCapability(const struct PciConfig* config, ULONG device, struct PciMsi* result)
{
    struct PciMsi value = {0};

    if (!result || !readCapability(config, device, 5, &value.offset)) {
        return FALSE;
    }

    if (value.offset) {
        ULONG control, low, high = 0, data;

        if (!readConfig(config, device, value.offset + 2, 2, &control)) {
            return FALSE;
        }

        unsigned supported = (control >> 1) & 7;
        unsigned enabled = (control >> 4) & 7;

        if (supported > 5 || enabled > supported) {
            return FALSE;
        }

        BOOL wide = (control & 0x80) != 0;
        BOOL masking = (control & 0x100) != 0;
        unsigned length = masking ? (wide ? 24 : 20) : (wide ? 14 : 10);

        if (value.offset > 256 - length) {
            return FALSE;
        }

        value.control = control;
        value.vectors = 1UL << supported;
        value.data_offset = value.offset + (wide ? 12 : 8);

        if (!readConfig(config, device, value.offset + 4, 4, &low) ||
            (wide && !readConfig(config, device, value.offset + 8, 4, &high)) ||
            !readConfig(config, device, value.data_offset, 2, &data)) {
            return FALSE;
        }

        value.address = ((uint64_t)high << 32) | low;
        value.data = data;

        if (masking) {
            value.mask_offset = value.data_offset + 4;
            value.pending_offset = value.mask_offset + 4;

            if (!readConfig(config, device, value.mask_offset, 4, &value.mask) ||
                !readConfig(config, device, value.pending_offset, 4, &value.pending)) {
                return FALSE;
            }
        }
    }

    *result = value;

    return TRUE;
}

BOOL readMsixCapability(const struct PciConfig* config, ULONG device, struct PciMsix* result)
{
    struct PciMsix value = {0};

    if (!result || !readCapability(config, device, 0x11, &value.offset)) {
        return FALSE;
    }

    if (value.offset) {
        ULONG control, table, pending;

        if (value.offset > 244 || !readConfig(config, device, value.offset + 2, 2, &control) ||
            !readConfig(config, device, value.offset + 4, 4, &table) ||
            !readConfig(config, device, value.offset + 8, 4, &pending)) {
            return FALSE;
        }

        value.control = control;
        value.vectors = (control & 0x7ff) + 1;
        value.table_bar = table & 7;
        value.pending_bar = pending & 7;

        if (value.table_bar > 5 || value.pending_bar > 5) {
            return FALSE;
        }

        value.table_offset = table & ~7UL;
        value.pending_offset = pending & ~7UL;
    }

    *result = value;

    return TRUE;
}

static BOOL tableAddress(const struct PciResources* resources, const struct PciMappings* mappings,
    unsigned index, ULONG offset, ULONG size, ULONG* result)
{
    if (index >= resources->count || (offset & 7) || !mappings->bars[index]) {
        return FALSE;
    }

    // Walk complete BARs so a BIR cannot select the upper DWORD of a 64-bit BAR.
    for (unsigned i = 0; i < index; ++i) {
        if (!(resources->bars[i].config & 1) && (resources->bars[i].config & 6) == 4) {
            if (++i == index) {
                return FALSE;
            }
        }
    }

    const struct PciBar* bar = &resources->bars[index];

    if ((bar->config & 1) || offset > bar->size || size > bar->size - offset) {
        return FALSE;
    }

    uint64_t address = (uint64_t)mappings->bars[index] + offset;

    if (address + size > UINT64_C(0x100000000)) {
        return FALSE;
    }

    *result = address;

    return TRUE;
}

BOOL resolveMsixTables(const struct PciResources* resources, const struct PciMappings* mappings,
    const struct PciMsix* capability, struct PciMsixTables* result)
{
    if (!resources || !mappings || !capability || !result ||
        (resources->count != 2 && resources->count != 6) ||
        capability->offset < 0x40 || capability->offset > 244 || (capability->offset & 3) ||
        !capability->vectors || capability->vectors > 2048) {
        return FALSE;
    }

    struct PciMsixTables value;

    value.table_size = capability->vectors * 16;
    value.pending_size = ((capability->vectors + 63) / 64) * 8;

    if (!tableAddress(resources, mappings, capability->table_bar, capability->table_offset,
            value.table_size, &value.table) ||
        !tableAddress(resources, mappings, capability->pending_bar, capability->pending_offset,
            value.pending_size, &value.pending)) {
        return FALSE;
    }

    if ((uint64_t)value.table < (uint64_t)value.pending + value.pending_size &&
        (uint64_t)value.pending < (uint64_t)value.table + value.table_size) {
        return FALSE;
    }

    *result = value;

    return TRUE;
}
