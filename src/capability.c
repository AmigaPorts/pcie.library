/* SPDX-License-Identifier: MPL-2.0 */
#include "capability.h"

static BOOL present(const struct PciConfig* config, ULONG device)
{
    ULONG id;

    return readConfig(config, device, 0, 4, &id) && (id & 65535) != 65535 && (id & 65535) != 1;
}

BOOL readCapability(const struct PciConfig* config, ULONG device, UBYTE capability, ULONG* result)
{
    ULONG status, header, offset;

    if (!result || !present(config, device) || !readConfig(config, device, 6, 2, &status)) {
        return FALSE;
    }

    if (!(status & 16)) {
        *result = 0;

        return TRUE;
    }

    if (!readConfig(config, device, 14, 1, &header)) {
        return FALSE;
    }

    header &= 0x7f;

    if (header > 2 || !readConfig(config, device, header == 2 ? 0x14 : 0x34, 1, &offset)) {
        return FALSE;
    }

    unsigned first = header == 2 ? 0x48 : 0x40;
    ULONG visited[2] = {0};

    // Conventional capability pointers reserve their low two bits.
    while ((offset &= 0xfc)) {
        if (offset < first) {
            return FALSE;
        }

        unsigned index = offset >> 2;
        ULONG bit = 1UL << (index & 31);

        if (visited[index >> 5] & bit) {
            return FALSE;
        }

        visited[index >> 5] |= bit;

        ULONG value;

        if (!readConfig(config, device, offset, 2, &value) || (value & 255) == 255) {
            return FALSE;
        }

        if ((value & 255) == capability) {
            *result = offset;

            return TRUE;
        }

        offset = value >> 8;
    }

    *result = 0;

    return TRUE;
}

ULONG findCapability(const struct PciConfig* config, ULONG device, UBYTE capability)
{
    ULONG offset = 0;

    readCapability(config, device, capability, &offset);

    return offset;
}

ULONG findExtCapability(const struct PciConfig* config, ULONG device, UWORD capability)
{
    if (!present(config, device)) {
        return 0;
    }

    ULONG visited[32] = {0};
    ULONG offset = 0x100;

    while (offset) {
        if (offset < 0x100 || offset > 0xffc || (offset & 3)) {
            return 0;
        }

        unsigned index = offset >> 2;
        ULONG bit = 1UL << (index & 31);

        if (visited[index >> 5] & bit) {
            return 0;
        }

        visited[index >> 5] |= bit;

        ULONG value;

        if (!readConfig(config, device, offset, 4, &value) || !value || value == 0xffffffff) {
            return 0;
        }

        if ((value & 65535) == capability) {
            return offset;
        }

        offset = value >> 20;
    }

    return 0;
}
