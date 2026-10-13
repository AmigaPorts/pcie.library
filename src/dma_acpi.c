/* SPDX-License-Identifier: MPL-2.0 */
#include "dma_acpi.h"

#include "devicetree_address.h"
#define __NOLIBBASE__
#include <proto/devicetree.h>

static ULONG cell(const UBYTE* p)
{
    return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3];
}

static uint64_t little(const UBYTE* p, unsigned width)
{
    uint64_t value = 0;

    while (width) {
        value = value << 8 | p[--width];
    }

    return value;
}

static BOOL matches(APTR DeviceTreeBase, APTR key, const char* name, const char* text)
{
    APTR property = DT_FindProperty(key, (CONST_STRPTR)name);

    if (!property) {
        return FALSE;
    }

    const UBYTE* value = DT_GetPropValue(property);
    ULONG length = DT_GetPropLen(property);

    if (!value) {
        return FALSE;
    }

    BOOL found = FALSE;

    for (ULONG offset = 0; offset < length;) {
        ULONG start = offset, i = 0;

        while (offset < length && value[offset]) {
            ++offset;
        }

        if (offset == length) {
            return FALSE;
        }

        while (i < offset - start && text[i] && text[i] == value[start + i]) {
            ++i;
        }

        if (i == offset - start && !text[i]) {
            found = TRUE;
        }

        ++offset;
    }

    return found;
}

static BOOL integer(APTR DeviceTreeBase, APTR key, const char* name, ULONG width, ULONG* output)
{
    APTR property = DT_FindProperty(key, (CONST_STRPTR)name);

    if (!property) {
        return FALSE;
    }

    const UBYTE* data = DT_GetPropValue(property);

    if (!data || DT_GetPropLen(property) != width || (width == 8 && cell(data))) {
        return FALSE;
    }

    *output = cell(data + width - 4);

    return TRUE;
}

static BOOL identifier(APTR DeviceTreeBase, APTR key, const char* name, const char* error, ULONG* value)
{
    if (DT_FindProperty(key, (CONST_STRPTR)error)) {
        return FALSE;
    }

    *value = 0; // ACPI _SEG/_BBN default, not a device-tree address fallback.
    return !DT_FindProperty(key, (CONST_STRPTR)name) || integer(DeviceTreeBase, key, name, 8, value);
}

// _DMA describes fixed bridge windows, not an IOMMU's current translations.
// Without an IOMMU backend, refuse remapping descriptions instead of treating
// their RAM as identity DMA. Validate the complete table snapshot first so a
// missing/error table cannot be mistaken for the absence of remapping hardware.
static BOOL directDma(APTR DeviceTreeBase, APTR acpi)
{
    APTR tables = dtFindCompatible(DeviceTreeBase, acpi, NULL, "acpi,tables");
    ULONG count;

    if (!tables || !integer(DeviceTreeBase, tables, "acpi,table-count", 4, &count) || !count) {
        return FALSE;
    }

    APTR key = NULL;
    ULONG found = 0;

    while ((key = dtFindCompatible(DeviceTreeBase, tables, key, "acpi,table"))) {
        ULONG index;

        if (found == count || !integer(DeviceTreeBase, key, "acpi,index", 4, &index) || index >= count ||
            DT_FindProperty(key, (CONST_STRPTR)"acpi,table-error")) {
            return FALSE;
        }

        // Index order is not part of the DT sibling traversal contract.
        APTR previous = NULL;

        while ((previous = dtFindCompatible(DeviceTreeBase, tables, previous, "acpi,table")) != key) {
            ULONG other;

            if (!previous || !integer(DeviceTreeBase, previous, "acpi,index", 4, &other) || other == index) {
                return FALSE;
            }
        }

        APTR property = DT_FindProperty(key, (CONST_STRPTR)"acpi,table");

        if (!property) {
            return FALSE;
        }

        const UBYTE* data = DT_GetPropValue(property);
        ULONG size = DT_GetPropLen(property);

        if (!data || size < 36 || little(data + 4, 4) != size) {
            return FALSE;
        }

        ULONG signature = cell(data);

        if (signature == UINT32_C(0x444d4152) || signature == UINT32_C(0x49565253) || // DMAR, IVRS
            signature == UINT32_C(0x494f5254) || signature == UINT32_C(0x56494f54) || // IORT, VIOT
            signature == UINT32_C(0x52494d54)) {
            return FALSE;
        }                                        // RIMT
        if (signature != UINT32_C(0x46414353)) { // FACS has no checksum.
            UBYTE sum = 0;

            for (ULONG i = 0; i < size; ++i) {
                sum += data[i];
            }

            if (sum) {
                return FALSE;
            }
        }

        ++found;
    }

    return found == count;
}

struct AcpiDma {
    APTR tree, root, context;
    BOOL (*sink)
    (APTR, uint64_t, uint64_t, uint64_t);
};

static BOOL translate(struct AcpiDma* state, APTR key, uint64_t start, uint64_t last, uint64_t bus)
{
    if (key == state->root) {
        return state->sink(state->context, start, last, bus);
    }

    APTR DeviceTreeBase = state->tree;
    APTR parent = DT_GetParent(key);

    if (!parent || DT_FindProperty(key, (CONST_STRPTR)"acpi,dma-error")) {
        return FALSE;
    }

    APTR property = DT_FindProperty(key, (CONST_STRPTR)"acpi,dma");

    if (!property) {
        return translate(state, parent, start, last, bus);
    }

    // ACPI 6.6, 6.2.4: _DMA is valid only with _CRS, and its empty resource
    // template means disabled DMA. Absence, handled above, means unrestricted.
    if (!DT_FindProperty(key, (CONST_STRPTR)"acpi,crs") ||
        DT_FindProperty(key, (CONST_STRPTR)"acpi,crs-error")) {
        return FALSE;
    }

    const UBYTE* data = DT_GetPropValue(property);
    ULONG length = DT_GetPropLen(property);

    if (!data || !length) {
        return FALSE;
    }

    for (ULONG offset = 0; offset < length;) {
        UBYTE tag = data[offset++];

        if (tag == 0x79) {
            if (length - offset != 1) {
                return FALSE;
            }

            if (data[offset]) {
                UBYTE sum = 0;

                for (ULONG i = 0; i < length; ++i) {
                    sum += data[i];
                }

                if (sum) {
                    return FALSE;
                }
            }

            return TRUE;
        }

        if (!(tag & 0x80) || length - offset < 2) {
            return FALSE;
        }

        ULONG size = little(data + offset, 2);

        offset += 2;

        if (size > length - offset) {
            return FALSE;
        }

        const UBYTE* p = data + offset;

        offset += size;

        unsigned width = tag == 0x88 ? 2 : tag == 0x87 ? 4
            : tag == 0x8a || tag == 0x8b               ? 8
                                                       : 0;
        unsigned header = tag == 0x8b ? 5 : 3;

        // Named resource sources need separate resolution; do not drop them.
        if (!width || size != header + width * (tag == 0x8b ? 6 : 5) ||
            (tag == 0x8b && (p[3] != 1 || p[4])) || p[0] != 0 || p[1] != 0x0c || (p[2] & 0xe0)) {
            return FALSE;
        }

        uint64_t minimum = little(p + header + width, width);
        uint64_t maximum = little(p + header + 2 * width, width);
        uint64_t delta = little(p + header + 3 * width, width);
        uint64_t span = little(p + header + 4 * width, width);

        if (!span) {
            continue;
        }

        if (maximum < minimum || span - 1 != maximum - minimum ||
            delta > UINT64_MAX - maximum) {
            return FALSE;
        }

        uint64_t first = start > minimum ? start : minimum;
        uint64_t end = last < maximum ? last : maximum;

        if (first <= end && !translate(state, parent, first + delta, end + delta, bus + first - start)) {
            return FALSE;
        }
    }

    return FALSE; // Truncated/missing EndTag.
}

BOOL collectAcpiDma(APTR DeviceTreeBase, APTR bridge,
    BOOL (*sink)(APTR, uint64_t, uint64_t, uint64_t), APTR context)
{
    if (!DeviceTreeBase || !bridge || !sink ||
        !matches(DeviceTreeBase, bridge, "compatible", "pci-host-ecam-generic")) {
        return FALSE;
    }

    APTR root = bridge, parent;

    for (;;) {
        if (DT_FindProperty(root, (CONST_STRPTR)"iommus") ||
            DT_FindProperty(root, (CONST_STRPTR)"iommu-map")) {
            return FALSE;
        }

        parent = DT_GetParent(root);

        if (!parent) {
            break;
        }

        root = parent;
    }

    APTR acpi = dtFindCompatible(DeviceTreeBase, root, NULL, "emu68,acpi-namespace");

    if (!acpi || DT_GetParent(acpi) != root ||
        dtFindCompatible(DeviceTreeBase, root, acpi, "emu68,acpi-namespace") || !directDma(DeviceTreeBase, acpi)) {
        return FALSE;
    }

    ULONG domain = 0;

    if (DT_FindProperty(bridge, (CONST_STRPTR)"linux,pci-domain") &&
        !integer(DeviceTreeBase, bridge, "linux,pci-domain", 4, &domain)) {
        return FALSE;
    }

    APTR property = DT_FindProperty(bridge, (CONST_STRPTR)"bus-range");

    if (!property || DT_GetPropLen(property) != 8) {
        return FALSE;
    }

    const UBYTE* range = DT_GetPropValue(property);

    if (!range || domain > 65535 || cell(range) > cell(range + 4) || cell(range + 4) > 255) {
        return FALSE;
    }

    APTR key = NULL, host = NULL;

    while ((key = dtFindCompatible(DeviceTreeBase, acpi, key, "acpi,device"))) {
        if (DT_FindProperty(key, (CONST_STRPTR)"acpi,alias")) {
            continue;
        }

        if (DT_FindProperty(key, (CONST_STRPTR)"acpi,hid-error") ||
            DT_FindProperty(key, (CONST_STRPTR)"acpi,cid-error")) {
            return FALSE;
        }

        if (!matches(DeviceTreeBase, key, "acpi,hid", "PNP0A03") &&
            !matches(DeviceTreeBase, key, "acpi,hid", "PNP0A08") &&
            !matches(DeviceTreeBase, key, "acpi,cid", "PNP0A03") &&
            !matches(DeviceTreeBase, key, "acpi,cid", "PNP0A08")) {
            continue;
        }

        ULONG segment, bus;

        if (!identifier(DeviceTreeBase, key, "acpi,seg", "acpi,seg-error", &segment) || segment > 65535 ||
            !identifier(DeviceTreeBase, key, "acpi,bbn", "acpi,bbn-error", &bus) || bus > 255) {
            return FALSE;
        }

        if (segment == domain && bus == cell(range)) {
            if (host) {
                return FALSE;
            }

            host = key;
        }
    }

    if (!host) {
        return FALSE;
    }

    struct AcpiDma state = {DeviceTreeBase, acpi, context, sink};

    return translate(&state, host, 0, UINT64_MAX, 0);
}
