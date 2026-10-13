/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "devicetree_interrupt.h"

// Resolve this PCI nexus's BDF (bus << 8 | slot << 3 | function) and INTx pin
// (1..4) through its device-tree interrupt-map. The result belongs to the actual
// interrupt-controller domain; it is not a guessed GSI or a PCI line-register
// value. Undescribed wiring fails without touching hardware or output.
BOOL resolvePciInterrupt(APTR tree_base, APTR root, APTR bridge, ULONG device,
    ULONG pin, struct DtInterrupt* result);

enum DtInterruptStatus lookupPciInterrupt(APTR tree_base, APTR root, APTR bridge, ULONG device,
    ULONG pin, struct DtInterrupt* result);

struct PciCatalog;
struct PciDevice;

// Resolve an enumerated function, preferring explicit firmware routing. For a
// valid map without a matching row, walk configured parent bridges and swizzle
// INTx using the downstream slot at each hop. The catalog/configuration belongs
// to pcie.library; callers serialize it. Invalid maps or ambiguous topology fail
// without fallback, output changes, config writes or controller programming.
BOOL resolveDeviceInterrupt(APTR tree_base, APTR root, APTR bridge, const struct PciCatalog* catalog,
    const struct PciDevice* device, struct DtInterrupt* result);

struct Library;
struct pci_dev;

// Prepare a catalog record through the public interrupt.library query (1.12+).
// The caller holds an open library reference, retains the resource/root/host,
// and serializes the catalog. Commit IRQ/trigger/polarity only after resolution;
// failure leaves the record unchanged. Pin zero publishes no INTx source.
// An allocation or bound server prevents re-preparation until released.
// This does not acquire/register an IRQ, set polarity, or touch controller state.
BOOL prepareDeviceInterrupt(APTR tree_base, APTR root, APTR bridge, struct Library* interrupt_base,
    struct PciCatalog* catalog, struct pci_dev* board);

struct Interrupt;

// Bind the original client server through public interrupt.library (1.9+), at
// the reference INTx priority zero. The provider invokes the original D0/A1/A6
// callback directly. Use prepared private routing, not writable pci_dev fields.
// Caller holds an open provider reference and serializes catalog/configuration
// plus an Exec Disable/Enable pair across polarity selection and registration.
// Keep the source quiescent during setup and retain provider, record and server
// until successful removal (and return of any callback already in progress).
// These helpers do not enable/mask the PCI function. The interrupt provider
// owns controller configuration and native vector allocation.
// Return existing GIC status codes; failures preserve the binding record. A
// failed registration can leave the otherwise unowned source polarity selected.
LONG bindDeviceInterrupt(struct Library* interrupt_base, struct PciCatalog* catalog,
    struct pci_dev* board, struct Interrupt* server);

// Uses the originally retained provider. Caller quiesces the device and excludes
// dispatch/configuration/teardown; failure retains the binding for retry. A wrong
// server never detaches another client. Repeated removal returns NOT_FOUND.
LONG unbindDeviceInterrupt(struct PciCatalog* catalog, struct pci_dev* board, struct Interrupt* server);
