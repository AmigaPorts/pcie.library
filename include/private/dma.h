/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <exec/types.h>
#include <stdint.h>

struct ExecBase;

// Library-owned, validated inbound mappings obtained through devicetree.resource.
// These describe reachable RAM, not BARs or arbitrary system Fast RAM. Retain
// full PCI addresses; narrowing belongs at the existing public APTR boundary.
struct PciDmaRange {
    ULONG logical, size;
    uint64_t bus;
};

// Reject empty, wrapping or ambiguous maps before allocation or publication.
BOOL validateDmaRanges(const struct PciDmaRange* ranges, ULONG count);
BOOL dmaToBus(const struct PciDmaRange* ranges, ULONG count, ULONG address, ULONG size, uint64_t* result);
BOOL dmaToLogical(const struct PciDmaRange* ranges, ULONG count, uint64_t address, ULONG size, ULONG* result);

struct PciDmaAllocation;
struct PciDmaMemory {
    const struct PciDmaRange* ranges;
    ULONG count, alignment;
    struct PciDmaAllocation* allocations;
};

// Task context only. The library semaphore protects this state and its retained
// ranges for the complete call. Alignment is a power of two, at least Exec's
// eight-byte allocation quantum, supplied by platform cache discovery. Public
// allocation flags are ignored by the reference ABI, not interpreted as MEMF_*.
// Only free MEMF_PUBLIC|MEMF_FAST memory inside a reachable range is claimed.
// Both ends are rounded to alignment; logical and bus starts must both align.
// Zero-fill before returning. Exec owns RAM; no separate emulator allocator.
APTR allocateDmaMemory(struct ExecBase* exec_base, struct PciDmaMemory* memory, ULONG size);

// Match the exact allocation and original size before calling Exec FreeMem.
// Wrong sizes, foreign/interior pointers and repeated releases change nothing.
BOOL releaseDmaMemory(struct ExecBase* exec_base, struct PciDmaMemory* memory, APTR address, ULONG size);

// Public PCI release semantics ignore the supplied size. Use the exact base
// address and retained reservation size; never read a caller-supplied pointer.
BOOL releaseDmaBuffer(struct ExecBase* exec_base, struct PciDmaMemory* memory, APTR address);

// The enclosing library must refuse teardown while this returns FALSE.
BOOL dmaMemoryIdle(const struct PciDmaMemory* memory);
