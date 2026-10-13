/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <exec/types.h>
#include <stdint.h>

struct PciMemoryWindow {
    uint64_t address;
    uint64_t size;
};

// Select the host's largest non-prefetchable memory range, retaining the first
// on ties, as required by the existing memory-window attributes. Use only public
// device-tree descriptions. Validate the complete ranges property and translate
// the selected interval through ordinary ancestors before publishing it. An
// explicitly empty property or no eligible range yields a zero window; missing
// or malformed descriptions fail and preserve output. Retain full PCI width.
BOOL readPciMemoryWindow(APTR tree_base, APTR bridge, struct PciMemoryWindow* window);
