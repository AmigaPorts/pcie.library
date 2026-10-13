/* SPDX-License-Identifier: MPL-2.0 */
#include "catalog.h"

BOOL prepareAssignedDevice(APTR tree_base, APTR bridge, struct PciCatalog* catalog, struct pci_dev* board)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!tree_base || !bridge || !device || device->interrupt_set || device->interrupt_allocation ||
        device->interrupt_type || device->interrupt_server || device->interrupt_provider ||
        device->interrupt_library) {
        return FALSE;
    }

    const struct PciConfig* config = device->config;
    ULONG slot = device->info.function.device;
    ULONG identity, header, command;

    if (!readConfig(config, slot, 0, 4, &identity) ||
        identity != ((ULONG)device->info.function.device_id << 16 | device->info.function.vendor_id) ||
        !readConfig(config, slot, 14, 1, &header) || (header & 0x7f) > 1 ||
        (header & 0x7f) != (device->info.function.header_type & 0x7f) ||
        !readConfig(config, slot, 4, 2, &command)) {
        return FALSE;
    }

    // Host and ISA bridges describe system decoding, not endpoint BARs. Their
    // decode-enable bits can be read-only one (RAM and legacy I/O stay enabled).
    // A type-0 system bridge with no assigned BAR or ROM needs no sizing writes;
    // the host apertures are described by the firmware's device-tree ranges.
    // Do not apply this to endpoints or silently discard a nonzero bridge BAR.
    ULONG bridge_class = device->info.function.class_code >> 8;

    if ((bridge_class == 0x0600 || bridge_class == 0x0601) && !(header & 0x7f)) {
        BOOL empty = TRUE;

        for (unsigned i = 0; i < 7; ++i) {
            ULONG value;

            if (!readConfig(config, slot, i == 6 ? 0x30 : 0x10 + 4 * i, 4, &value)) {
                return FALSE;
            }

            if (value) {
                empty = FALSE;
            }
        }

        if (empty) {
            const struct PciResources resources = {.count = 6};

            return prepareDeviceResources(tree_base, bridge, device, &resources);
        }
    }

    ULONG disabled = command & ~3UL, observed;

    // Word transactions preserve adjacent write-one-to-clear status. The caller
    // excludes BAR consumers. Bus mastering controls outgoing transactions, not
    // BAR decoding; preserve it throughout rather than trying to stop device DMA.
    if (command != disabled) {
        BOOL written = writeConfig(config, slot, 4, 2, disabled);
        BOOL read = readConfig(config, slot, 4, 2, &observed);

        if (!written || !read || observed != disabled) {
            // No BAR has been touched, so restoring the previous command is safe.
            writeConfig(config, slot, 4, 2, command);

            return FALSE;
        }
    }

    struct PciResources resources;

    if (!probeResources(config, slot, &resources)) {
        // A failed probe can include an unverified BAR restore. Do not restore
        // decode enables in that case; never expose a partly prepared record.
        return FALSE;
    }

    struct PciDevice pending = *device;
    BOOL mapped = prepareDeviceResources(tree_base, bridge, &pending, &resources);

    // Even an unavailable mapping must leave a successfully restored device in
    // its original decode state. It does not authorize reassignment or publication.
    if (command != disabled && !writeConfig(config, slot, 4, 2, command)) {
        return FALSE;
    }

    if (!readConfig(config, slot, 4, 2, &observed) || observed != command || !mapped) {
        return FALSE;
    }

    *device = pending;

    return TRUE;
}
