/* SPDX-License-Identifier: MPL-2.0 */
#include "attributes.h"

#include <stdint.h>
#include <libraries/pcitags.h>

// Attribute widths are part of the existing tag-search ABI. Location fields
// compare the entire ULONG; identity and class components narrow their input.
static BOOL matchesAttribute(const struct PciDevice* device, ULONG tag, ULONG value)
{
    const struct PciFunction* function = &device->info.function;
    ULONG actual, mask = 0xffffffffUL;

    switch (tag) {
        case PRM_Vendor:
            actual = device->board.vendor;
            mask = 0xffff;
            break;
        case PRM_Device:
            actual = device->board.device;
            mask = 0xffff;
            break;
        case PRM_Class:
            actual = device->board.devclass >> 16;
            mask = 0xff;
            break;
        case PRM_SubClass:
            actual = device->board.devclass >> 8;
            mask = 0xff;
            break;
        case PRM_Interface:
            actual = device->board.devclass;
            mask = 0xff;
            break;
        case PRM_Revision:
            actual = function->revision;
            mask = 0xff;
            break;
        case PRM_SubsysVendor:
            actual = device->info.subsystem_vendor;
            mask = 0xffff;
            break;
        case PRM_SubsysID:
            actual = device->info.subsystem_device;
            mask = 0xffff;
            break;
        case PRM_BusNumber:
            actual = function->device >> 8;
            break;
        case PRM_SlotNumber:
            actual = device->board.devfn >> 3;
            break;
        case PRM_FunctionNumber:
            actual = device->board.devfn & 7;
            break;

        // The reference never matches either memory-range filter.
        case PRM_MemRangeLower:
        case PRM_MemRangeUpper:
            return FALSE;
        default:
            return TRUE;
    }

    return (actual & mask) == (value & mask);
}

struct pci_dev* findCatalogBoard(const struct PciCatalog* catalog, const struct pci_dev* previous,
    const struct TagItem* tags)
{
    if (!catalog || !tags) {
        return NULL;
    }

    struct PciDevice* device = previous ? catalogDevice(catalog, previous) : catalog->first;

    if (previous && device) {
        device = device->next;
    }

    while (device) {
        const struct TagItem* item = tags;

        while (item->ti_Tag != TAG_DONE && matchesAttribute(device, item->ti_Tag, item->ti_Data)) {
            ++item;
        }

        if (item->ti_Tag == TAG_DONE) {
            return &device->board;
        }

        device = device->next;
    }

    return NULL;
}

BOOL setCatalogAttrs(struct PciCatalog* catalog, struct pci_dev* board, const struct TagItem* tags)
{
    if (!tags || !catalogDevice(catalog, board)) {
        return FALSE;
    }

    BOOL success = TRUE;

    for (const struct TagItem* item = tags; item->ti_Tag != TAG_DONE; ++item) {
        if (item->ti_Tag == TAG_IGNORE) {
            continue;
        }

        if (item->ti_Tag != PRM_BoardOwner ||
            !setCatalogOwner(catalog, board, (struct Node*)(uintptr_t)item->ti_Data)) {
            success = FALSE;
        }
    }

    return success;
}
