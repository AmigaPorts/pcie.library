/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "dma.h"

// Read explicit PCI dma-ranges, translate their parent addresses through
// ordinary ancestor ranges, and intersect with the supplied memory node's reg.
// Without dma-ranges, resolve the PCI root and fixed bus translations through
// the exported ACPI namespace. Missing both descriptions is unknown; an empty
// dma-ranges property means identity. No IOMMU translations are inferred.
// Neither BAR ranges nor unrelated Fast RAM substitute for inbound descriptions.
// Output starts empty and is published only after the complete map validates.
// The caller retains the immutable tree and serializes this state throughout.
BOOL discoverDmaMemory(struct ExecBase* exec_base, APTR tree_base, APTR bridge,
    APTR ram, ULONG alignment, struct PciDmaMemory* memory);

// Release only an instance created by discoverDmaMemory; refuse while buffers
// remain outstanding. Caller-supplied/static range tables are not owned here.
BOOL freeDmaMemory(struct ExecBase* exec_base, struct PciDmaMemory* memory);

// Maximum described data/unified cache line or block in a CPU/cache subtree,
// including next-level-cache/l2-cache phandles outside that subtree.
// Missing, malformed or non-power-of-two descriptions fail without changing
// output. The caller supplies the CPU subtree obtained from the public resource.
BOOL readDmaAlignment(APTR tree_base, APTR cpus, ULONG* alignment);
