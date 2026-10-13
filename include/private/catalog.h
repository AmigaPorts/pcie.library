/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "enumeration.h"
#include "mapping.h"
#include <libraries/openpci.h>

struct ExecBase;
struct Library;
struct Interrupt;
struct PciInterruptAllocation;
struct PciInterruptSet;

struct PciDeviceInfo {
    struct PciFunction function;
    UWORD command;
    UBYTE interrupt_line, interrupt_pin;
    UWORD subsystem_vendor, subsystem_device;
    BOOL has_subsystem;
};

struct PciDevice {
    struct pci_dev board;
    struct PciDevice* next;
    struct PciDevice* previous;
    const struct PciConfig* config;
    struct PciDeviceInfo info;
    struct PciResources resources;
    struct PciMappings mappings;
    BOOL resources_prepared;
    BOOL prefer_msi;
    BOOL interrupt_prepared, interrupt_edge, interrupt_active_low;
    ULONG interrupt_irq, interrupt_type;
    struct Library* interrupt_provider;
    struct Interrupt* interrupt_server;
    struct Library* interrupt_library;
    struct PciInterruptAllocation* interrupt_allocation;
    struct PciInterruptSet* interrupt_set;
};

struct PciCatalog {
    struct PciDevice* first;
    struct PciDevice* last;
    ULONG count;
};

enum PciMatchField { PCI_MATCH_VENDOR = 1,
    PCI_MATCH_DEVICE = 2,
    PCI_MATCH_CLASS = 4,
    PCI_MATCH_SLOT = 8 };
struct PciMatch {
    ULONG fields;
    UWORD vendor, device;
    ULONG class_code, slot;
};

// Internal discovery catalog for one PCI segment. The embedded ABI record has
// identity/list fields only after discovery. It must remain private until resource
// preparation and the remaining public fields are complete.
// The caller serializes access and keeps config alive until the catalog is freed.
// Remove every bound interrupt server before freeing records or provider bases.
// Start with a zeroed catalog. Appending a non-overlapping bus window preserves
// scan order; any read/retry/allocation failure leaves existing entries intact.
BOOL appendCatalog(struct ExecBase* exec_base, const struct PciConfig* config, struct PciCatalog* catalog);

// Refuse atomically while any record retains an interrupt allocation/provider
// or server. The caller must complete teardown before releasing host windows.
BOOL freeCatalog(struct ExecBase* exec_base, struct PciCatalog* catalog);

// Read layout-dependent metadata for a discovered function without writes.
// Interrupt line is only the configuration byte, never a resolved controller IRQ.
// Subsystem IDs use the endpoint/CardBus fields or a bridge subsystem capability.
BOOL readDeviceInfo(const struct PciConfig* config, const struct PciFunction* function, struct PciDeviceInfo* info);

// Unselected fields are wildcards; selected fields match exactly. A NULL previous
// starts from the head. A foreign previous pointer returns NULL without dereference.
struct PciDevice* findCatalogDevice(const struct PciCatalog* catalog, const struct PciDevice* previous,
    const struct PciMatch* match);

// Recover a library record without trusting or dereferencing a caller's pointer.
struct PciDevice* catalogDevice(const struct PciCatalog* catalog, const struct pci_dev* board);

// PRM_BoardOwner semantics: a non-NULL claim requires an empty owner field, even
// for the same owner; NULL clears unconditionally. Advisory only: lookup and
// configuration access do not acquire ownership. The caller holds the library
// semaphore for validation plus this update; this helper does not add a lock.
BOOL setCatalogOwner(struct PciCatalog* catalog, struct pci_dev* board, struct Node* owner);

// Prepare a private record from assigned resources and existing DT mappings.
// The caller serializes preparation and excludes all users of the resources.
// Failure preserves the entire record. Success updates BAR fields and retains
// full resource/mapping metadata. It does not publish the device, enable decode,
// route interrupts or replace owner/list/command fields. ROM metadata remains
// private; the reference constructor does not populate public ROM fields.
BOOL prepareDeviceResources(APTR tree_base, APTR bridge, struct PciDevice* device,
    const struct PciResources* resources);

// Enable bus mastering using the live command word, then publish its full
// readback in master. Success means the access completed, not that the device
// accepted the bit. Ownership is advisory. The caller holds the library semaphore
// across validation, configuration access and publication. Transport failure
// leaves master unchanged; a completed write is not undone if readback fails.
BOOL setCatalogMaster(struct PciCatalog* catalog, struct pci_dev* board);

// CheckSetINTxMask semantics: nonzero mask requests masking, allowed only while
// this function reports pending; unmask is allowed only when it is not pending.
// Preserve the remaining live command bits, status and the catalog. TRUE means
// the requested mask bit was observed (including an already matching command).
// No allocation or semaphore: caller retains the record and excludes competing
// configuration writers/teardown, including interrupt-context writers. A failed
// write/readback cannot promise to undo a hardware effect. No controller access.
BOOL checkCatalogIntxMask(struct PciCatalog* catalog, struct pci_dev* board, BOOL mask);

// Prepare firmware-assigned BARs for a library-owned function through
// the host's existing device-tree ranges. The caller excludes configuration and
// resource users (including bridge descendants). Preserve bus mastering; BAR
// sizing concerns incoming address decoding and does not stop outgoing DMA.
// Temporarily disable I/O/memory decode, probe/restore BARs, translate the complete
// set, and restore/verify command before committing resource metadata. No address
// allocation, interrupt changes, MMU services or public command-shadow updates.
// Type-0 host/ISA bridges with empty BAR/ROM slots need no endpoint sizing;
// leave their system-decode command untouched and retain empty resource metadata.
// Failure preserves the catalog. A probe failure leaves decoding disabled because
// BAR restoration may be uncertain. A later mapping failure restores the original
// command; a failed configuration transport cannot guarantee hardware restoration.
BOOL prepareAssignedDevice(APTR tree_base, APTR bridge, struct PciCatalog* catalog, struct pci_dev* board);

// Classic obtain/release uses the current Exec task. A claim requires an empty
// owner, including rejection of repeated claims by that task. Release clears
// only the calling task's claim. This differs from SetBoardAttrs' unconditional
// NULL release; both APIs share the same advisory owner field. The caller holds
// the library semaphore across each operation and supplies the real Exec base.
BOOL obtainCatalogCard(struct ExecBase* exec_base, struct PciCatalog* catalog, struct pci_dev* board);
void releaseCatalogCard(struct ExecBase* exec_base, struct PciCatalog* catalog, struct pci_dev* board);
