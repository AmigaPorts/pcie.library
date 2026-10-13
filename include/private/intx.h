/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "catalog.h"

// Internal INTx backend, selected by the allocation policy. The caller retains
// an open provider reference and serializes the catalog. Zero minimum means one;
// INTx cannot supply multiple vectors. No controller or device register access.
// The public MSI-X/MSI/INTx selection policy is separate and still unfinished.
LONG allocateIntx(struct Library* interrupt_base, struct PciCatalog* catalog,
    struct pci_dev* board, ULONG minimum);

// Release the allocation only after the server has been removed and all users
// have stopped. BUSY preserves a live binding. No hardware access or allocation.
LONG releaseIntx(struct PciCatalog* catalog, struct pci_dev* board);

// Setup/teardown control for the allocated device. Enabling requires a bound
// server through the allocation's retained provider; disabling does not. Unlike
// CheckSetINTxMask, this explicitly sets assertion eligibility without a pending
// guard. The caller excludes configuration/interrupt writers and retains the
// binding until disabled/quiescent and removed. Failure may follow a hardware
// effect; it never drops the binding or changes catalog data. Verify readback.
BOOL enableIntx(struct PciCatalog* catalog, struct pci_dev* board, BOOL enabled);

// ISR path for allocated INTx vector zero. No semaphore or allocation; caller
// retains the catalog/device/provider and excludes competing config writers.
// Apply the documented pending guard implemented by CheckSetINTxMask.
BOOL maskIntx(struct PciCatalog* catalog, struct pci_dev* board, ULONG vector, BOOL mask);
