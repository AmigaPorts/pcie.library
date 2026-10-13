/* SPDX-License-Identifier: MPL-2.0 */
#include "dma.h"

#include <exec/execbase.h>
#include <exec/memory.h>
#define __NOLIBBASE__
#include <proto/exec.h>

struct PciDmaAllocation {
    struct PciDmaAllocation* next;
    APTR address;
    ULONG requested, reserved;
};

static APTR claimMemory(struct ExecBase* SysBase, const struct PciDmaMemory* memory, ULONG size)
{
    // Exec's free lists are task-owned. Forbid covers this walk and AllocAbs;
    // no interrupt code is permitted to allocate/free system memory.
    APTR result = NULL;

    Forbid();

    for (struct MemHeader* header = (struct MemHeader*)SysBase->MemList.lh_Head;
         header->mh_Node.ln_Succ && !result; header = (struct MemHeader*)header->mh_Node.ln_Succ) {
        if ((header->mh_Attributes & (MEMF_PUBLIC | MEMF_FAST)) != (MEMF_PUBLIC | MEMF_FAST)) {
            continue;
        }

        for (struct MemChunk* chunk = header->mh_First; chunk && !result; chunk = chunk->mc_Next) {
            uint64_t start = (uintptr_t)chunk, end = start + chunk->mc_Bytes;

            for (ULONG i = 0; i < memory->count; ++i) {
                const struct PciDmaRange* range = memory->ranges + i;

                if ((range->bus - range->logical) & (memory->alignment - 1)) {
                    continue;
                }

                uint64_t first = start > range->logical ? start : range->logical;
                uint64_t last = (uint64_t)range->logical + range->size;

                if (last > end) {
                    last = end;
                }

                first = (first + memory->alignment - 1) & ~((uint64_t)memory->alignment - 1);

                if (first < last && size <= last - first) {
                    result = AllocAbs(size, (APTR)(uintptr_t)first);

                    // AllocAbs changes this free list. Do not follow the old
                    // chunk after a successful claim, even if it was split.
                    if (result) {
                        break;
                    }
                }
            }

            if (result) {
                break;
            }
        }
    }

    Permit();

    return result;
}

APTR allocateDmaMemory(struct ExecBase* SysBase, struct PciDmaMemory* memory, ULONG size)
{
    if (!SysBase || !memory || !size || memory->alignment < 8 ||
        (memory->alignment & (memory->alignment - 1)) || size > UINT32_MAX - (memory->alignment - 1) ||
        !validateDmaRanges(memory->ranges, memory->count)) {
        return NULL;
    }

    struct PciDmaAllocation* record = AllocMem(sizeof(*record), MEMF_PUBLIC);

    if (!record) {
        return NULL;
    }

    // Keep both ends off other allocations' cache lines, including the trailing
    // partial line of an odd-sized request.
    ULONG reserved = (size + memory->alignment - 1) & ~(memory->alignment - 1);
    APTR address = claimMemory(SysBase, memory, reserved);

    if (!address) {
        FreeMem(record, sizeof(*record));

        return NULL;
    }

    for (ULONG i = 0; i < reserved; ++i) {
        ((UBYTE*)address)[i] = 0;
    }

    record->address = address;
    record->requested = size;
    record->reserved = reserved;
    record->next = memory->allocations;
    memory->allocations = record;

    return address;
}

static BOOL release(struct ExecBase* SysBase, struct PciDmaMemory* memory, APTR address, ULONG size, BOOL check_size)
{
    if (!SysBase || !memory || !address) {
        return FALSE;
    }

    struct PciDmaAllocation** link = &memory->allocations;

    while (*link) {
        struct PciDmaAllocation* record = *link;

        if (record->address == address) {
            if (check_size && record->requested != size) {
                return FALSE;
            }

            *link = record->next;
            FreeMem(record->address, record->reserved);
            FreeMem(record, sizeof(*record));

            return TRUE;
        }

        link = &record->next;
    }

    return FALSE;
}

BOOL releaseDmaMemory(struct ExecBase* SysBase, struct PciDmaMemory* memory, APTR address, ULONG size)
{
    return release(SysBase, memory, address, size, TRUE);
}

BOOL releaseDmaBuffer(struct ExecBase* SysBase, struct PciDmaMemory* memory, APTR address)
{
    return release(SysBase, memory, address, 0, FALSE);
}

BOOL dmaMemoryIdle(const struct PciDmaMemory* memory)
{
    return memory && !memory->allocations;
}
