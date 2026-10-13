/* SPDX-License-Identifier: MPL-2.0 */
#include "catalog.h"

BOOL prepareDeviceResources(APTR tree_base, APTR bridge, struct PciDevice* device,
    const struct PciResources* resources)
{
    if (!device || !resources || device->interrupt_set || device->interrupt_allocation || device->interrupt_type ||
        device->interrupt_server || device->interrupt_provider || device->interrupt_library) {
        return FALSE;
    }

    unsigned header = device->info.function.header_type & 0x7f;

    if (header > 1 || resources->count != (header ? 2 : 6)) {
        return FALSE;
    }

    struct PciMappings mappings;

    if (!translateResources(tree_base, bridge, resources, &mappings)) {
        return FALSE;
    }

    // Commit only after validating every interval. The public ABI retains the
    // low sizing-response word, not a decoded size. A 64-bit BAR's upper slot
    // remains empty; the full response stays in the private resource record.
    for (unsigned i = 0; i < 6; ++i) {
        device->board.base_address[i] = mappings.bars[i];
        device->board.base_size[i] = resources->bars[i].size ? (ULONG)resources->bars[i].mask : 0;
    }

    device->resources = *resources;
    device->mappings = mappings;
    device->resources_prepared = TRUE;

    return TRUE;
}
