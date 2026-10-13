/* SPDX-License-Identifier: MPL-2.0 */
#include "interrupt_allocation.h"
#include "intx.h"
#include <exec/memory.h>
#include <libraries/pci_constants.h>
#include <libraries/bcmpcie_errors.h>
#define __NOLIBBASE__
#include <proto/exec.h>

static BOOL canFallback(LONG status)
{
    return status == PCIE_ERR_NOMEM || status == PCIE_ERR_NOTSUPP || status == PCIE_ERR_NODEV;
}

static LONG reserveMessages(struct ExecBase* SysBase, struct Library* provider, struct PciDevice* device,
    const struct PciInterruptAllocation* description, ULONG type, ULONG minimum, ULONG maximum)
{
    struct PciInterruptAllocation* allocation = AllocMem(sizeof(*allocation), MEMF_PUBLIC | MEMF_CLEAR);

    if (!allocation) {
        return PCIE_ERR_NOMEM;
    }

    *allocation = *description;

    // Publish private ownership before calling the provider: failed rollback
    // must leave a reachable record even though no public mode was acquired.
    device->interrupt_allocation = allocation;

    LONG result = reservePciMessages(SysBase, provider, type, minimum, maximum,
        type == PCI_IRQ_MSI ? allocation->msi.vectors : allocation->msix.vectors, &allocation->messages);

    if (result > 0) {
        device->interrupt_type = type;
        device->interrupt_provider = provider;
    } else if (!allocation->messages.messages) {
        device->interrupt_allocation = NULL;
        FreeMem(allocation, sizeof(*allocation));
    }

    return result;
}

LONG reserveDeviceInterrupts(struct ExecBase* SysBase, struct Library* provider,
    struct PciCatalog* catalog, struct pci_dev* board, ULONG minimum, ULONG maximum, ULONG flags)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!SysBase || !device || !maximum || minimum > maximum || !flags || (flags & ~PCI_IRQ_ALL_TYPES)) {
        return PCIE_ERR_INVAL;
    }

    if (device->interrupt_set || device->interrupt_allocation || device->interrupt_type || device->interrupt_server ||
        device->interrupt_provider || device->interrupt_library) {
        return PCIE_ERR_BUSY;
    }

    if (!minimum) {
        minimum = 1;
    }

    struct PciInterruptAllocation description = {0};
    ULONG slot = device->info.function.device;

    // Even INTx-only selection must not take over a function already sending
    // messages. Read both modes, but never reserve or enable a forbidden type.
    if (!readMsiCapability(device->config, slot, &description.msi) ||
        !readMsixCapability(device->config, slot, &description.msix)) {
        return PCIE_ERR_IO;
    }

    if ((description.msi.control & 1) || (description.msix.control & 0x8000)) {
        return PCIE_ERR_BUSY;
    }

    LONG result = PCIE_ERR_NOTSUPP;

    if ((flags & PCI_IRQ_MSIX) && description.msix.offset) {
        if (device->resources_prepared &&
            resolveMsixTables(&device->resources, &device->mappings, &description.msix, &description.tables)) {
            result = reserveMessages(SysBase, provider, device, &description, PCI_IRQ_MSIX, minimum, maximum);

            if (result > 0 || device->interrupt_allocation || !canFallback(result)) {
                return result;
            }
        }
    }

    if ((flags & PCI_IRQ_MSI) && description.msi.offset) {
        result = reserveMessages(SysBase, provider, device, &description, PCI_IRQ_MSI, minimum, maximum);

        if (result > 0 || device->interrupt_allocation || !canFallback(result)) {
            return result;
        }
    }

    if ((flags & PCI_IRQ_INTX) && minimum == 1) {
        return allocateIntx(provider, catalog, board, minimum);
    }

    return result;
}

LONG releaseDeviceReservations(struct ExecBase* SysBase, struct PciCatalog* catalog, struct pci_dev* board)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!SysBase || !device) {
        return PCIE_ERR_INVAL;
    }

    if (device->interrupt_server || device->interrupt_library) {
        return PCIE_ERR_BUSY;
    }

    struct PciInterruptAllocation* allocation = device->interrupt_allocation;

    if (!allocation) {
        return releaseIntx(catalog, board);
    }

    if (allocation->server_count) {
        return PCIE_ERR_BUSY;
    }

    if (allocation->phase != PCI_INTERRUPT_RESERVED && allocation->phase != PCI_INTERRUPT_STOPPED &&
        allocation->phase != PCI_INTERRUPT_RELEASING) {
        return PCIE_ERR_BUSY;
    }

    allocation->phase = PCI_INTERRUPT_RELEASING;

    if (allocation->servers) {
        FreeMem(allocation->servers, allocation->messages.capacity * sizeof(*allocation->servers));
        allocation->servers = NULL;
    }

    LONG result = releasePciMessages(SysBase, &allocation->messages);

    if (result) {
        return result;
    }

    FreeMem(allocation, sizeof(*allocation));
    device->interrupt_allocation = NULL;
    device->interrupt_type = 0;
    device->interrupt_provider = NULL;

    return PCIE_OK;
}
