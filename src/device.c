/* SPDX-License-Identifier: MPL-2.0 */
#include "catalog.h"
#include "capability.h"

BOOL readDeviceInfo(const struct PciConfig* config, const struct PciFunction* function, struct PciDeviceInfo* info)
{
    if (!function || !info) {
        return FALSE;
    }

    struct PciDeviceInfo result = {0};

    result.function = *function;

    ULONG value;

    if (!readConfig(config, function->device, 4, 2, &value)) {
        return FALSE;
    }

    result.command = value;

    if (!readConfig(config, function->device, 0x3c, 2, &value)) {
        return FALSE;
    }

    result.interrupt_line = value;
    result.interrupt_pin = value >> 8;

    unsigned type = function->header_type & 0x7f;
    ULONG subsystem;

    if (type == 0) {
        subsystem = 0x2c;
    } else if (type == 2) {
        subsystem = 0x40;
    } else if (type == 1) {
        if (!readCapability(config, function->device, 0x0d, &subsystem)) {
            return FALSE;
        }

        if (subsystem) {
            if (subsystem > 0xf8) {
                return FALSE;
            }

            subsystem += 4;
        }
    } else {
        return FALSE;
    }

    if (subsystem) {
        if (!readConfig(config, function->device, subsystem, 4, &value)) {
            return FALSE;
        }

        result.subsystem_vendor = value;
        result.subsystem_device = value >> 16;
        result.has_subsystem = TRUE;
    }

    *info = result;

    return TRUE;
}
