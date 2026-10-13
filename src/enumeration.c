/* SPDX-License-Identifier: MPL-2.0 */
#include "enumeration.h"

#include <stdint.h>

enum PciScanResult nextPciFunction(const struct PciConfig* config, ULONG* cursor,
    struct PciFunction* function)
{
    if (!config || !config->address || !cursor || !function || config->first_bus > config->last_bus) {
        return PCI_SCAN_ERROR;
    }

    ULONG first = (ULONG)config->first_bus << 8;
    ULONG end = ((ULONG)config->last_bus + 1) << 8;
    ULONG size = (end - first) << 12;

    if (*cursor < first || *cursor > end || config->size < size ||
        (uintptr_t)config->address & 3 || size - 1 > UINTPTR_MAX - (uintptr_t)config->address) {
        return PCI_SCAN_ERROR;
    }

    ULONG device = *cursor;

    while (device < end) {
        ULONG slot = device & ~7UL;
        ULONG id, header;

        if (!readConfig(config, slot, 0, 4, &id)) {
            return PCI_SCAN_ERROR;
        }

        if ((id & 65535) == 1) {
            return PCI_SCAN_RETRY;
        } // PCIe CRS software visibility.
        if ((id & 65535) == 65535) {
            device = slot + 8;
            continue;
        }

        if (!readConfig(config, slot, 14, 1, &header)) {
            return PCI_SCAN_ERROR;
        }

        ULONG limit = slot + ((header & 0x80) ? 8 : 1);

        while (device < limit) {
            ULONG current_id = id, current_header = header, class_revision;

            if (device != slot) {
                if (!readConfig(config, device, 0, 4, &current_id)) {
                    return PCI_SCAN_ERROR;
                }

                if ((current_id & 65535) == 1) {
                    return PCI_SCAN_RETRY;
                }

                if ((current_id & 65535) == 65535) {
                    ++device;
                    continue;
                }

                if (!readConfig(config, device, 14, 1, &current_header)) {
                    return PCI_SCAN_ERROR;
                }
            }

            if (!readConfig(config, device, 8, 4, &class_revision)) {
                return PCI_SCAN_ERROR;
            }

            *function = (struct PciFunction){
                .device = device, .vendor_id = current_id & 65535, .device_id = current_id >> 16,
                .class_code = class_revision >> 8, .revision = class_revision & 255, .header_type = current_header
            };
            *cursor = device + 1 == limit ? slot + 8 : device + 1;

            return PCI_SCAN_FUNCTION;
        }

        device = slot + 8;
    }

    *cursor = end;

    return PCI_SCAN_END;
}
