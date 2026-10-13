/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <exec/types.h>
#include <stdint.h>

// Enumerate fixed ACPI bus-to-CPU windows from the public firmware snapshot.
// The sink receives inclusive CPU endpoints and the corresponding PCI start.
// No MMIO, AML execution, allocation, tree edits or IOMMU programming. Failure
// may follow sink calls: callers must keep their output private until success.
BOOL collectAcpiDma(APTR tree_base, APTR bridge,
    BOOL (*sink)(APTR, uint64_t, uint64_t, uint64_t), APTR context);
