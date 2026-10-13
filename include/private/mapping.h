/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "address.h"
#include "probe.h"

struct PciMappings {
    ULONG bars[6];
    ULONG rom;
};

// Resolve every implemented BAR through existing device-tree ranges. The caller
// must supply assigned resources and retain their raw PCI addresses/masks; these
// outputs are 68k pointers, not replacement BAR values. No address allocation,
// configuration writes or MMU operations occur here. Absent BARs and the upper
// halves of 64-bit BARs have zero pointers, as do disabled expansion ROMs.
// Enabled ROMs must translate like other resources. Failure leaves output unchanged:
// never publish a partial set of mappings or truncate an inaccessible resource.
BOOL translateResources(APTR tree_base, APTR bridge, const struct PciResources* resources,
    struct PciMappings* mappings);
