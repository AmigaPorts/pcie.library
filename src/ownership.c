/* SPDX-License-Identifier: MPL-2.0 */
#include "catalog.h"

#define __NOLIBBASE__
#include <proto/exec.h>

BOOL obtainCatalogCard(struct ExecBase* SysBase, struct PciCatalog* catalog, struct pci_dev* board)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!device || device->board.owner) {
        return FALSE;
    }

    device->board.owner = (struct Node*)FindTask(NULL);

    return TRUE;
}

void releaseCatalogCard(struct ExecBase* SysBase, struct PciCatalog* catalog, struct pci_dev* board)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (device && device->board.owner == (struct Node*)FindTask(NULL)) {
        device->board.owner = NULL;
    }
}
