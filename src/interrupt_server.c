/* SPDX-License-Identifier: MPL-2.0 */
#include "interrupt.h"
#include "catalog.h"
#include <exec/interrupts.h>
#include <exec/libraries.h>
#include <libraries/pci_constants.h>
#define __NOLIBBASE__
#include <proto/interrupt.h>

LONG bindDeviceInterrupt(struct Library* InterruptBase, struct PciCatalog* catalog,
    struct pci_dev* board, struct Interrupt* server)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!device || !server || !server->is_Code) {
        return GIC400_ERR_INVALID_ARGUMENT;
    }

    if (!InterruptBase || InterruptBase->lib_Version < 1 ||
        (InterruptBase->lib_Version == 1 && InterruptBase->lib_Revision < 9) ||
        InterruptBase->lib_NegSize < 156) {
        return GIC400_ERR_NOT_READY;
    }

    if (device->interrupt_type && device->interrupt_type != PCI_IRQ_INTX) {
        return GIC400_ERR_NOT_ROUTABLE;
    }

    if (device->interrupt_type && device->interrupt_provider != InterruptBase) {
        return GIC400_ERR_NOT_READY;
    }

    if (device->interrupt_allocation) {
        return GIC400_ERR_NOT_READY;
    }

    if (!device->interrupt_prepared) {
        return GIC400_ERR_NOT_READY;
    }

    if (!device->info.interrupt_pin || device->info.interrupt_pin > 4) {
        return GIC400_ERR_NOT_ROUTABLE;
    }

    if (device->interrupt_server) {
        return device->interrupt_server == server && device->interrupt_library == InterruptBase ?
            0 : GIC400_ERR_ALREADY_REGISTERED;
    }

    // The private resolved IRQ remains authoritative even if a caller changes
    // the writable public pci_dev. Polarity must be set before registration.
    LONG status = SetIntPolarity(device->interrupt_irq, device->interrupt_active_low);

    if (status) {
        return status;
    }

    status = AddIntServerEx(device->interrupt_irq, 0, device->interrupt_edge, server);

    if (status) {
        return status;
    }

    device->interrupt_server = server;
    device->interrupt_library = InterruptBase;

    return 0;
}

LONG unbindDeviceInterrupt(struct PciCatalog* catalog, struct pci_dev* board, struct Interrupt* server)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!device || !server) {
        return GIC400_ERR_INVALID_ARGUMENT;
    }

    if (!device->interrupt_server) {
        return GIC400_ERR_NOT_FOUND;
    }

    if (device->interrupt_server != server) {
        return GIC400_ERR_INVALID_ARGUMENT;
    }

    struct Library* InterruptBase = device->interrupt_library;

    if (!InterruptBase) {
        return GIC400_ERR_NOT_READY;
    }

    LONG status = RemIntServerEx(device->interrupt_irq, server);

    if (status) {
        return status;
    }

    device->interrupt_server = NULL;
    device->interrupt_library = NULL;

    return 0;
}
