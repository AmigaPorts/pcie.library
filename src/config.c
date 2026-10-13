/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"

#include <stdint.h>
#include "devicetree_address.h"
#define __NOLIBBASE__
#include <proto/devicetree.h>

static uint32_t treeCell(const UBYTE* value)
{
    return ((uint32_t)value[0] << 24) | ((uint32_t)value[1] << 16) |
        ((uint32_t)value[2] << 8) | value[3];
}

BOOL readConfigWindow(APTR DeviceTreeBase, APTR key, struct PciConfig* config)
{
    if (!DeviceTreeBase || !key || !config) {
        return FALSE;
    }

    ULONG address, size;

    if (!dtGetRegister(DeviceTreeBase, key, 0, &address, &size) || !address || (address & 0xfffff)) {
        return FALSE;
    }

    uint32_t first = 0, last = 255;
    APTR property = DT_FindProperty(key, (CONST_STRPTR)"bus-range");

    if (property) {
        const UBYTE* value = DT_GetPropValue(property);

        if (!value || DT_GetPropLen(property) != 8) {
            return FALSE;
        }

        first = treeCell(value);
        last = treeCell(value + 4);
    }

    if (first > last || last > 255 || size < (last - first + 1) * 0x100000UL) {
        return FALSE;
    }

    struct PciConfig result = {0};

    result.address = (volatile UBYTE*)(uintptr_t)address;
    result.size = size;
    result.first_bus = first;
    result.last_bus = last;
    *config = result;

    return TRUE;
}

static volatile UBYTE* configAddress(const struct PciConfig* config, ULONG device, ULONG offset, unsigned width)
{
    if (!config || !config->address || device > 65535 ||
        (width != 1 && width != 2 && width != 4) || offset >= 4096 || (offset & (width - 1))) {
        return NULL;
    }

    unsigned bus = device >> 8;

    if (bus < config->first_bus || bus > config->last_bus) {
        return NULL;
    }

    ULONG position = ((device - ((ULONG)config->first_bus << 8)) << 12) + offset;

    if (position >= config->size || width > config->size - position) {
        return NULL;
    }

    return config->address + position;
}

static uint16_t littleWord(uint16_t value)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return __builtin_bswap16(value);
#else
    return value;
#endif
}

static uint32_t littleLong(uint32_t value)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return __builtin_bswap32(value);
#else
    return value;
#endif
}

BOOL readConfig(const struct PciConfig* config, ULONG device, ULONG offset, unsigned width, ULONG* value)
{
    if (!value) {
        return FALSE;
    }

    volatile UBYTE* address = configAddress(config, device, offset, width);

    if (!address) {
        return FALSE;
    }

    if (width == 1) {
        *value = *address;
    } else if (width == 2) {
        *value = littleWord(*(volatile uint16_t*)address);
    } else {
        *value = littleLong(*(volatile uint32_t*)address);
    }

    return TRUE;
}

BOOL writeConfig(const struct PciConfig* config, ULONG device, ULONG offset, unsigned width, ULONG value)
{
    volatile UBYTE* address = configAddress(config, device, offset, width);

    if (!address) {
        return FALSE;
    }

    if (width == 1) {
        *address = value;
    } else if (width == 2) {
        *(volatile uint16_t*)address = littleWord(value);
    } else {
        *(volatile uint32_t*)address = littleLong(value);
    }

    return TRUE;
}
