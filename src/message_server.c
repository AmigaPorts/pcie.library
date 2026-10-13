/* SPDX-License-Identifier: MPL-2.0 */
#include "interrupt_allocation.h"
#include <exec/interrupts.h>
#include <exec/memory.h>
#include <libraries/pci_constants.h>
#include <libraries/bcmpcie_errors.h>
#define __NOLIBBASE__
#include <proto/exec.h>
#include <proto/interrupt.h>

static struct PciInterruptAllocation* deviceVector(struct PciDevice* device, ULONG vector)
{
    if (!device || !device->interrupt_allocation) {
        return NULL;
    }

    struct PciInterruptAllocation* allocation = device->interrupt_allocation;

    if (!allocation->messages.messages || !allocation->messages.provider ||
        allocation->messages.provider != device->interrupt_provider ||
        allocation->messages.type != device->interrupt_type ||
        (device->interrupt_type != PCI_IRQ_MSI && device->interrupt_type != PCI_IRQ_MSIX) ||
        allocation->messages.count > allocation->messages.capacity || vector >= allocation->messages.count) {
        return NULL;
    }

    return allocation;
}

static BOOL canActivate(const struct PciInterruptAllocation* allocation)
{
    return allocation->phase == PCI_INTERRUPT_PROGRAMMED || allocation->phase == PCI_INTERRUPT_ACTIVE;
}

static BOOL maskVector(struct PciDevice* device, ULONG vector, BOOL mask)
{
    struct PciInterruptAllocation* allocation = device->interrupt_allocation;

    return device->interrupt_type == PCI_IRQ_MSI ?
        maskMsiVector(device->config, device->info.function.device, &allocation->msi, vector, mask) :
        maskMsixVector(&allocation->tables, vector, mask);
}

BOOL maskDeviceMessage(struct PciCatalog* catalog, struct pci_dev* board, ULONG vector, BOOL mask)
{
    struct PciDevice* device = catalogDevice(catalog, board);
    struct PciInterruptAllocation* allocation = deviceVector(device, vector);

    return allocation && canActivate(allocation) && allocation->servers && allocation->servers[vector] &&
        maskVector(device, vector, mask);
}

LONG bindDeviceMessage(struct ExecBase* SysBase, struct PciCatalog* catalog,
    struct pci_dev* board, ULONG vector, struct Interrupt* server)
{
    struct PciDevice* device = catalogDevice(catalog, board);
    struct PciInterruptAllocation* allocation = deviceVector(device, vector);

    if (!SysBase || !allocation || !server || !server->is_Code) {
        return PCIE_ERR_INVAL;
    }

    if (!canActivate(allocation)) {
        return PCIE_ERR_BUSY;
    }

    if (!allocation->servers) {
        allocation->servers = AllocMem(allocation->messages.capacity * sizeof(*allocation->servers),
            MEMF_PUBLIC | MEMF_CLEAR);

        if (!allocation->servers) {
            return PCIE_ERR_NOMEM;
        }
    }

    if (allocation->servers[vector]) {
        return allocation->servers[vector] == server ? PCIE_OK : PCIE_ERR_BUSY;
    }

    struct Library* InterruptBase = allocation->messages.provider;
    LONG result = AddIntServerEx(allocation->messages.messages[vector].irq, 0, TRUE, server);

    if (result) {
        return pciInterruptError(result);
    }

    allocation->servers[vector] = server;
    ++allocation->server_count;

    return PCIE_OK;
}

LONG activateDeviceMessage(struct PciCatalog* catalog, struct pci_dev* board, ULONG vector)
{
    struct PciDevice* device = catalogDevice(catalog, board);
    struct PciInterruptAllocation* allocation = deviceVector(device, vector);

    if (!allocation) {
        return PCIE_ERR_INVAL;
    }

    if (!canActivate(allocation) || !allocation->servers || !allocation->servers[vector]) {
        return PCIE_ERR_BUSY;
    }

    // A failed enable readback may follow a successful write. Retain all
    // bindings and require verified stop before releasing controller ownership.
    allocation->phase = PCI_INTERRUPT_ACTIVE;

    ULONG slot = device->info.function.device;
    BOOL enabled = device->interrupt_type == PCI_IRQ_MSI ?
        enableMsi(device->config, slot, &allocation->msi, TRUE) :
        enableMsix(device->config, slot, &allocation->msix, TRUE, FALSE);

    if (!enabled) {
        return PCIE_ERR_IO;
    }

    if ((device->interrupt_type == PCI_IRQ_MSIX || (allocation->msi.control & 0x100)) &&
        !maskVector(device, vector, FALSE)) {
        return PCIE_ERR_IO;
    }

    return PCIE_OK;
}

LONG unbindDeviceMessage(struct PciCatalog* catalog, struct pci_dev* board,
    ULONG vector, struct Interrupt* server)
{
    struct PciDevice* device = catalogDevice(catalog, board);
    struct PciInterruptAllocation* allocation = deviceVector(device, vector);

    if (!allocation || !server) {
        return PCIE_ERR_INVAL;
    }

    if (!allocation->servers || !allocation->servers[vector]) {
        return PCIE_ERR_NODEV;
    }

    if (allocation->servers[vector] != server) {
        return PCIE_ERR_INVAL;
    }

    if (!canActivate(allocation) && allocation->phase != PCI_INTERRUPT_STOPPED) {
        return PCIE_ERR_BUSY;
    }

    if (allocation->phase != PCI_INTERRUPT_STOPPED &&
        (device->interrupt_type == PCI_IRQ_MSIX || (allocation->msi.control & 0x100)) &&
        !maskVector(device, vector, TRUE)) {
        return PCIE_ERR_IO;
    }

    struct Library* InterruptBase = allocation->messages.provider;
    LONG result = RemIntServerEx(allocation->messages.messages[vector].irq, server);

    if (result) {
        return pciInterruptError(result);
    }

    allocation->servers[vector] = NULL;
    --allocation->server_count;

    return PCIE_OK;
}
