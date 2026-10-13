/* SPDX-License-Identifier: MPL-2.0 */
#include "host.h"

#include <stdint.h>
#include <exec/memory.h>
#include "devicetree_address.h"
#define __NOLIBBASE__
#include <proto/exec.h>
#include <proto/devicetree.h>

BOOL freePciHosts(struct ExecBase* SysBase, struct PciHosts* hosts)
{
    if (!SysBase || !hosts || hosts->shared_dma.allocations) {
        return FALSE;
    }

    // Preflight every host before freeing any borrowing device record.
    for (struct PciHost* host = hosts->first; host; host = host->next) {
        if (host->dma.allocations) {
            return FALSE;
        }
    }

    // Catalog records borrow their host's configuration window.
    if (!freeCatalog(SysBase, &hosts->catalog)) {
        return FALSE;
    }

    if (hosts->shared_dma.ranges) {
        FreeMem((APTR)hosts->shared_dma.ranges, hosts->shared_dma.count * sizeof(*hosts->shared_dma.ranges));
    }

    struct PciHost* host = hosts->first;

    while (host) {
        struct PciHost* next = host->next;

        // These tables are owned by prepareHostDma; never a borrowed board map.
        if (host->dma.ranges) {
            FreeMem((APTR)host->dma.ranges, host->dma.count * sizeof(*host->dma.ranges));
        }

        FreeMem(host, sizeof(*host));
        host = next;
    }

    *hosts = (struct PciHosts){0};

    return TRUE;
}

BOOL discoverPciHosts(struct ExecBase* SysBase, APTR DeviceTreeBase, APTR root, struct PciHosts* result)
{
    if (!SysBase || !DeviceTreeBase || !root || !result || result->first ||
        result->catalog.first || result->catalog.last || result->catalog.count || result->shared_dma.ranges ||
        result->shared_dma.count || result->shared_dma.alignment || result->shared_dma.allocations) {
        return FALSE;
    }

    struct PciHosts pending = {0};
    struct PciHost** tail = &pending.first;
    APTR key = NULL;

    while ((key = dtFindCompatible(DeviceTreeBase, root, key, "pci-host-ecam-generic"))) {
        APTR property = DT_FindProperty(key, (CONST_STRPTR)"linux,pci-domain");

        if (property) {
            const UBYTE* bytes = DT_GetPropValue(property);

            if (!bytes || DT_GetPropLen(property) != 4 || bytes[0] || bytes[1]) {
                goto failed;
            }

            if (bytes[2] || bytes[3]) {
                continue;
            }
        }

        struct PciConfig config;

        if (!readConfigWindow(DeviceTreeBase, key, &config)) {
            goto failed;
        }

        uint64_t start = (uintptr_t)config.address;
        uint64_t size = ((ULONG)config.last_bus - config.first_bus + 1) << 20;

        for (struct PciHost* other = pending.first; other; other = other->next) {
            if (config.first_bus <= other->config.last_bus && config.last_bus >= other->config.first_bus) {
                goto failed;
            }

            uint64_t other_start = (uintptr_t)other->config.address;
            uint64_t other_size = ((ULONG)other->config.last_bus - other->config.first_bus + 1) << 20;

            if (start < other_start + other_size && other_start < start + size) {
                goto failed;
            }
        }

        struct PciHost* host = AllocMem(sizeof(*host), MEMF_PUBLIC | MEMF_CLEAR);

        if (!host) {
            goto failed;
        }

        host->key = key;
        host->config = config;
        *tail = host;
        tail = &host->next;
    }

    if (!pending.first) {
        goto failed;
    }

    for (struct PciHost* host = pending.first; host; host = host->next) {
        if (!appendCatalog(SysBase, &host->config, &pending.catalog)) {
            goto failed;
        }
    }

    *result = pending;

    return TRUE;
failed:
    freePciHosts(SysBase, &pending);

    return FALSE;
}
