/* SPDX-License-Identifier: MPL-2.0 */
#include "host.h"

#include <exec/memory.h>
#define __NOLIBBASE__
#include <proto/exec.h>

// Sweep logical interval boundaries, retaining only complete cache lines
// reachable and aligned through every host. The first host supplies the bus
// coordinates for allocator validation, never another board's translation.
static BOOL commonRanges(const struct PciHosts* hosts, struct PciDmaRange* output, ULONG* count)
{
    const struct PciDmaMemory* first = &hosts->first->dma;
    ULONG found = 0, mask = first->alignment - 1;

    for (ULONG i = 0; i < first->count; ++i) {
        const struct PciDmaRange* source = first->ranges + i;
        uint64_t end = (uint64_t)source->logical + source->size;

        for (uint64_t position = source->logical; position < end;) {
            uint64_t next = end;
            BOOL common = TRUE;

            for (const struct PciHost* host = hosts->first; host; host = host->next) {
                BOOL covered = FALSE;

                for (ULONG n = 0; n < host->dma.count; ++n) {
                    const struct PciDmaRange* range = host->dma.ranges + n;
                    uint64_t limit = (uint64_t)range->logical + range->size;

                    if (range->logical > position) {
                        if (range->logical < next) {
                            next = range->logical;
                        }
                    } else if (position < limit) {
                        if (limit < next) {
                            next = limit;
                        }

                        covered = !((range->bus - range->logical) & mask);
                    }
                }

                if (!covered) {
                    common = FALSE;
                }
            }

            uint64_t start = (position + mask) & ~(uint64_t)mask;
            uint64_t stop = next & ~(uint64_t)mask;

            if (common && start < stop) {
                if (found == UINT32_MAX / sizeof(*output)) {
                    return FALSE;
                }

                if (output) {
                    if (found >= *count) {
                        return FALSE;
                    }

                    output[found] = (struct PciDmaRange){
                        (ULONG)start, (ULONG)(stop - start), source->bus + (start - source->logical)};
                }

                ++found;
            }

            position = next;
        }
    }

    *count = found;

    return TRUE;
}

BOOL prepareSharedDma(struct ExecBase* SysBase, struct PciHosts* hosts)
{
    if (!SysBase || !hosts || !hosts->first || hosts->shared_dma.ranges || hosts->shared_dma.count ||
        hosts->shared_dma.alignment || hosts->shared_dma.allocations) {
        return FALSE;
    }

    ULONG alignment = hosts->first->dma.alignment;

    if (alignment < 8 || (alignment & (alignment - 1))) {
        return FALSE;
    }

    for (const struct PciHost* host = hosts->first; host; host = host->next) {
        if (host->dma.alignment != alignment || host->dma.allocations ||
            !validateDmaRanges(host->dma.ranges, host->dma.count)) {
            return FALSE;
        }
    }

    ULONG count;

    if (!commonRanges(hosts, NULL, &count)) {
        return FALSE;
    }

    struct PciDmaRange* ranges = NULL;

    if (count) {
        ULONG filled = count;

        ranges = AllocMem(count * sizeof(*ranges), MEMF_PUBLIC);

        if (!ranges) {
            return FALSE;
        }

        if (!commonRanges(hosts, ranges, &filled) || filled != count || !validateDmaRanges(ranges, count)) {
            FreeMem(ranges, count * sizeof(*ranges));

            return FALSE;
        }
    }

    hosts->shared_dma = (struct PciDmaMemory){ranges, count, alignment, NULL};

    return TRUE;
}
