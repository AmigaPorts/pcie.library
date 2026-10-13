/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "catalog.h"
#include "window.h"
#include <utility/tagitem.h>

// Internal implementations of FindBoardA and SetBoardAttrsA. The caller holds
// the library semaphore and only exposes a fully prepared catalog. These do not
// add a library base, acquire ownership implicitly or access native services.
// Reference tag lists are flat: TAG_DONE terminates, TAG_IGNORE is skipped.
// TAG_MORE and TAG_SKIP are unrecognized, not traversal controls in this ABI.
struct pci_dev* findCatalogBoard(const struct PciCatalog* catalog, const struct pci_dev* previous,
    const struct TagItem* tags);

// Only PRM_BoardOwner is writable. Process the entire list in order; failure of
// one item does not roll back successful claims/releases or skip later items.
// Empty/ignored-only lists succeed; invalid records or NULL lists fail.
BOOL setCatalogAttrs(struct PciCatalog* catalog, struct pci_dev* board, const struct TagItem* tags);

// GetBoardAttrsA: flat, ordered list of ULONG output pointers. Count every
// recognized non-NULL output, including duplicates; ignore unknown tags. Use
// public shadows where the reference does, retained routing for IRQs, and the
// function-specific ECAM mapping for ConfigArea. ROM flags are read live from
// offset 0x30, including bridge headers; failed reads report zero and still count.
// The caller supplies this device's validated host window (zero if absent),
// holds the semaphore and retains configuration/routing metadata. The fixed
// PCIToHostOffset compatibility value is not a DMA translation contract.
ULONG getCatalogAttrs(const struct PciCatalog* catalog, const struct pci_dev* board,
    const struct PciMemoryWindow* window, const struct TagItem* tags);
