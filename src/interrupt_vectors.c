/* SPDX-License-Identifier: MPL-2.0 */
#include "interrupt_vectors.h"
#include "interrupt_allocation.h"
#include "interrupt.h"
#include "intx.h"
#include <exec/memory.h>
#include <libraries/pci_constants.h>
#include <libraries/bcmpcie_errors.h>
#define __NOLIBBASE__
#include <proto/exec.h>

static LONG detach(struct PciCatalog* catalog, struct PciDevice* device, ULONG vector)
{
    struct PciInterruptServer* server = &device->interrupt_set->servers[vector];

    if (!server->bound) {
        server->owner = NULL;

        return PCIE_OK;
    }

    LONG result;

    if (device->interrupt_type == PCI_IRQ_INTX) {
        if (!enableIntx(catalog, &device->board, FALSE)) {
            return PCIE_ERR_IO;
        }

        result = unbindDeviceInterrupt(catalog, &device->board, &server->server);

        if (result) {
            result = pciInterruptError(result);
        }
    } else {
        result = unbindDeviceMessage(catalog, &device->board, vector, &server->server);
    }

    if (!result) {
        server->bound = FALSE;
        server->owner = NULL;
    }

    return result;
}

LONG freeInterruptVectors(struct ExecBase* SysBase, struct PciCatalog* catalog, struct pci_dev* board)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!SysBase || !device) {
        return PCIE_ERR_INVAL;
    }

    struct PciInterruptSet* set = device->interrupt_set;

    if (set) {
        set->ready = FALSE;

        for (ULONG vector = 0; vector < set->count; ++vector) {
            set->servers[vector].client = NULL;
        }

        LONG result = device->interrupt_type == PCI_IRQ_INTX ?
            (enableIntx(catalog, board, FALSE) ? PCIE_OK : PCIE_ERR_IO) : stopDeviceMessages(catalog, board);

        if (result) {
            return result;
        }

        for (ULONG vector = 0; vector < set->count; ++vector) {
            result = detach(catalog, device, vector);

            if (result) {
                return result;
            }
        }
    }

    LONG result = releaseDeviceReservations(SysBase, catalog, board);

    if (result) {
        return result;
    }

    if (set) {
        device->interrupt_set = NULL;
        FreeMem(set, sizeof(*set) + set->count * sizeof(*set->servers));
    }

    return PCIE_OK;
}

LONG allocateInterruptVectors(struct ExecBase* SysBase, struct Library* interrupt_base,
    struct PciCatalog* catalog, struct pci_dev* board, ULONG minimum, ULONG maximum, ULONG flags)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!SysBase || !device) {
        return PCIE_ERR_INVAL;
    }

    if (device->interrupt_set) {
        return PCIE_ERR_BUSY;
    }

    LONG count = reserveDeviceInterrupts(SysBase, interrupt_base, catalog, board, minimum, maximum, flags);

    if (count < 1) {
        return count;
    }

    struct PciInterruptSet* set = AllocMem(sizeof(*set) + count * sizeof(*set->servers), MEMF_PUBLIC | MEMF_CLEAR);

    if (!set) {
        // A refused release stays reachable through the private device record;
        // FreeIntVectors can retry even though no coordinator set was created.
        releaseDeviceReservations(SysBase, catalog, board);

        return PCIE_ERR_NOMEM;
    }

    set->count = count;

    for (ULONG vector = 0; vector < set->count; ++vector) {
        initPciInterruptServer(set->servers + vector);
    }

    device->interrupt_set = set;

    LONG result = device->interrupt_type == PCI_IRQ_INTX ?
        (enableIntx(catalog, board, FALSE) ? PCIE_OK : PCIE_ERR_IO) : programDeviceMessages(catalog, board);

    if (result) {
        freeInterruptVectors(SysBase, catalog, board);

        return result;
    }

    set->ready = TRUE;

    return count;
}

LONG addInterruptVectorServer(struct ExecBase* SysBase, struct PciCatalog* catalog,
    struct pci_dev* board, ULONG vector, struct Interrupt* client)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!SysBase || !device || !client || !client->is_Code || !device->interrupt_set ||
        vector >= device->interrupt_set->count) {
        return PCIE_ERR_INVAL;
    }

    struct PciInterruptSet* set = device->interrupt_set;

    if (!set->ready) {
        return PCIE_ERR_BUSY;
    }

    struct PciInterruptServer* server = set->servers + vector;

    if (server->owner || server->bound) {
        return server->client == client && server->bound ? PCIE_OK : PCIE_ERR_BUSY;
    }

    LONG result;

    if (device->interrupt_type == PCI_IRQ_INTX) {
        result = bindDeviceInterrupt(device->interrupt_provider, catalog, board, &server->server);

        if (result) {
            result = pciInterruptError(result);
        }
    } else {
        result = bindDeviceMessage(SysBase, catalog, board, vector, &server->server);
    }

    if (result) {
        return result;
    }

    server->bound = TRUE;
    server->owner = client;
    server->client = client;
    result = device->interrupt_type == PCI_IRQ_INTX ?
        (enableIntx(catalog, board, TRUE) ? PCIE_OK : PCIE_ERR_IO) : activateDeviceMessage(catalog, board, vector);

    if (result) {
        server->client = NULL;

        // The hardware may have accepted an unverified write. A refused detach
        // leaves only our inert server attached, never the caller's object.
        detach(catalog, device, vector);
    }

    return result;
}

LONG remInterruptVectorServer(struct PciCatalog* catalog, struct pci_dev* board,
    ULONG vector, struct Interrupt* client)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!device || !client || !device->interrupt_set || vector >= device->interrupt_set->count) {
        return PCIE_ERR_INVAL;
    }

    struct PciInterruptServer* server = device->interrupt_set->servers + vector;

    if (!server->owner) {
        return PCIE_ERR_NODEV;
    }

    if (server->owner != client) {
        return PCIE_ERR_INVAL;
    }

    server->client = NULL;

    return detach(catalog, device, vector);
}

BOOL maskInterruptVector(struct PciCatalog* catalog, struct pci_dev* board, ULONG vector, BOOL mask)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!device || !device->interrupt_set || !device->interrupt_set->ready ||
        vector >= device->interrupt_set->count || !device->interrupt_set->servers[vector].client) {
        return FALSE;
    }

    return device->interrupt_type == PCI_IRQ_INTX ? maskIntx(catalog, board, vector, mask) :
        maskDeviceMessage(catalog, board, vector, mask);
}

ULONG getInterruptVectorType(struct PciCatalog* catalog, struct pci_dev* board)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    return device && device->interrupt_set && device->interrupt_set->ready ? device->interrupt_type : 0;
}
