/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "config.h"

struct PciFunction {
    ULONG device;
    UWORD vendor_id, device_id;
    ULONG class_code;
    UBYTE revision, header_type;
};

enum PciScanResult { PCI_SCAN_RETRY = -2,
    PCI_SCAN_ERROR,
    PCI_SCAN_END,
    PCI_SCAN_FUNCTION };

// Read configured functions in ascending bus/device/function order. Start cursor
// at first_bus << 8. On success, cursor advances past the returned function;
// END sets it to (last_bus + 1) << 8 and preserves output. ERROR and RETRY
// preserve cursor and output. RETRY indicates PCIe Configuration Request Retry
// Status; the caller owns the bounded wait/retry policy. Function zero determines
// whether functions 1..7 exist.
// Scan the complete firmware-configured bus range, including devices behind
// configured bridges, without assigning bus numbers or modifying configuration.
// The library serializes discovery with configuration changes. This internal
// iterator neither allocates a catalog nor claims devices or their resources.
enum PciScanResult nextPciFunction(const struct PciConfig* config, ULONG* cursor,
    struct PciFunction* function);
