/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "catalog.h"
#include <exec/interrupts.h>

struct PciInterruptServer {
    struct Interrupt server;
    struct Interrupt* owner;
    struct Interrupt* volatile client;
    BOOL bound;
};

struct PciInterruptSet {
    ULONG count;
    BOOL ready;
    struct PciInterruptServer servers[];
};

// Library-owned callback entry. The provider never retains the caller's server.
// Initialize before registration; client is published/cleared with dispatch
// excluded. Forward live client code/data on every call, preserving D0/A1/A6.
void initPciInterruptServer(struct PciInterruptServer* server);

// Coordinator for the existing public interrupt API. The Resident supplies the
// catalog lock and Exec Disable/Enable around task-level operations. Sources
// must be quiescent during setup; quiesce/drain before removal/free. Keep the
// provider open until all retained ownership is gone, including failed cleanup.
// Allocation selects/programs but leaves delivery disabled until a server is
// added. Negative allocation may retain cleanup state; free retries it.
LONG allocateInterruptVectors(struct ExecBase* exec_base, struct Library* interrupt_base,
    struct PciCatalog* catalog, struct pci_dev* board, ULONG minimum, ULONG maximum, ULONG flags);
LONG freeInterruptVectors(struct ExecBase* exec_base, struct PciCatalog* catalog, struct pci_dev* board);

// Add binds and activates as one operation. Failure clears the new client
// reference before returning, even if provider removal also fails. Retained
// library-owned servers cannot call discarded client code/data. Remove clears
// the client first; its internal status permits diagnostics/retry by void ABI
// wrappers. A wrong server never affects the current client.
LONG addInterruptVectorServer(struct ExecBase* exec_base, struct PciCatalog* catalog,
    struct pci_dev* board, ULONG vector, struct Interrupt* server);
LONG remInterruptVectorServer(struct PciCatalog* catalog, struct pci_dev* board,
    ULONG vector, struct Interrupt* server);

// ISR-safe queries/masking: no allocation, lock or provider reconfiguration.
// The caller retains/excludes competing users of the device and binding.
BOOL maskInterruptVector(struct PciCatalog* catalog, struct pci_dev* board, ULONG vector, BOOL mask);
ULONG getInterruptVectorType(struct PciCatalog* catalog, struct pci_dev* board);
