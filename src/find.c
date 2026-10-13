/* SPDX-License-Identifier: MPL-2.0 */
#include "host.h"

struct PciDevice* findCatalogDevice(const struct PciCatalog* catalog, const struct PciDevice* previous,
    const struct PciMatch* match)
{
    if (!catalog || !match || (match->fields & ~15UL) ||
        ((match->fields & PCI_MATCH_CLASS) && match->class_code > 0xffffff) ||
        ((match->fields & PCI_MATCH_SLOT) && match->slot > 65535)) {
        return NULL;
    }

    struct PciDevice* device = catalog->first;

    if (previous) {
        while (device && device != previous) {
            device = device->next;
        }

        if (!device) {
            return NULL;
        }

        device = device->next;
    }

    for (; device; device = device->next) {
        const struct PciFunction* function = &device->info.function;

        if ((match->fields & PCI_MATCH_VENDOR) && function->vendor_id != match->vendor) {
            continue;
        }

        if ((match->fields & PCI_MATCH_DEVICE) && function->device_id != match->device) {
            continue;
        }

        if ((match->fields & PCI_MATCH_CLASS) && function->class_code != match->class_code) {
            continue;
        }

        if ((match->fields & PCI_MATCH_SLOT) && function->device != match->slot) {
            continue;
        }

        return device;
    }

    return NULL;
}

struct PciDevice* catalogDevice(const struct PciCatalog* catalog, const struct pci_dev* board)
{
    if (!catalog || !board) {
        return NULL;
    }

    for (struct PciDevice* device = catalog->first; device; device = device->next) {
        if (&device->board == board) {
            return device;
        }
    }

    return NULL;
}

BOOL setCatalogOwner(struct PciCatalog* catalog, struct pci_dev* board, struct Node* owner)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!device || (owner && device->board.owner)) {
        return FALSE;
    }

    device->board.owner = owner;

    return TRUE;
}

struct PciDmaMemory* hostDmaMemory(struct PciHosts* hosts, const struct pci_dev* board)
{
    if (!hosts) {
        return NULL;
    }

    struct PciDevice* device = catalogDevice(&hosts->catalog, board);

    if (!device) {
        return NULL;
    }

    for (struct PciHost* host = hosts->first; host; host = host->next) {
        if (device->config == &host->config) {
            return host->dma.ranges ? &host->dma : NULL;
        }
    }

    return NULL;
}
