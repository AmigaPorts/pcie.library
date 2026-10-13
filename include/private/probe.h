/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "config.h"
#include "resource.h"

struct PciResources {
    struct PciBar bars[6];
    struct PciBar rom;
    UBYTE count;
};

// Internal operation for a library-owned function. The caller must
// exclude configuration changes and all users of its resources for the entire
// operation. I/O and memory decoding must already be disabled. Bus mastering
// controls outgoing transactions and is preserved; this does not stop DMA.
// Supports endpoint and PCI bridge headers. Upper halves of 64-bit BARs remain
// empty in the result. Bridge forwarding windows are not BARs and are untouched.
// Restore every probed BAR before decoding its response, including on errors.
// Failure leaves output unchanged; an inaccessible/disappearing device can still
// prevent restoration. No command/status writes, allocation or address mapping.
BOOL probeResources(const struct PciConfig* config, ULONG device, struct PciResources* resources);
