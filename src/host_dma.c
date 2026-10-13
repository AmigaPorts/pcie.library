/* SPDX-License-Identifier: MPL-2.0 */
#include "host.h"
#include "dma_discovery.h"

BOOL prepareHostDma(struct ExecBase* exec_base, APTR tree_base, APTR cpus, APTR ram, struct PciHosts* hosts)
{
    if (!exec_base || !tree_base || !cpus || !ram || !hosts || !hosts->first || hosts->shared_dma.ranges ||
        hosts->shared_dma.count || hosts->shared_dma.alignment || hosts->shared_dma.allocations) {
        return FALSE;
    }

    // Do not overwrite a retained map, even if no buffers currently use it.
    for (struct PciHost* host = hosts->first; host; host = host->next) {
        if (host->dma.ranges || host->dma.count || host->dma.alignment || host->dma.allocations) {
            return FALSE;
        }
    }

    ULONG alignment;

    if (!readDmaAlignment(tree_base, cpus, &alignment)) {
        return FALSE;
    }

    for (struct PciHost* host = hosts->first; host; host = host->next) {
        if (!discoverDmaMemory(exec_base, tree_base, host->key, ram, alignment, &host->dma)) {
            for (struct PciHost* previous = hosts->first; previous != host; previous = previous->next) {
                freeDmaMemory(exec_base, &previous->dma);
            }

            return FALSE;
        }
    }

    if (!prepareSharedDma(exec_base, hosts)) {
        for (struct PciHost* host = hosts->first; host; host = host->next) {
            freeDmaMemory(exec_base, &host->dma);
        }

        return FALSE;
    }

    return TRUE;
}
