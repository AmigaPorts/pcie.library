/* SPDX-License-Identifier: MPL-2.0 */
#include "message_allocation.h"
#include <exec/libraries.h>
#include <exec/memory.h>
#include <libraries/pci_constants.h>
#include <libraries/bcmpcie_errors.h>
#define __NOLIBBASE__
#include <proto/exec.h>
#include <proto/interrupt.h>

LONG pciInterruptError(LONG status)
{
    switch (status) {
        case GIC400_ERR_NO_MEMORY:
            return PCIE_ERR_NOMEM;
        case GIC400_ERR_NOT_READY:
        case GIC400_ERR_ALREADY_REGISTERED:
            return PCIE_ERR_BUSY;
        case GIC400_ERR_NOT_ROUTABLE:
            return PCIE_ERR_NOTSUPP;
        case GIC400_ERR_INVALID_IRQ:
        case GIC400_ERR_INVALID_ARGUMENT:
            return PCIE_ERR_INVAL;
        case GIC400_ERR_NOT_FOUND:
            return PCIE_ERR_NODEV;
        default:
            return PCIE_ERR_IO;
    }
}

LONG releasePciMessages(struct ExecBase* SysBase, struct PciMessageAllocation* allocation)
{
    if (!SysBase || !allocation) {
        return PCIE_ERR_INVAL;
    }

    if (!allocation->messages) {
        return allocation->provider || allocation->type || allocation->count || allocation->capacity ?
            PCIE_ERR_INVAL : PCIE_OK;
    }

    if (!allocation->provider || !allocation->capacity || allocation->capacity > 2048 ||
        allocation->count > allocation->capacity ||
        (allocation->type != PCI_IRQ_MSI && allocation->type != PCI_IRQ_MSIX) ||
        (allocation->type == PCI_IRQ_MSI && (allocation->capacity > 32 ||
            (allocation->count && (allocation->count & (allocation->count - 1)))))) {
        return PCIE_ERR_INVAL;
    }

    struct Library* InterruptBase = allocation->provider;

    while (allocation->count) {
        ULONG count = allocation->type == PCI_IRQ_MSI ? allocation->count : 1;
        ULONG first = allocation->count - count;
        LONG status = FreeMessageInterrupts(allocation->messages[first].irq, count);

        if (status) {
            return pciInterruptError(status);
        }

        allocation->count = first;
    }

    FreeMem(allocation->messages, allocation->capacity * sizeof(*allocation->messages));
    *allocation = (struct PciMessageAllocation){0};

    return PCIE_OK;
}

LONG reservePciMessages(struct ExecBase* SysBase, struct Library* InterruptBase,
    ULONG type, ULONG minimum, ULONG maximum, ULONG limit, struct PciMessageAllocation* allocation)
{
    if (!SysBase || !allocation || !maximum || minimum > maximum || !limit ||
        (type != PCI_IRQ_MSI && type != PCI_IRQ_MSIX) ||
        (type == PCI_IRQ_MSI ? limit > 32 || (limit & (limit - 1)) : limit > 2048)) {
        return PCIE_ERR_INVAL;
    }

    if (allocation->messages || allocation->provider || allocation->type || allocation->count || allocation->capacity) {
        return PCIE_ERR_BUSY;
    }

    if (!InterruptBase || InterruptBase->lib_Version < 1 ||
        (InterruptBase->lib_Version == 1 && InterruptBase->lib_Revision < 13) ||
        InterruptBase->lib_NegSize < 192) {
        return PCIE_ERR_NOTSUPP;
    }

    if (!minimum) {
        minimum = 1;
    }

    ULONG capacity = maximum < limit ? maximum : limit;

    if (type == PCI_IRQ_MSI) {
        ULONG count = 1;

        while (count <= capacity / 2) {
            count *= 2;
        }

        capacity = count;
    }

    if (capacity < minimum) {
        return PCIE_ERR_NOMEM;
    }

    struct InterruptMessage* messages = AllocMem(capacity * sizeof(*messages), MEMF_PUBLIC | MEMF_CLEAR);

    if (!messages) {
        return PCIE_ERR_NOMEM;
    }

    *allocation = (struct PciMessageAllocation){InterruptBase, messages, type, 0, capacity};

    LONG status = GIC400_ERR_NO_MEMORY;

    if (type == PCI_IRQ_MSI) {
        for (ULONG count = capacity; count >= minimum; count /= 2) {
            status = AllocMessageInterrupts(count, messages);

            if (!status) {
                allocation->count = count;

                return count;
            }

            if (status != GIC400_ERR_NO_MEMORY) {
                break;
            }
        }
    } else {
        while (allocation->count < capacity) {
            status = AllocMessageInterrupts(1, messages + allocation->count);

            if (status) {
                break;
            }

            ++allocation->count;
        }

        if (allocation->count == capacity ||
            (status == GIC400_ERR_NO_MEMORY && allocation->count >= minimum)) {
            return allocation->count;
        }
    }

    LONG cleanup = releasePciMessages(SysBase, allocation);

    return cleanup ? cleanup : pciInterruptError(status);
}
