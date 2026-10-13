/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <exec/types.h>
#include <stdint.h>

// PCI binding address-space codes, independent of BAR attribute bits.
enum PciAddressSpace { PCI_IO = 1,
    PCI_MEMORY32 = 2,
    PCI_MEMORY64 = 3 };

// Translate an assigned PCI bus interval through a host bridge's ranges and
// ordinary ancestor buses. Only existing device-tree mappings are resolved;
// this does not allocate a BAR or create a mapping. Failure leaves output alone.
BOOL translatePciAddress(APTR tree_base, APTR bridge, enum PciAddressSpace space,
    uint64_t base, uint64_t size, ULONG* address);
