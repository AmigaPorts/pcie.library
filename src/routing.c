/* SPDX-License-Identifier: MPL-2.0 */
#include "interrupt.h"
#include "catalog.h"

// Build a temporary parent map from this library's catalog and ECAM reads.
// There are exactly 256 possible bus numbers. No device pointers, emulator
// services, allocations or configuration writes are needed for the walk.
static BOOL readParents(const struct PciCatalog* catalog, const struct PciConfig* config,
    UWORD parents[256], UBYTE ends[256])
{
    for (unsigned bus = 0; bus < 256; ++bus) {
        parents[bus] = 65535;
        ends[bus] = 0;
    }

    for (const struct PciDevice* bridge = catalog->first; bridge; bridge = bridge->next) {
        unsigned header = bridge->info.function.header_type & 127;

        if (bridge->config != config || (header != 1 && header != 2)) {
            continue;
        }

        ULONG numbers, device = bridge->info.function.device;

        if (!readConfig(config, device, 24, 4, &numbers)) {
            return FALSE;
        }

        unsigned primary = numbers & 255, secondary = (numbers >> 8) & 255, subordinate = (numbers >> 16) & 255;

        if (!secondary && !subordinate) {
            continue;
        }

        if (primary != (device >> 8) || primary < config->first_bus || primary >= secondary ||
            secondary > subordinate || subordinate > config->last_bus || parents[secondary] != 65535) {
            return FALSE;
        }

        parents[secondary] = device;
        ends[secondary] = subordinate;
    }

    // In increasing bus order, a new bridge must be under the innermost
    // currently forwarding interval. This rejects overlapping sibling windows,
    // missing parents and child windows escaping their ancestor's decode.
    unsigned parent = config->first_bus;

    ends[parent] = config->last_bus;

    for (unsigned bus = parent + 1; bus <= config->last_bus; ++bus) {
        if (parents[bus] == 65535) {
            continue;
        }

        while (parent != config->first_bus && bus > ends[parent]) {
            parent = parents[parent] >> 8;
        }

        if ((parents[bus] >> 8) != parent || ends[bus] > ends[parent]) {
            return FALSE;
        }

        parent = bus;
    }

    return TRUE;
}

BOOL resolveDeviceInterrupt(APTR tree_base, APTR root, APTR bridge, const struct PciCatalog* catalog,
    const struct PciDevice* device, struct DtInterrupt* result)
{
    if (!catalog || !device || !result) {
        return FALSE;
    }

    const struct PciDevice* entry = catalog->first;

    while (entry && entry != device) {
        entry = entry->next;
    }

    if (!entry || !entry->config || entry->config->first_bus > entry->config->last_bus) {
        return FALSE;
    }

    const struct PciConfig* config = entry->config;
    ULONG current = entry->info.function.device, pin = entry->info.interrupt_pin;

    if (current > 65535 || (current >> 8) < config->first_bus || (current >> 8) > config->last_bus ||
        pin < 1 || pin > 4) {
        return FALSE;
    }

    UWORD parents[256];
    UBYTE ends[256];
    BOOL have_parents = FALSE;

    for (;;) {
        struct DtInterrupt pending;
        enum DtInterruptStatus status = lookupPciInterrupt(tree_base, root, bridge, current, pin, &pending);

        if (status == DT_INTERRUPT_FOUND) {
            *result = pending;

            return TRUE;
        }

        unsigned bus = current >> 8;

        if (status != DT_INTERRUPT_MISSING || bus == config->first_bus) {
            return FALSE;
        }

        if (!have_parents) {
            if (!readParents(catalog, config, parents, ends)) {
                return FALSE;
            }

            have_parents = TRUE;
        }

        if (parents[bus] == 65535) {
            return FALSE;
        }

        pin = ((pin - 1 + ((current >> 3) & 31)) & 3) + 1;
        current = parents[bus];

        // Every validated parent has a strictly lower bus number.
    }
}
