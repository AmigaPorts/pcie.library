/* SPDX-License-Identifier: MPL-2.0 */
#include "dma_discovery.h"

#include "devicetree_interrupt.h"
#define __NOLIBBASE__
#include <proto/devicetree.h>

static ULONG cell(const UBYTE* value)
{
    return (ULONG)value[0] << 24 | (ULONG)value[1] << 16 | (ULONG)value[2] << 8 | value[3];
}

struct CachePath {
    APTR key;
    const struct CachePath* previous;
};

static BOOL cacheAlignment(APTR DeviceTreeBase, APTR root, APTR key,
    const struct CachePath* previous, BOOL required, ULONG* largest)
{
    for (const struct CachePath* path = previous; path; path = path->previous) {
        if (path->key == key) {
            return FALSE;
        }
    }

    struct CachePath path = {key, previous};
    const char* names[] = {"d-cache-line-size", "cache-line-size", "d-cache-block-size", "cache-block-size"};
    BOOL described = FALSE;

    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        APTR property = DT_FindProperty(key, (CONST_STRPTR)names[i]);

        if (!property) {
            continue;
        }

        const UBYTE* value = DT_GetPropValue(property);

        if (!value || DT_GetPropLen(property) != 4) {
            return FALSE;
        }

        ULONG size = cell(value);

        if (size < 8 || (size & (size - 1))) {
            return FALSE;
        }

        if (size > *largest) {
            *largest = size;
        }

        described = TRUE;
    }

    APTR type = DT_FindProperty(key, (CONST_STRPTR)"device_type");

    if (type && DT_GetPropLen(type) == 4) {
        const UBYTE* value = DT_GetPropValue(type);

        if (value && value[0] == 'c' && value[1] == 'p' && value[2] == 'u' && !value[3]) {
            required = TRUE;
        }
    }

    if (required && !described) {
        return FALSE;
    }

    APTR next = DT_FindProperty(key, (CONST_STRPTR)"next-level-cache");

    if (!next) {
        next = DT_FindProperty(key, (CONST_STRPTR)"l2-cache");
    }

    if (next) {
        const UBYTE* value = DT_GetPropValue(next);

        if (!value || DT_GetPropLen(next) != 4) {
            return FALSE;
        }

        APTR cache = dtFindPhandle(DeviceTreeBase, root, cell(value));

        if (!cache || !cacheAlignment(DeviceTreeBase, root, cache, &path, TRUE, largest)) {
            return FALSE;
        }
    }

    APTR child = NULL;

    while ((child = DT_GetChild(key, child))) {
        if (!cacheAlignment(DeviceTreeBase, root, child, &path, FALSE, largest)) {
            return FALSE;
        }
    }

    return TRUE;
}

BOOL readDmaAlignment(APTR DeviceTreeBase, APTR cpus, ULONG* alignment)
{
    if (!DeviceTreeBase || !cpus || !alignment) {
        return FALSE;
    }

    APTR root = cpus, parent;

    while ((parent = DT_GetParent(root))) {
        root = parent;
    }

    ULONG largest = 0;

    if (!cacheAlignment(DeviceTreeBase, root, cpus, NULL, FALSE, &largest) || !largest) {
        return FALSE;
    }

    *alignment = largest;

    return TRUE;
}
