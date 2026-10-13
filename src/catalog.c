/* SPDX-License-Identifier: MPL-2.0 */
#include "catalog.h"

#include <exec/memory.h>
#define __NOLIBBASE__
#include <proto/exec.h>

BOOL freeCatalog(struct ExecBase* SysBase, struct PciCatalog* catalog)
{
    if (!SysBase || !catalog) {
        return FALSE;
    }

    for (struct PciDevice* device = catalog->first; device; device = device->next) {
        if (device->interrupt_set || device->interrupt_allocation || device->interrupt_type || device->interrupt_server ||
            device->interrupt_provider || device->interrupt_library) {
            return FALSE;
        }
    }

    struct PciDevice* device = catalog->first;

    while (device) {
        struct PciDevice* next = device->next;

        FreeMem(device, sizeof(*device));
        device = next;
    }

    *catalog = (struct PciCatalog){0};

    return TRUE;
}

BOOL appendCatalog(struct ExecBase* SysBase, const struct PciConfig* config, struct PciCatalog* catalog)
{
    if (!SysBase || !config || !catalog) {
        return FALSE;
    }

    for (struct PciDevice* device = catalog->first; device; device = device->next) {
        // The public bus/devfn API cannot distinguish overlapping windows.
        if (config->first_bus <= device->config->last_bus && config->last_bus >= device->config->first_bus) {
            return FALSE;
        }
    }

    struct PciCatalog pending = {0};
    ULONG cursor = (ULONG)config->first_bus << 8;
    struct PciFunction function;
    enum PciScanResult status;

    while ((status = nextPciFunction(config, &cursor, &function)) == PCI_SCAN_FUNCTION) {
        struct PciDeviceInfo info;

        if (!readDeviceInfo(config, &function, &info)) {
            goto failed;
        }

        struct PciDevice* device = AllocMem(sizeof(*device), MEMF_PUBLIC | MEMF_CLEAR);

        if (!device) {
            goto failed;
        }

        device->config = config;
        device->info = info;
        device->board.devfn = function.device & 255;
        device->board.vendor = function.vendor_id;
        device->board.device = function.device_id;
        device->board.devclass = function.class_code;

        // IRQ requires resolved INTx wiring; the configuration line is not it.
        // Resource/command fields remain private until device preparation.
        device->previous = pending.last;

        if (pending.last) {
            pending.last->next = device;
            pending.last->board.next = &device->board;
            device->board.pred = &pending.last->board;
        } else {
            pending.first = device;
        }

        pending.last = device;
        ++pending.count;
    }

    if (status != PCI_SCAN_END || pending.count > 0xffffffffUL - catalog->count) {
        goto failed;
    }

    if (pending.first) {
        pending.first->previous = catalog->last;

        if (catalog->last) {
            catalog->last->next = pending.first;
            catalog->last->board.next = &pending.first->board;
            pending.first->board.pred = &catalog->last->board;
        } else {
            catalog->first = pending.first;
        }

        catalog->last = pending.last;
        catalog->count += pending.count;
    }

    return TRUE;
failed:
    freeCatalog(SysBase, &pending);

    return FALSE;
}
