/* SPDX-License-Identifier: MPL-2.0 */
#include "intx.h"
#include <exec/libraries.h>
#include <libraries/pci_constants.h>
#include <libraries/bcmpcie_errors.h>

LONG allocateIntx(struct Library* interrupt_base, struct PciCatalog* catalog,
    struct pci_dev* board, ULONG minimum)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!device) {
        return PCIE_ERR_INVAL;
    }

    if (device->interrupt_set || device->interrupt_allocation || device->interrupt_type || device->interrupt_server) {
        return PCIE_ERR_BUSY;
    }

    if (minimum > 1) {
        return PCIE_ERR_INVAL;
    }

    if (!device->interrupt_prepared || !device->info.interrupt_pin || device->info.interrupt_pin > 4) {
        return PCIE_ERR_NODEV;
    }

    if (!interrupt_base || interrupt_base->lib_Version < 1 ||
        (interrupt_base->lib_Version == 1 && interrupt_base->lib_Revision < 9) ||
        interrupt_base->lib_NegSize < 156) {
        return PCIE_ERR_NOTSUPP;
    }

    device->interrupt_type = PCI_IRQ_INTX;
    device->interrupt_provider = interrupt_base;

    return 1;
}

LONG releaseIntx(struct PciCatalog* catalog, struct pci_dev* board)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!device) {
        return PCIE_ERR_INVAL;
    }

    if (device->interrupt_allocation || device->interrupt_server) {
        return PCIE_ERR_BUSY;
    }

    if (device->interrupt_type && device->interrupt_type != PCI_IRQ_INTX) {
        return PCIE_ERR_INVAL;
    }

    device->interrupt_type = 0;
    device->interrupt_provider = NULL;

    return PCIE_OK;
}

BOOL enableIntx(struct PciCatalog* catalog, struct pci_dev* board, BOOL enabled)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!device || device->interrupt_type != PCI_IRQ_INTX || !device->interrupt_provider ||
        (enabled && (!device->interrupt_server || device->interrupt_library != device->interrupt_provider))) {
        return FALSE;
    }

    ULONG command, actual;
    ULONG slot = device->info.function.device;

    if (!readConfig(device->config, slot, 4, 2, &command)) {
        return FALSE;
    }

    UWORD requested = enabled ? command & ~0x400 : command | 0x400;

    if ((UWORD)command == requested) {
        return TRUE;
    }

    return writeConfig(device->config, slot, 4, 2, requested) &&
        readConfig(device->config, slot, 4, 2, &actual) && ((actual ^ requested) & 0x400) == 0;
}

BOOL maskIntx(struct PciCatalog* catalog, struct pci_dev* board, ULONG vector, BOOL mask)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    return device && device->interrupt_type == PCI_IRQ_INTX && device->interrupt_provider && !vector &&
        checkCatalogIntxMask(catalog, board, mask);
}
