/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "catalog.h"

// ObtainPCIRegion's memory-BAR operation. Resolve the entire requested PCI bus
// interval inside this device's prepared mappings. Never allocate/map/probe or
// access device registers. Region zero is invalid. Preserve the existing ABI's
// zero-size point lookup (including the end of a BAR); overlapping entries use
// the first BAR in order. I/O BARs and the expansion ROM are excluded.
// The caller retains the catalog/mappings and excludes resource replacement.
APTR obtainDeviceRegion(const struct PciCatalog* catalog, const struct pci_dev* board,
    ULONG region, ULONG size);

// Private full-width BAR translations for the classic address API's BAR-first
// lookup. Include memory and I/O BARs, exclude the ROM and one-past-end pointers.
// Failure leaves output untouched, so the eventual public wrapper can try its
// separately validated DMA ranges. No assumption that RAM is identity-mapped.
// Bus addresses stay 64-bit here; narrowing to the classic APTR result belongs
// at its ABI boundary, not in the retained BAR metadata.
BOOL deviceBarToBus(const struct PciCatalog* catalog, const struct pci_dev* board,
    ULONG address, uint64_t* result);
BOOL deviceBarToLogical(const struct PciCatalog* catalog, const struct pci_dev* board,
    uint64_t address, ULONG* result);
