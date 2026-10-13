/* SPDX-License-Identifier: MPL-2.0 */
#include "host.h"
#include "region.h"

APTR allocateBoardDma(struct ExecBase* exec_base, struct PciHosts* hosts,
    const struct pci_dev* board, ULONG size, ULONG flags)
{
    (void)board;
    (void)flags;

    return hosts ? allocateDmaMemory(exec_base, &hosts->shared_dma, size) : NULL;
}

BOOL releaseBoardDma(struct ExecBase* exec_base, struct PciHosts* hosts,
    const struct pci_dev* board, APTR address, ULONG size)
{
    (void)board;
    (void)size;

    return hosts && releaseDmaBuffer(exec_base, &hosts->shared_dma, address);
}

APTR boardLogicalToPhysical(struct PciHosts* hosts, const struct pci_dev* board, APTR address)
{
    if (!hosts) {
        return NULL;
    }

    uint64_t bus;

    if (!deviceBarToBus(&hosts->catalog, board, (ULONG)(uintptr_t)address, &bus)) {
        struct PciDmaMemory* memory = hostDmaMemory(hosts, board);

        if (!memory || !dmaToBus(memory->ranges, memory->count, (ULONG)(uintptr_t)address, 1, &bus)) {
            return NULL;
        }
    }

    return (APTR)(uintptr_t)(ULONG)bus;
}

APTR boardPhysicalToLogical(struct PciHosts* hosts, const struct pci_dev* board, APTR address)
{
    if (!hosts) {
        return NULL;
    }

    ULONG logical;

    if (!deviceBarToLogical(&hosts->catalog, board, (ULONG)(uintptr_t)address, &logical)) {
        struct PciDmaMemory* memory = hostDmaMemory(hosts, board);

        if (!memory || !dmaToLogical(memory->ranges, memory->count, (ULONG)(uintptr_t)address, 1, &logical)) {
            return NULL;
        }
    }

    return (APTR)(uintptr_t)logical;
}
