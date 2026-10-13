/* SPDX-License-Identifier: MPL-2.0 */
#include "state.h"
#include "interrupt.h"

#include <exec/libraries.h>
#define __NOLIBBASE__
#include <proto/exec.h>
#include <proto/devicetree.h>

static struct PciHost* deviceHost(struct PciHosts* hosts, const struct PciDevice* device)
{
    for (struct PciHost* host = hosts->first; host; host = host->next) {
        if (&host->config == device->config) {
            return host;
        }
    }

    return NULL;
}

static BOOL prepareCatalog(struct PciState* state)
{
    for (struct PciHost* host = state->hosts.first; host; host = host->next) {
        if (!readPciMemoryWindow(state->tree_base, host->key, &host->window)) {
            return FALSE;
        }
    }

    // Check every function and route first, so a later unsupported device
    // or missing firmware description cannot trigger earlier BAR probes.
    for (struct PciDevice* device = state->hosts.catalog.first; device; device = device->next) {
        struct PciHost* host = deviceHost(&state->hosts, device);
        ULONG command, identity, header;
        ULONG slot = device->info.function.device;

        if (!host || device->board.owner ||
            !readConfig(device->config, slot, 0, 4, &identity) ||
            identity != ((ULONG)device->info.function.device_id << 16 | device->info.function.vendor_id) ||
            !readConfig(device->config, slot, 14, 1, &header) || (header & 0x7f) > 1 ||
            (header & 0x7f) != (device->info.function.header_type & 0x7f) ||
            !readConfig(device->config, slot, 4, 2, &command) ||
            !prepareDeviceInterrupt(state->tree_base, state->root, host->key, state->interrupt_base,
                &state->hosts.catalog, &device->board)) {
            return FALSE;
        }
    }

    for (struct PciDevice* device = state->hosts.catalog.first; device; device = device->next) {
        struct PciHost* host = deviceHost(&state->hosts, device);

        if (!prepareAssignedDevice(state->tree_base, host->key, &state->hosts.catalog, &device->board)) {
            return FALSE;
        }

        // The ABI's reserved pointer is opaque; public operations still check
        // catalog membership and never trust a writable caller-owned shadow.
        device->board.reserved = device;
    }

    return TRUE;
}

BOOL releasePciState(struct PciState* state)
{
    if (!state || !state->exec_base) {
        return FALSE;
    }

    for (struct PciDevice* device = state->hosts.catalog.first; device; device = device->next) {
        if (device->board.owner) {
            return FALSE;
        }
    }

    struct ExecBase* SysBase = state->exec_base;

    if (!freePciHosts(SysBase, &state->hosts)) {
        return FALSE;
    }

    if (state->interrupt_base) {
        CloseLibrary(state->interrupt_base);
    }

    APTR DeviceTreeBase = state->tree_base;

    if (state->root) {
        DT_CloseKey(state->root);
    }

    state->exec_base = NULL;
    state->tree_base = state->root = NULL;
    state->interrupt_base = NULL;

    return TRUE;
}

BOOL initializePciState(struct ExecBase* SysBase, struct PciState* result)
{
    if (!SysBase || !result || result->exec_base || result->tree_base || result->root || result->interrupt_base ||
        result->hosts.first || result->hosts.catalog.first || result->hosts.catalog.last || result->hosts.catalog.count ||
        result->hosts.shared_dma.ranges || result->hosts.shared_dma.count || result->hosts.shared_dma.alignment ||
        result->hosts.shared_dma.allocations) {
        return FALSE;
    }

    struct PciState pending = {.exec_base = SysBase};
    APTR DeviceTreeBase = pending.tree_base = OpenResource((CONST_STRPTR)"devicetree.resource");

    if (!DeviceTreeBase) {
        return FALSE;
    }

    pending.root = DT_OpenKey((CONST_STRPTR)"/");

    if (!pending.root) {
        return FALSE;
    }

    pending.interrupt_base = OpenLibrary((CONST_STRPTR)"interrupt.library", 1);

    if (!pending.interrupt_base || pending.interrupt_base->lib_Version < 1 ||
        (pending.interrupt_base->lib_Version == 1 && pending.interrupt_base->lib_Revision < 13) ||
        pending.interrupt_base->lib_NegSize < 192) {
        goto failed;
    }

    APTR cpus = DT_OpenKey((CONST_STRPTR)"/cpus");
    APTR ram = DT_OpenKey((CONST_STRPTR)"/memory");
    BOOL mapped = cpus && ram && discoverPciHosts(SysBase, DeviceTreeBase, pending.root, &pending.hosts) &&
        prepareHostDma(SysBase, DeviceTreeBase, cpus, ram, &pending.hosts);

    if (ram) {
        DT_CloseKey(ram);
    }

    if (cpus) {
        DT_CloseKey(cpus);
    }

    if (!mapped || !prepareCatalog(&pending)) {
        goto failed;
    }

    *result = pending;

    // InitSemaphore embeds pointers to itself. Initialize at its final address,
    // never copy an initialized semaphore from temporary startup storage.
    InitSemaphore(&result->semaphore);

    return TRUE;
failed:
    releasePciState(&pending);

    return FALSE;
}
