/* SPDX-License-Identifier: MPL-2.0 */
#include "dma_discovery.h"
#include "dma_acpi.h"

#include <exec/memory.h>
#include "devicetree_address.h"
#define __NOLIBBASE__
#include <proto/exec.h>
#include <proto/devicetree.h>

static ULONG cell(const UBYTE* value)
{
    return (ULONG)value[0] << 24 | (ULONG)value[1] << 16 | (ULONG)value[2] << 8 | value[3];
}

static uint64_t number(const UBYTE* value, ULONG count)
{
    return count == 1 ? cell(value) : (uint64_t)cell(value) << 32 | cell(value + 4);
}

static ULONG cells(APTR DeviceTreeBase, APTR key, const char* name, ULONG fallback)
{
    APTR property = DT_FindProperty(key, (CONST_STRPTR)name);

    if (!property) {
        return fallback;
    }

    const UBYTE* value = DT_GetPropValue(property);

    return value && DT_GetPropLen(property) == 4 ? cell(value) : 0;
}

static BOOL extent(uint64_t start, uint64_t size, ULONG count)
{
    return size && size - 1 <= UINT64_MAX - start &&
        (count == 2 || (start <= UINT32_MAX && size - 1 <= UINT32_MAX - start));
}

struct DmaDiscovery {
    APTR tree, ram;
    ULONG ram_count, count, capacity;
    struct PciDmaRange* ranges;
};

static BOOL addRam(APTR context, uint64_t start, uint64_t last, uint64_t bus)
{
    struct DmaDiscovery* state = context;

    for (ULONG i = 0; i < state->ram_count; ++i) {
        ULONG address, size;

        if (!dtGetRegister(state->tree, state->ram, i, &address, &size)) {
            return FALSE;
        }

        uint64_t first = start > address ? start : address;
        uint64_t end = (uint64_t)address + size - 1;

        if (end > last) {
            end = last;
        }

        if (first > end) {
            continue;
        }

        uint64_t length = end - first + 1;

        if (!first || length > UINT32_MAX || first - start > UINT64_MAX - bus ||
            length - 1 > UINT64_MAX - (bus + first - start) ||
            state->count == UINT32_MAX / sizeof(struct PciDmaRange)) {
            return FALSE;
        }

        if (state->ranges) {
            if (state->count == state->capacity) {
                return FALSE;
            }

            state->ranges[state->count] = (struct PciDmaRange){first, length, bus + first - start};
        }

        ++state->count;
    }

    return TRUE;
}

// Split at ancestor-window boundaries, preserving the PCI-to-parent offset.
// Inclusive ends also represent an empty property's full-width identity space
// without overflowing a 64-bit length. Only reachable RAM becomes an allocation.
static BOOL translateWindow(struct DmaDiscovery* state, APTR key,
    uint64_t start, uint64_t last, uint64_t bus)
{
    APTR DeviceTreeBase = state->tree;
    APTR parent = DT_GetParent(key);

    if (!parent) {
        return addRam(state, start, last, bus);
    }

    ULONG ac = cells(DeviceTreeBase, key, "#address-cells", 2);
    ULONG sc = cells(DeviceTreeBase, key, "#size-cells", 1);
    ULONG pac = cells(DeviceTreeBase, parent, "#address-cells", 2);

    if ((ac != 1 && ac != 2) || (sc != 1 && sc != 2) || (pac != 1 && pac != 2)) {
        return FALSE;
    }

    APTR property = DT_FindProperty(key, (CONST_STRPTR)"ranges");

    if (!property) {
        return FALSE;
    }

    ULONG length = DT_GetPropLen(property), stride = 4 * (ac + pac + sc);
    const UBYTE* value = DT_GetPropValue(property);

    if (length % stride || (length && !value)) {
        return FALSE;
    }

    if (!length) {
        uint64_t end = last;

        if ((ac == 1 || pac == 1) && end > UINT32_MAX) {
            end = UINT32_MAX;
        }

        return start > end || translateWindow(state, parent, start, end, bus);
    }

    for (ULONG offset = 0; offset < length; offset += stride) {
        uint64_t child = number(value + offset, ac);
        uint64_t host = number(value + offset + 4 * ac, pac);
        uint64_t size = number(value + offset + 4 * (ac + pac), sc);

        if (!extent(child, size, ac) || !extent(host, size, pac)) {
            return FALSE;
        }

        // Ambiguous source windows must not depend on table order.
        for (ULONG previous = 0; previous < offset; previous += stride) {
            uint64_t other = number(value + previous, ac);
            uint64_t span = number(value + previous + 4 * (ac + pac), sc);

            if (child >= other ? child - other < span : other - child < size) {
                return FALSE;
            }
        }

        uint64_t first = start > child ? start : child;
        uint64_t end = child + size - 1;

        if (end > last) {
            end = last;
        }

        if (first <= end && !translateWindow(state, parent, host + first - child,
            host + end - child, bus + first - start)) {
            return FALSE;
        }
    }

    return TRUE;
}

static BOOL collect(struct DmaDiscovery* state, APTR bridge)
{
    APTR DeviceTreeBase = state->tree;
    APTR parent = DT_GetParent(bridge);

    if (!parent || cells(DeviceTreeBase, bridge, "#address-cells", 0) != 3 ||
        cells(DeviceTreeBase, bridge, "#size-cells", 0) != 2) {
        return FALSE;
    }

    ULONG pac = cells(DeviceTreeBase, parent, "#address-cells", 2);

    if (pac != 1 && pac != 2) {
        return FALSE;
    }

    APTR property = DT_FindProperty(bridge, (CONST_STRPTR)"dma-ranges");

    if (!property) {
        return collectAcpiDma(DeviceTreeBase, bridge, addRam, state);
    }

    ULONG length = DT_GetPropLen(property), stride = 4 * (5 + pac);
    const UBYTE* value = DT_GetPropValue(property);

    if (length % stride || (length && !value)) {
        return FALSE;
    }

    if (!length) {
        return translateWindow(state, parent, 0, pac == 1 ? UINT32_MAX : UINT64_MAX, 0);
    }

    for (ULONG offset = 0; offset < length; offset += stride) {
        ULONG flags = cell(value + offset), space = flags >> 24 & 3;
        uint64_t bus = number(value + offset + 4, 2);
        uint64_t host = number(value + offset + 12, pac);
        uint64_t size = number(value + offset + 12 + 4 * pac, 2);

        if ((flags & ~UINT32_C(0xc3000000)) || (space != 2 && space != 3) ||
            !extent(bus, size, space == 2 ? 1 : 2) || !extent(host, size, pac)) {
            return FALSE;
        }

        if (!translateWindow(state, parent, host, host + size - 1, bus)) {
            return FALSE;
        }
    }

    return TRUE;
}

BOOL discoverDmaMemory(struct ExecBase* SysBase, APTR DeviceTreeBase, APTR bridge,
    APTR ram, ULONG alignment, struct PciDmaMemory* memory)
{
    if (!SysBase || !DeviceTreeBase || !bridge || !ram || !memory || memory->ranges || memory->count ||
        memory->alignment || memory->allocations || alignment < 8 || (alignment & (alignment - 1))) {
        return FALSE;
    }

    APTR parent = DT_GetParent(ram);

    if (!parent) {
        return FALSE;
    }

    ULONG ac = cells(DeviceTreeBase, parent, "#address-cells", 2);
    ULONG sc = cells(DeviceTreeBase, parent, "#size-cells", 1);

    if ((ac != 1 && ac != 2) || (sc != 1 && sc != 2)) {
        return FALSE;
    }

    APTR property = DT_FindProperty(ram, (CONST_STRPTR)"reg");

    if (!property) {
        return FALSE;
    }

    ULONG length = DT_GetPropLen(property), stride = 4 * (ac + sc);

    if (!length || length % stride || !DT_GetPropValue(property)) {
        return FALSE;
    }

    struct DmaDiscovery state = {DeviceTreeBase, ram, length / stride, 0, 0, NULL};

    // Validate every RAM record even when no inbound window intersects it.
    for (ULONG i = 0; i < state.ram_count; ++i) {
        ULONG address, size;

        if (!dtGetRegister(DeviceTreeBase, ram, i, &address, &size)) {
            return FALSE;
        }
    }

    if (!collect(&state, bridge) || !state.count) {
        return FALSE;
    }

    state.capacity = state.count;
    state.ranges = AllocMem(state.count * sizeof(*state.ranges), MEMF_PUBLIC);

    if (!state.ranges) {
        return FALSE;
    }

    state.count = 0;

    if (!collect(&state, bridge) || state.count != state.capacity || !validateDmaRanges(state.ranges, state.count)) {
        FreeMem(state.ranges, state.capacity * sizeof(*state.ranges));

        return FALSE;
    }

    *memory = (struct PciDmaMemory){state.ranges, state.count, alignment, NULL};

    return TRUE;
}

BOOL freeDmaMemory(struct ExecBase* SysBase, struct PciDmaMemory* memory)
{
    if (!SysBase || !memory || memory->allocations) {
        return FALSE;
    }

    if (memory->ranges) {
        FreeMem((APTR)memory->ranges, memory->count * sizeof(*memory->ranges));
    }

    *memory = (struct PciDmaMemory){0};

    return TRUE;
}
