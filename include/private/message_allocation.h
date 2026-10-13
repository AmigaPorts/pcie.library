/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <libraries/interrupt.h>

struct ExecBase;
struct Library;

struct PciMessageAllocation {
    struct Library* provider;
    struct InterruptMessage* messages;
    ULONG type, count, capacity;
};

// Private controller reservations for one PCI function. Start with zero state.
// The caller retains an open provider reference, serializes allocation/teardown,
// and supplies the validated capability's vector limit. No device programming,
// controller address calculation, callback registration or public API change.
// Minimum zero means one; maximum is clamped to capability capacity. MSI chooses
// a power-of-two block, trying smaller counts only on provider exhaustion. MSI-X
// acquires independent messages and accepts a shorter count only on exhaustion.
// Success returns the count. Failure returns PCIE_ERR_* and releases reservations
// acquired by this call; if cleanup itself fails, state retains the remaining
// messages and provider for releasePciMessages to retry. Never discard that state
// or fall back to another interrupt mode while state.messages remains non-NULL.
LONG reservePciMessages(struct ExecBase* exec_base, struct Library* interrupt_base,
    ULONG type, ULONG minimum, ULONG maximum, ULONG limit, struct PciMessageAllocation* allocation);

// Before releasing a published allocation, the caller quiesces the device,
// disables its message mode, drains delivery and removes all handlers. The
// provider additionally checks each block for installed handlers/queued/native
// delivery. Retryable failure retains every unreleased message; MSI-X releases
// from the end so count always describes the still-owned prefix. The caller must
// exclude all users even when a release is partial. No device register access.
LONG releasePciMessages(struct ExecBase* exec_base, struct PciMessageAllocation* allocation);

// Internal conversion of nonzero public interrupt-provider errors to PCI errors.
LONG pciInterruptError(LONG status);
