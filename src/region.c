/* SPDX-License-Identifier: MPL-2.0 */
#include "region.h"

static BOOL rangeFits(uint64_t base, uint64_t size, uint64_t limit)
{
    return size && base <= limit && size - 1 <= limit - base;
}

static BOOL locate(const struct PciCatalog* catalog, const struct pci_dev* board,
    uint64_t address, ULONG length, BOOL to_bus, BOOL memory_only, uint64_t* result)
{
    const struct PciDevice* device = catalogDevice(catalog, board);

    if (!device || !device->resources_prepared || !address ||
        (device->resources.count != 2 && device->resources.count != 6)) {
        return FALSE;
    }

    for (unsigned index = 0; index < device->resources.count; ++index) {
        const struct PciBar* bar = device->resources.bars + index;
        uint64_t logical = device->mappings.bars[index];

        if (!logical || (memory_only && (bar->config & 1)) ||
            !rangeFits(logical, bar->size, UINT32_MAX) ||
            !rangeFits(bar->address, bar->size, UINT64_MAX)) {
            continue;
        }

        uint64_t start = to_bus ? logical : bar->address;

        if (address < start) {
            continue;
        }

        uint64_t offset = address - start;

        if (offset > bar->size || length > bar->size - offset) {
            continue;
        }

        *result = (to_bus ? bar->address : logical) + offset;

        return TRUE;
    }

    return FALSE;
}

APTR obtainDeviceRegion(const struct PciCatalog* catalog, const struct pci_dev* board, ULONG region, ULONG size)
{
    uint64_t address;

    if (!locate(catalog, board, region, size, FALSE, TRUE, &address) || address > UINT32_MAX) {
        return NULL;
    }

    return (APTR)(uintptr_t)address;
}

BOOL deviceBarToBus(const struct PciCatalog* catalog, const struct pci_dev* board, ULONG address, uint64_t* result)
{
    return result && locate(catalog, board, address, 1, TRUE, FALSE, result);
}

BOOL deviceBarToLogical(const struct PciCatalog* catalog, const struct pci_dev* board, uint64_t address, ULONG* result)
{
    uint64_t logical;

    if (!result || !locate(catalog, board, address, 1, FALSE, FALSE, &logical)) {
        return FALSE;
    }

    *result = logical;

    return TRUE;
}
