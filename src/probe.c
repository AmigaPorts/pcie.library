/* SPDX-License-Identifier: MPL-2.0 */
#include "probe.h"

static BOOL probeRegister(const struct PciConfig* config, ULONG device, ULONG offset,
    ULONG low, ULONG high, BOOL wide, ULONG probe, ULONG* low_mask, ULONG* high_mask)
{
    BOOL good = writeConfig(config, device, offset, 4, probe);

    if (good && wide) {
        good = writeConfig(config, device, offset + 4, 4, 0xffffffff);
    }

    if (good) {
        good = readConfig(config, device, offset, 4, low_mask);
    }

    if (good && wide) {
        good = readConfig(config, device, offset + 4, 4, high_mask);
    }

    // Do not short-circuit restoration when a transaction fails. Both halves
    // are restored before any caller can use the function's resources again.
    if (wide && !writeConfig(config, device, offset + 4, 4, high)) {
        good = FALSE;
    }

    if (!writeConfig(config, device, offset, 4, low)) {
        good = FALSE;
    }

    ULONG value;

    if (!readConfig(config, device, offset, 4, &value) || value != low) {
        good = FALSE;
    }

    if (wide && (!readConfig(config, device, offset + 4, 4, &value) || value != high)) {
        good = FALSE;
    }

    return good;
}

BOOL probeResources(const struct PciConfig* config, ULONG device, struct PciResources* resources)
{
    ULONG id, command, header;

    if (!resources || !readConfig(config, device, 0, 4, &id) ||
        (id & 65535) == 65535 || (id & 65535) == 1 ||
        !readConfig(config, device, 4, 2, &command) || (command & 3) ||
        !readConfig(config, device, 14, 1, &header)) {
        return FALSE;
    }

    header &= 0x7f;

    if (header > 1) {
        return FALSE;
    }

    struct PciResources result = {0};

    result.count = header ? 2 : 6;

    ULONG saved[6], rom;
    ULONG rom_offset = header ? 0x38 : 0x30;

    // Snapshot and validate the complete header before the first write.
    for (unsigned i = 0; i < result.count; ++i) {
        if (!readConfig(config, device, 0x10 + 4 * i, 4, &saved[i])) {
            return FALSE;
        }
    }

    if (!readConfig(config, device, rom_offset, 4, &rom)) {
        return FALSE;
    }

    for (unsigned i = 0; i < result.count; ++i) {
        if (saved[i] & 1) {
            if (saved[i] & 2) {
                return FALSE;
            }
        } else {
            unsigned type = saved[i] & 6;

            if (type == 6) {
                return FALSE;
            }

            if (type == 4) {
                if (i + 1 == result.count) {
                    return FALSE;
                }

                ++i;
            }
        }
    }

    for (unsigned i = 0; i < result.count; ++i) {
        BOOL wide = (saved[i] & 7) == 4;
        ULONG high = wide ? saved[i + 1] : 0, low_mask = 0, high_mask = 0;

        if (!probeRegister(config, device, 0x10 + 4 * i, saved[i], high, wide,
                0xffffffff, &low_mask, &high_mask) ||
            !decodeBar(saved[i], high, low_mask, high_mask, &result.bars[i])) {
            return FALSE;
        }

        if (wide) {
            ++i;
        }
    }

    ULONG mask;

    // Keep the expansion-ROM enable bit clear throughout sizing.
    if (!probeRegister(config, device, rom_offset, rom, 0, FALSE, 0xfffff800, &mask, NULL) ||
        !decodeRomBar(rom, mask, &result.rom)) {
        return FALSE;
    }

    *resources = result;

    return TRUE;
}
