/* SPDX-License-Identifier: MPL-2.0 */
#include "window.h"

#include "devicetree_address.h"
#define __NOLIBBASE__
#include <proto/devicetree.h>

static ULONG cell(const UBYTE* bytes)
{
    return (ULONG)bytes[0] << 24 | (ULONG)bytes[1] << 16 | (ULONG)bytes[2] << 8 | bytes[3];
}

static uint64_t number(const UBYTE* bytes, unsigned count)
{
    return count == 1 ? cell(bytes) : (uint64_t)cell(bytes) << 32 | cell(bytes + 4);
}

static ULONG cells(APTR DeviceTreeBase, APTR key, const char* name, ULONG fallback)
{
    APTR property = DT_FindProperty(key, (CONST_STRPTR)name);

    if (!property) {
        return fallback;
    }

    const UBYTE* bytes = DT_GetPropValue(property);

    return bytes && DT_GetPropLen(property) == 4 ? cell(bytes) : 0;
}

BOOL readPciMemoryWindow(APTR DeviceTreeBase, APTR bridge, struct PciMemoryWindow* window)
{
    if (!DeviceTreeBase || !bridge || !window) {
        return FALSE;
    }

    APTR parent = DT_GetParent(bridge);

    if (!parent || cells(DeviceTreeBase, bridge, "#address-cells", 0) != 3 ||
        cells(DeviceTreeBase, bridge, "#size-cells", 0) != 2) {
        return FALSE;
    }

    ULONG parent_cells = cells(DeviceTreeBase, parent, "#address-cells", 2);

    if (parent_cells != 1 && parent_cells != 2) {
        return FALSE;
    }

    APTR property = DT_FindProperty(bridge, (CONST_STRPTR)"ranges");

    if (!property) {
        return FALSE;
    }

    ULONG length = DT_GetPropLen(property), stride = (5 + parent_cells) * 4;
    const UBYTE* bytes = DT_GetPropValue(property);

    if (length % stride || (length && !bytes)) {
        return FALSE;
    }

    struct PciMemoryWindow selected = {0};
    uint64_t cpu_address = 0;

    for (ULONG offset = 0; offset < length; offset += stride) {
        const UBYTE* entry = bytes + offset;
        ULONG flags = cell(entry), space = flags >> 24 & 3;
        uint64_t bus = number(entry + 4, 2), cpu = number(entry + 12, parent_cells);
        uint64_t size = number(entry + 12 + 4 * parent_cells, 2);

        if ((flags & ~UINT32_C(0xc3000000)) || !space || !size ||
            size - 1 > UINT64_MAX - bus || size - 1 > UINT64_MAX - cpu ||
            (space != 3 && (bus > UINT32_MAX || size - 1 > UINT32_MAX - bus)) ||
            (parent_cells == 1 && (cpu > UINT32_MAX || size - 1 > UINT32_MAX - cpu))) {
            return FALSE;
        }

        if (space != 1 && !(flags & UINT32_C(0x40000000)) && size > selected.size) {
            selected.address = bus;
            selected.size = size;
            cpu_address = cpu;
        }
    }

    ULONG translated;

    if (selected.size && !dtTranslateAddress(DeviceTreeBase, parent, cpu_address, selected.size, &translated)) {
        return FALSE;
    }

    *window = selected;

    return TRUE;
}
