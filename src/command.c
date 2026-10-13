/* SPDX-License-Identifier: MPL-2.0 */
#include "catalog.h"

BOOL setCatalogMaster(struct PciCatalog* catalog, struct pci_dev* board)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!device) {
        return FALSE;
    }

    ULONG command;
    ULONG slot = device->info.function.device;

    // Read live state, not the discovery snapshot or the caller's public shadow.
    // Word access excludes the adjacent write-one-to-clear PCI status register.
    if (!readConfig(device->config, slot, 4, 2, &command) ||
        !writeConfig(device->config, slot, 4, 2, command | 4) ||
        !readConfig(device->config, slot, 4, 2, &command)) {
        return FALSE;
    }

    device->board.master = command;

    return TRUE;
}

BOOL checkCatalogIntxMask(struct PciCatalog* catalog, struct pci_dev* board, BOOL mask)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!device) {
        return FALSE;
    }

    ULONG snapshot;
    ULONG slot = device->info.function.device;

    // Sample the command and interrupt status together. Only the command word
    // is writable here: status contains write-one-to-clear error indications.
    if (!readConfig(device->config, slot, 4, 4, &snapshot)) {
        return FALSE;
    }

    BOOL pending = (snapshot & 0x00080000UL) != 0;

    if (pending != (mask != FALSE)) {
        return FALSE;
    }

    UWORD command = snapshot;
    UWORD requested = mask ? command | 0x400 : command & ~0x400;

    if (command == requested) {
        return TRUE;
    }

    ULONG actual;

    return writeConfig(device->config, slot, 4, 2, requested) &&
        readConfig(device->config, slot, 4, 2, &actual) && ((actual ^ requested) & 0x400) == 0;
}
