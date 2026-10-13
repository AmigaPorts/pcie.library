/* SPDX-License-Identifier: MPL-2.0 */
#include "address.h"

#include "devicetree_address.h"
#define __NOLIBBASE__
#include <proto/devicetree.h>

static uint32_t cell(const UBYTE* value)
{
    return ((uint32_t)value[0] << 24) | ((uint32_t)value[1] << 16) |
        ((uint32_t)value[2] << 8) | value[3];
}

static uint64_t number(const UBYTE* value, unsigned cells)
{
    return cells == 2 ? ((uint64_t)cell(value) << 32) | cell(value + 4) : cell(value);
}

static unsigned cellCount(APTR DeviceTreeBase, APTR key, const char* name, unsigned fallback)
{
    APTR property = DT_FindProperty(key, (CONST_STRPTR)name);

    if (!property) {
        return fallback;
    }

    const UBYTE* value = DT_GetPropValue(property);

    if (!value || DT_GetPropLen(property) != 4) {
        return 0;
    }

    return cell(value);
}

static BOOL propertyString(APTR DeviceTreeBase, APTR key, const char* name, const char* expected)
{
    APTR property = DT_FindProperty(key, (CONST_STRPTR)name);

    if (!property) {
        return FALSE;
    }

    const UBYTE* data = DT_GetPropValue(property);
    ULONG length = DT_GetPropLen(property);

    if (!data) {
        return FALSE;
    }

    BOOL found = FALSE;

    for (ULONG offset = 0; offset < length;) {
        ULONG start = offset, i = 0;

        while (offset < length && data[offset]) {
            ++offset;
        }

        if (offset == length) {
            return FALSE;
        }

        while (i < offset - start && expected[i] && data[start + i] == expected[i]) {
            ++i;
        }

        if (i == offset - start && !expected[i]) {
            found = TRUE;
        }

        ++offset;
    }

    return found;
}

static BOOL acpiInteger(APTR DeviceTreeBase, APTR key, const char* name, const char* error, ULONG* result)
{
    if (DT_FindProperty(key, (CONST_STRPTR)error)) {
        return FALSE;
    }

    APTR property = DT_FindProperty(key, (CONST_STRPTR)name);
    *result = 0; // ACPI's _SEG and _BBN defaults.
    if (!property) {
        return TRUE;
    }

    const UBYTE* data = DT_GetPropValue(property);

    if (!data || DT_GetPropLen(property) != 8 || cell(data)) {
        return FALSE;
    }

    *result = cell(data + 4);

    return TRUE;
}

static uint64_t littleNumber(const UBYTE* data, unsigned width)
{
    uint64_t value = 0;

    while (width) {
        value = value << 8 | data[--width];
    }

    return value;
}

// A root bridge's _CRS supplies the PCI-to-port translation. The already
// exported port window supplies the port-to-guest translation. Neither address
// is a platform constant, and memory ranges never imply identity port access.
static BOOL translateAcpiPort(APTR DeviceTreeBase, APTR bridge, uint64_t base, uint64_t size, ULONG* address)
{
    APTR root = bridge, parent;

    while ((parent = DT_GetParent(root))) {
        root = parent;
    }

    APTR acpi = dtFindCompatible(DeviceTreeBase, root, NULL, "emu68,acpi-namespace");

    if (!acpi || DT_GetParent(acpi) != root ||
        dtFindCompatible(DeviceTreeBase, root, acpi, "emu68,acpi-namespace") ||
        !propertyString(DeviceTreeBase, bridge, "compatible", "pci-host-ecam-generic")) {
        return FALSE;
    }

    APTR property = DT_FindProperty(bridge, (CONST_STRPTR)"bus-range");

    if (!property || DT_GetPropLen(property) != 8) {
        return FALSE;
    }

    const UBYTE* data = DT_GetPropValue(property);

    if (!data || cell(data) > cell(data + 4) || cell(data + 4) > 255) {
        return FALSE;
    }

    ULONG first_bus = cell(data), domain = 0;

    property = DT_FindProperty(bridge, (CONST_STRPTR)"linux,pci-domain");

    if (property) {
        data = DT_GetPropValue(property);

        if (!data || DT_GetPropLen(property) != 4 || cell(data) > 65535) {
            return FALSE;
        }

        domain = cell(data);
    }

    APTR host = NULL, key = NULL;

    while ((key = dtFindCompatible(DeviceTreeBase, acpi, key, "acpi,device"))) {
        if (DT_FindProperty(key, (CONST_STRPTR)"acpi,alias")) {
            continue;
        }

        if (DT_FindProperty(key, (CONST_STRPTR)"acpi,hid-error") ||
            DT_FindProperty(key, (CONST_STRPTR)"acpi,cid-error")) {
            return FALSE;
        }

        if (!propertyString(DeviceTreeBase, key, "acpi,hid", "PNP0A03") &&
            !propertyString(DeviceTreeBase, key, "acpi,hid", "PNP0A08") &&
            !propertyString(DeviceTreeBase, key, "acpi,cid", "PNP0A03") &&
            !propertyString(DeviceTreeBase, key, "acpi,cid", "PNP0A08")) {
            continue;
        }

        ULONG segment, bus;

        if (!acpiInteger(DeviceTreeBase, key, "acpi,seg", "acpi,seg-error", &segment) || segment > 65535 ||
            !acpiInteger(DeviceTreeBase, key, "acpi,bbn", "acpi,bbn-error", &bus) || bus > 255) {
            return FALSE;
        }

        if (domain == segment && first_bus == bus) {
            if (host) {
                return FALSE;
            }

            host = key;
        }
    }

    if (!host || DT_FindProperty(host, (CONST_STRPTR)"acpi,crs-error")) {
        return FALSE;
    }

    property = DT_FindProperty(acpi, (CONST_STRPTR)"acpi,io-port-window");

    if (!property || DT_GetPropLen(property) != 8) {
        return FALSE;
    }

    data = DT_GetPropValue(property);

    if (!data) {
        return FALSE;
    }

    ULONG guest = cell(data), window = cell(data + 4);

    if (!guest || !window || window > 65536 || window - 1 > UINT32_MAX - guest) {
        return FALSE;
    }

    property = DT_FindProperty(host, (CONST_STRPTR)"acpi,crs");

    if (!property) {
        return FALSE;
    }

    data = DT_GetPropValue(property);

    ULONG length = DT_GetPropLen(property);

    if (!data || !length) {
        return FALSE;
    }

    BOOL found = FALSE;
    ULONG translated = 0;

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

            if (found) {
                *address = translated;
            }

            return found;
        }

        ULONG bytes = tag & 7;

        if (tag & 0x80) {
            if (length - offset < 2) {
                return FALSE;
            }

            bytes = littleNumber(data + offset, 2);
            offset += 2;
        }

        if (bytes > length - offset) {
            return FALSE;
        }

        const UBYTE* descriptor = data + offset;

        offset += bytes;

        unsigned width = tag == 0x88 ? 2 : tag == 0x87 ? 4
            : tag == 0x8a || tag == 0x8b               ? 8
                                                       : 0;

        if (!width) {
            // Dependent alternatives are not a resolved current assignment.
            if (!(tag & 0x80) && ((tag >> 3) == 6 || (tag >> 3) == 7)) {
                return FALSE;
            }

            continue;
        }

        unsigned header = tag == 0x8b ? 5 : 3;

        if (bytes < header + width * (tag == 0x8b ? 6 : 5)) {
            return FALSE;
        }

        if (descriptor[0] != 1 || (descriptor[1] & 1)) {
            continue;
        } // Only producer I/O apertures.
        // Fixed positive/subtractive, dense port windows. Named sources and
        // port-to-memory windows cannot be resolved through the native port map.
        if (bytes != header + width * (tag == 0x8b ? 6 : 5) ||
            (descriptor[1] & ~2) != 0x0c || (descriptor[2] & ~3) || !(descriptor[2] & 3) ||
            (tag == 0x8b && (descriptor[3] != 1 || descriptor[4] || littleNumber(descriptor + 45, 8)))) {
            return FALSE;
        }

        uint64_t minimum = littleNumber(descriptor + header + width, width);
        uint64_t maximum = littleNumber(descriptor + header + width * 2, width);
        uint64_t delta = littleNumber(descriptor + header + width * 3, width);
        uint64_t span = littleNumber(descriptor + header + width * 4, width);

        if (!span) {
            continue;
        }

        if (maximum < minimum || span - 1 != maximum - minimum || delta > UINT64_MAX - maximum ||
            maximum > UINT32_MAX || maximum + delta >= window) {
            return FALSE;
        }

        if (base < minimum || base > maximum || size - 1 > maximum - base) {
            continue;
        }

        // ACPI ISA/non-ISA selection repeats every 1 KiB. The complete BAR must
        // lie in the selected class; never map across a hole in that aperture.
        unsigned selection = descriptor[2] & 3;
        unsigned low = base & 0x3ff;

        if (selection != 3 && ((low < 0x100 ? 2 : 1) != selection ||
            size > (low < 0x100 ? 0x100 - low : 0x400 - low))) {
            continue;
        }

        if (found) {
            return FALSE;
        }

        translated = guest + (ULONG)(base + delta);
        found = TRUE;
    }

    return FALSE;
}

BOOL translatePciAddress(APTR DeviceTreeBase, APTR bridge, enum PciAddressSpace space,
    uint64_t base, uint64_t size, ULONG* address)
{
    if (!DeviceTreeBase || !bridge || !address || space < PCI_IO || space > PCI_MEMORY64 ||
        !size || size - 1 > UINT64_MAX - base ||
        (space != PCI_MEMORY64 && (base > UINT32_MAX || size - 1 > UINT32_MAX - base))) {
        return FALSE;
    }

    APTR parent = DT_GetParent(bridge);

    if (!parent || cellCount(DeviceTreeBase, bridge, "#address-cells", 0) != 3 ||
        cellCount(DeviceTreeBase, bridge, "#size-cells", 0) != 2) {
        return FALSE;
    }

    unsigned pac = cellCount(DeviceTreeBase, parent, "#address-cells", 2);

    if (pac != 1 && pac != 2) {
        return FALSE;
    }

    APTR property = DT_FindProperty(bridge, (CONST_STRPTR)"ranges");

    if (!property) {
        return FALSE;
    }

    const UBYTE* value = DT_GetPropValue(property);
    ULONG length = DT_GetPropLen(property), stride = 4 * (3 + pac + 2);

    // PCI spaces cannot be inferred from an empty ordinary identity mapping.
    if (!value || !length || length % stride) {
        return FALSE;
    }

    BOOL found = FALSE;
    uint64_t translated = 0;

    for (ULONG offset = 0; offset < length; offset += stride) {
        const UBYTE* entry = value + offset;
        uint32_t flags = cell(entry), type = (flags >> 24) & 3;
        uint64_t child = number(entry + 4, 2), host = number(entry + 12, pac);
        uint64_t window = number(entry + 12 + 4 * pac, 2);

        // A ranges child address contains only n, p and ss in phys.hi.
        if ((flags & ~UINT32_C(0xc3000000)) || !type || !window ||
            window - 1 > UINT64_MAX - child || window - 1 > UINT64_MAX - host ||
            (type != PCI_MEMORY64 && (child > UINT32_MAX || window - 1 > UINT32_MAX - child)) ||
            (pac == 1 && (host > UINT32_MAX || window - 1 > UINT32_MAX - host))) {
            return FALSE;
        }

        // PCI binding section 12: range selection compares ss, not n or p.
        if (type == (unsigned)space && base >= child && base - child < window &&
            size <= window - (base - child)) {
            if (found) {
                return FALSE;
            }

            translated = host + (base - child);
            found = TRUE;
        }
    }

    if (found) {
        return dtTranslateAddress(DeviceTreeBase, parent, translated, size, address);
    }

    return space == PCI_IO && translateAcpiPort(DeviceTreeBase, bridge, base, size, address);
}
