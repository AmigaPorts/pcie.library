/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "catalog.h"
#include "message_allocation.h"
#include "msi.h"

enum PciInterruptPhase {
    PCI_INTERRUPT_RESERVED,
    PCI_INTERRUPT_PROGRAMMING,
    PCI_INTERRUPT_PROGRAMMED,
    PCI_INTERRUPT_ACTIVE,
    PCI_INTERRUPT_STOPPING,
    PCI_INTERRUPT_STOPPED,
    PCI_INTERRUPT_RELEASING
};

struct PciInterruptAllocation {
    struct PciMessageAllocation messages;
    struct PciMsi msi;
    struct PciMsix msix;
    struct PciMsixTables tables;
    enum PciInterruptPhase phase;
    struct Interrupt** servers;
    ULONG server_count;
};

// Private reservation phase of the PCI interrupt coordinator. Validate the
// catalog identity, then select only permitted types in MSI-X, MSI, INTx order.
// Read capability state and require both message modes disabled. MSI-X also
// needs complete prepared table/PBA mappings. No device writes or callbacks.
// Success retains private capability/mapping/provider ownership and returns the
// count. Exhaustion/unsupported modes may fall back only after complete cleanup;
// I/O, busy, invalid state and retained cleanup failures stop selection.
// Caller holds the catalog lock and retains the open provider and DT mappings.
// A negative result may retain interrupt_allocation when rollback is refused;
// release it before retrying, changing resources or destroying the catalog.
LONG reserveDeviceInterrupts(struct ExecBase* exec_base, struct Library* interrupt_base,
    struct PciCatalog* catalog, struct pci_dev* board, ULONG minimum, ULONG maximum, ULONG flags);

// Release the reservation phase (or an allocation already stopped by the device
// coordinator). Caller has quiesced/drained hardware, disabled message delivery,
// removed handlers and excluded every user. No device writes. Retain the private
// record and provider on failure, including partial MSI-X release, for retry.
LONG releaseDeviceReservations(struct ExecBase* exec_base, struct PciCatalog* catalog, struct pci_dev* board);

// Program the selected message allocation, leaving delivery disabled. MSI-X
// masks the complete hardware table before programming the allocated prefix.
// MSI masks allocated vectors when its optional per-vector mask is supported.
// No server registration, command/BME changes or controller register access.
// Caller owns/serializes the device and excludes reset and resource changes.
// Only a fresh reservation may be programmed; after a write attempt, failure
// retains ownership until stopDeviceMessages verifies disabled delivery.
LONG programDeviceMessages(struct PciCatalog* catalog, struct pci_dev* board);

// Caller first quiesces/drains the device and excludes callbacks/users. Disable
// message delivery with readback; MSI-X also sets its function mask. Failure
// retains the stopping state and every reservation for retry. Success permits
// releaseDeviceReservations, whose provider may still refuse pending delivery.
// This does not drain device DMA or clear pending bits. A stopped/releasing
// allocation cannot be programmed again; release it before a new allocation.
LONG stopDeviceMessages(struct PciCatalog* catalog, struct pci_dev* board);

// Message-vector binding phase. Register the original server through the held
// provider, at priority zero/edge trigger. Caller holds the catalog lock, retains
// the server/provider and excludes dispatch with Exec Disable/Enable throughout
// binding plus activation. Device sources remain quiescent until setup finishes.
// Same-server registration is idempotent; a different server is never replaced.
LONG bindDeviceMessage(struct ExecBase* exec_base, struct PciCatalog* catalog,
    struct pci_dev* board, ULONG vector, struct Interrupt* server);

// Enable the selected message mode and unmask this bound vector. No allocation
// or registration. Any write/readback failure retains the binding and marks the
// allocation active: the caller must stop/remove it or retry activation before
// discarding a server. This is an internal phase, not a public AddIntVectorServer
// wrapper with implicit rollback. Other MSI-X entries remain masked. MSI without
// per-vector masking enables the block; keep unbound device sources quiescent.
LONG activateDeviceMessage(struct PciCatalog* catalog, struct pci_dev* board, ULONG vector);

// Caller quiesces/drains this source and excludes callbacks before removal.
// Mask the device vector when supported, then detach through its original
// provider. A failed mask/removal retains the server for retry. Stopped devices
// need no additional mask writes. MSI without per-vector masking relies on the
// driver's source quiescence; removal does not disable other vectors' delivery.
LONG unbindDeviceMessage(struct PciCatalog* catalog, struct pci_dev* board,
    ULONG vector, struct Interrupt* server);

// ISR-safe message masking: no semaphore, allocation or controller operation.
// Requires a bound vector in a programmed/active allocation. MSI without an
// optional per-vector mask returns FALSE. A stopped allocation cannot be unmasked.
BOOL maskDeviceMessage(struct PciCatalog* catalog, struct pci_dev* board, ULONG vector, BOOL mask);
