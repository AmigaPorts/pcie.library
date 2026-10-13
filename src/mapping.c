/* SPDX-License-Identifier: MPL-2.0 */
#include "mapping.h"

static BOOL sameBar(const struct PciBar* first, const struct PciBar* second)
{
    return first->config == second->config && first->mask == second->mask &&
        first->address == second->address && first->size == second->size;
}

BOOL translateResources(APTR tree_base, APTR bridge, const struct PciResources* resources,
    struct PciMappings* mappings)
{
    if (!tree_base || !bridge || !resources || !mappings ||
        (resources->count != 2 && resources->count != 6)) {
        return FALSE;
    }

    struct PciMappings result = {0};
    const struct PciBar empty = {0};

    for (unsigned i = 0; i < 6; ++i) {
        const struct PciBar* bar = &resources->bars[i];

        if (i >= resources->count) {
            if (!sameBar(bar, &empty)) {
                return FALSE;
            }

            continue;
        }

        struct PciBar decoded;

        if (!decodeBar(bar->config, bar->config >> 32, bar->mask, bar->mask >> 32, &decoded) ||
            !sameBar(bar, &decoded)) {
            return FALSE;
        }

        enum PciAddressSpace space = bar->config & 1 ? PCI_IO : (bar->config & 6) == 4 ? PCI_MEMORY64
                                                                                       : PCI_MEMORY32;

        if (bar->size && (!translatePciAddress(tree_base, bridge, space, bar->address, bar->size, &result.bars[i]) ||
            !result.bars[i])) {
            return FALSE;
        }

        if (space == PCI_MEMORY64) {
            if (i + 1 == resources->count || !sameBar(&resources->bars[i + 1], &empty)) {
                return FALSE;
            }

            ++i;
        }
    }

    const struct PciBar* rom = &resources->rom;
    struct PciBar decoded;

    if (!decodeRomBar(rom->config, rom->mask, &decoded) || !sameBar(rom, &decoded)) {
        return FALSE;
    }

    // A disabled expansion ROM decodes no address. Firmware may leave its
    // last sizing value in the BAR, outside every assigned host window. Keep
    // the raw metadata, but do not require or publish a mapping until enabled.
    if (rom->size && (rom->config & 1) && (!translatePciAddress(tree_base, bridge, PCI_MEMORY32, rom->address, rom->size, &result.rom) ||
        !result.rom)) {
        return FALSE;
    }

    *mappings = result;

    return TRUE;
}
