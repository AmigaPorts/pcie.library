/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "host.h"
#include <exec/semaphores.h>

// One state object for the primary library and its compatibility alias.
// The Resident owns publication/open counts; no library is registered here.
struct PciState {
    struct ExecBase* exec_base;
    APTR tree_base, root;
    struct Library* interrupt_base;
    struct SignalSemaphore semaphore;
    struct PciHosts hosts;
};

// Initialize unpublished state before PCI clients run. Firmware-assigned
// BAR consumers must be excluded; bus mastering is preserved throughout.
// Resolve dependencies, ranges and IRQ descriptions before any BAR probes.
// Publish only after every function is prepared. Failure releases software
// ownership and preserves the caller's zeroed result; failed BAR restoration
// can leave that device's decoding disabled, as documented by its probe.
BOOL initializePciState(struct ExecBase* exec_base, struct PciState* result);

// The caller excludes all vector calls and holds the shared library lock.
// Refuse before releasing any dependency if board ownership, DMA buffers or
// interrupt ownership remain. Open counts/alias removal belong to the Resident.
// Preserve the semaphore so a successful caller can release its lock normally.
BOOL releasePciState(struct PciState* state);
