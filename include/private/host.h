/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "catalog.h"
#include "dma.h"
#include "window.h"

struct PciHost {
    struct PciHost* next;
    APTR key;
    struct PciConfig config;
    struct PciDmaMemory dma;
    struct PciMemoryWindow window;
};

struct PciHosts {
    struct PciHost* first;
    struct PciCatalog catalog;
    struct PciDmaMemory shared_dma;
};

// Discover enabled ECAM hosts through the original device-tree resource. The
// public bus/devfn ABI addresses segment zero; explicitly described other
// segments are left alone. Validate all windows before reading configuration.
// Start with an empty result and retain the resource/root for its lifetime.
// Failure, including no usable host, leaves result unchanged and frees all
// intermediate allocations. No configuration writes, BAR probing or publication.
BOOL discoverPciHosts(struct ExecBase* exec_base, APTR tree_base, APTR root, struct PciHosts* result);

// Initialize all hosts' inbound maps together, using the described CPU cache
// alignment and RAM. Start with empty DMA states. Failure rolls back all maps;
// no device or buffer is exposed by this operation. Hold the library semaphore.
BOOL prepareHostDma(struct ExecBase* exec_base, APTR tree_base, APTR cpus, APTR ram, struct PciHosts* hosts);

// Build the common allocation ranges from all retained host maps. Every host
// must use the same CPU cache alignment discovered by prepareHostDma. Keep the
// host maps for address translation; only the intersection owns allocations.
// Empty intersection succeeds with an empty pool. Failure preserves the pool.
BOOL prepareSharedDma(struct ExecBase* exec_base, struct PciHosts* hosts);

// Resolve a public board through catalog membership before dereferencing it.
// The result borrows the host-owned map; retain hosts and the library semaphore.
struct PciDmaMemory* hostDmaMemory(struct PciHosts* hosts, const struct pci_dev* board);

// Both allocation APIs use one zero-filled shared pool. The compatibility ABI
// ignores board, flags and release size, including NULL/foreign board arguments.
// Classic callers pass NULL. Retain hosts and hold the library semaphore.
APTR allocateBoardDma(struct ExecBase* exec_base, struct PciHosts* hosts,
    const struct pci_dev* board, ULONG size, ULONG flags);
BOOL releaseBoardDma(struct ExecBase* exec_base, struct PciHosts* hosts,
    const struct pci_dev* board, APTR address, ULONG size);

// Classic address lookup: this board's BARs first, then its host's DMA ranges.
// Unknown/foreign addresses return NULL; never infer an identity mapping.
// APTR is the existing 32-bit public interface. Only the final bus result is
// narrowed; retained BAR and DMA records continue to carry full PCI addresses.
APTR boardLogicalToPhysical(struct PciHosts* hosts, const struct pci_dev* board, APTR address);
APTR boardPhysicalToLogical(struct PciHosts* hosts, const struct pci_dev* board, APTR address);

// Refuse without freeing hosts, records or DMA maps while interrupt ownership
// or DMA buffers remain. Hold the same semaphore used for allocation/dispatch.
BOOL freePciHosts(struct ExecBase* exec_base, struct PciHosts* hosts);
