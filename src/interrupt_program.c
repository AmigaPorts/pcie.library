/* SPDX-License-Identifier: MPL-2.0 */
#include "interrupt_allocation.h"
#include <libraries/pci_constants.h>
#include <libraries/bcmpcie_errors.h>

static BOOL validAllocation(const struct PciDevice* device)
{
    const struct PciInterruptAllocation* allocation = device->interrupt_allocation;

    return allocation && allocation->messages.messages && allocation->messages.count &&
        allocation->messages.count <= allocation->messages.capacity &&
        allocation->messages.provider == device->interrupt_provider && device->interrupt_provider &&
        allocation->messages.type == device->interrupt_type &&
        (device->interrupt_type == PCI_IRQ_MSI || device->interrupt_type == PCI_IRQ_MSIX);
}

LONG programDeviceMessages(struct PciCatalog* catalog, struct pci_dev* board)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!device || !validAllocation(device)) {
        return PCIE_ERR_INVAL;
    }

    struct PciInterruptAllocation* allocation = device->interrupt_allocation;

    if (allocation->phase != PCI_INTERRUPT_RESERVED || device->interrupt_server || device->interrupt_library) {
        return PCIE_ERR_BUSY;
    }

    // Selection was read-only. Check both live modes again before any write so
    // programming cannot silently take over an enabled, competing message mode.
    ULONG slot = device->info.function.device;
    ULONG control;

    if (allocation->msi.offset) {
        if (!readConfig(device->config, slot, allocation->msi.offset + 2, 2, &control)) {
            return PCIE_ERR_IO;
        }

        if (control & 1) {
            return PCIE_ERR_BUSY;
        }
    }

    if (allocation->msix.offset) {
        if (!readConfig(device->config, slot, allocation->msix.offset + 2, 2, &control)) {
            return PCIE_ERR_IO;
        }

        if (control & 0x8000) {
            return PCIE_ERR_BUSY;
        }
    }

    // Mark ownership before the first possible write. A failed readback does
    // not prove that the device ignored the write, so release must stay blocked.
    allocation->phase = PCI_INTERRUPT_PROGRAMMING;

    if (device->interrupt_type == PCI_IRQ_MSI) {
        if (!programMsi(device->config, slot, &allocation->msi, allocation->messages.count,
                allocation->messages.messages)) {
            return PCIE_ERR_IO;
        }

        if (allocation->msi.control & 0x100) {
            for (ULONG vector = 0; vector < allocation->messages.count; ++vector) {
                if (!maskMsiVector(device->config, slot, &allocation->msi, vector, TRUE)) {
                    return PCIE_ERR_IO;
                }
            }
        }
    } else {
        if (!enableMsix(device->config, slot, &allocation->msix, FALSE, TRUE)) {
            return PCIE_ERR_IO;
        }

        // The function mask alone is insufficient once registration later
        // unmasks the function. Unallocated entries must stay individually masked.
        for (ULONG vector = 0; vector < allocation->msix.vectors; ++vector) {
            if (!maskMsixVector(&allocation->tables, vector, TRUE)) {
                return PCIE_ERR_IO;
            }
        }

        for (ULONG vector = 0; vector < allocation->messages.count; ++vector) {
            if (!programMsixVector(&allocation->tables, vector, allocation->messages.messages + vector)) {
                return PCIE_ERR_IO;
            }
        }
    }

    allocation->phase = PCI_INTERRUPT_PROGRAMMED;

    return PCIE_OK;
}

LONG stopDeviceMessages(struct PciCatalog* catalog, struct pci_dev* board)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!device || !validAllocation(device)) {
        return PCIE_ERR_INVAL;
    }

    struct PciInterruptAllocation* allocation = device->interrupt_allocation;

    if (allocation->phase == PCI_INTERRUPT_STOPPED || allocation->phase == PCI_INTERRUPT_RELEASING) {
        return PCIE_OK;
    }

    // Even a reserved allocation may have had its enable bit changed by the
    // driver. Verify live disabled state rather than assuming the old snapshot.
    allocation->phase = PCI_INTERRUPT_STOPPING;

    ULONG slot = device->info.function.device;
    BOOL stopped = device->interrupt_type == PCI_IRQ_MSI ?
        enableMsi(device->config, slot, &allocation->msi, FALSE) :
        enableMsix(device->config, slot, &allocation->msix, FALSE, TRUE);

    if (!stopped) {
        return PCIE_ERR_IO;
    }

    allocation->phase = PCI_INTERRUPT_STOPPED;

    return PCIE_OK;
}
