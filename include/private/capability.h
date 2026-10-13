/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "config.h"

// Internal implementations for the existing FindCapability/FindExtCapability
// ABI. Return the first matching configuration offset, or zero for no match,
// an inaccessible function or a malformed chain encountered before the match.
// Read only; the caller serializes lookup with reset/configuration changes.
// Finding a header does not validate the capability-specific payload length.
ULONG findCapability(const struct PciConfig* config, ULONG device, UBYTE capability);
ULONG findExtCapability(const struct PciConfig* config, ULONG device, UWORD capability);

// Internal status-preserving conventional lookup for metadata readers. Success
// includes a well-formed list with no match (offset zero). Failure preserves
// output, allowing a catalog scan to abort rather than hide a failed read.
BOOL readCapability(const struct PciConfig* config, ULONG device, UBYTE capability, ULONG* offset);
