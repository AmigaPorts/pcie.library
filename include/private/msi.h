/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "capability.h"
#include "mapping.h"

struct PciMsi {
    ULONG offset, vectors;
    UWORD control, data;
    uint64_t address;
    ULONG data_offset, mask_offset, pending_offset;
    ULONG mask, pending;
};

struct PciMsix {
    ULONG offset, vectors;
    UWORD control;
    UBYTE table_bar, pending_bar;
    ULONG table_offset, pending_offset;
};

struct PciMsixTables {
    ULONG table, table_size;
    ULONG pending, pending_size;
};

// Read-only capability snapshots. A well-formed list without this capability
// succeeds with a zeroed result; a failed read or malformed payload leaves the
// result unchanged. MSI layouts depend on 64-bit-address and per-vector-mask
// support. Conventional payloads must fit below configuration offset 0x100.
// Pending bits can change during the read; the result is not an atomic snapshot.
// Caller serializes reset/configuration changes; this neither allocates vectors
// nor selects a controller, programs a message or enables device interrupts.
BOOL readMsiCapability(const struct PciConfig* config, ULONG device, struct PciMsi* result);
BOOL readMsixCapability(const struct PciConfig* config, ULONG device, struct PciMsix* result);

// Resolve the complete MSI-X table and pending-bit array using already prepared
// resources/mappings from this function. Reject I/O/absent/upper-half BARs,
// out-of-bounds or overlapping arrays, and 68k address overflow. No BAR probing,
// mapping service, allocation or table access. Failure leaves output unchanged.
// Retain the prepared resources and exclude reconfiguration for their use.
BOOL resolveMsixTables(const struct PciResources* resources, const struct PciMappings* mappings,
    const struct PciMsix* capability, struct PciMsixTables* result);

// Device-only ISR masking. The caller owns this function's active allocation,
// retains a validated capability/table mapping, bounds vector against its own
// allocated count, and excludes competing writers/reset/BAR changes/teardown.
// MSI returns FALSE without hardware access when per-vector masking is absent.
// MSI-X accesses only the selected entry's 32-bit vector-control register.
// Preserve other vectors/control bits; never touch message address/data, pending
// bits, enable controls, INTx state or the interrupt controller. No allocation or
// semaphore. Readback verifies the requested bit and completes a posted write.
// Failure can follow a hardware effect; retain the allocation and binding.
BOOL maskMsiVector(const struct PciConfig* config, ULONG device, const struct PciMsi* capability,
    ULONG vector, BOOL mask);
BOOL maskMsixVector(const struct PciMsixTables* tables, ULONG vector, BOOL mask);

struct InterruptMessage;

// Device programming from the public interrupt provider's message descriptors.
// Caller owns this function and the allocation, retains validated capability/
// table mappings, and excludes reset, competing writers and resource teardown.
// Neither operation allocates vectors nor derives controller addresses/data.
// MSI: count and first descriptor identify one exact AllocMessageInterrupts
// block (1..32 power of two). Require disabled MSI, program the address/data and
// MME with readback, leave MSI disabled and preserve optional masks/pending bits.
// MSI-X: require this entry masked, program its full-width address/data with
// readback, preserving vector control, neighboring entries and pending bits.
// On failure, partial hardware effects may remain; keep allocation/mappings for
// retry or verified teardown. These calls do not install or remove callbacks.
BOOL programMsi(const struct PciConfig* config, ULONG device, const struct PciMsi* capability,
    ULONG count, const struct InterruptMessage* message);
BOOL programMsixVector(const struct PciMsixTables* tables, ULONG vector, const struct InterruptMessage* message);

// Change only live MSI enable or MSI-X enable/function-mask bits, with readback.
// Caller has programmed all eligible vectors, bound servers and excluded the
// other interrupt mode before enabling. Quiesce/drain the device before disabling
// and releasing its allocation; disabling alone does not drain in-flight writes.
// Do not change PCI command/BME/INTx, per-vector masks, messages or provider state.
// Failure can follow a hardware effect, including enabling; retain all ownership.
BOOL enableMsi(const struct PciConfig* config, ULONG device, const struct PciMsi* capability, BOOL enabled);
BOOL enableMsix(const struct PciConfig* config, ULONG device, const struct PciMsix* capability,
    BOOL enabled, BOOL masked);
