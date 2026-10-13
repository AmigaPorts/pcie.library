/* SPDX-License-Identifier: MPL-2.0 */
// Independently declared public ABI for compatibility with rondoval's
// bcmpcie.library/openpci v3 interface. No GPL implementation is included.
// Source revision and interface provenance are recorded in README.md.
#pragma once
#include <exec/types.h>

struct Node;

#define BCM2711PCIeBus 0x80
#define MEM_PCI 0x1
#define MEM_NONCACHEABLE 0x2
#define MEM_24BITDMA 0x4
#define MIN_OPENPCI_VERSION 3

// Preserve the 68k interface's two-byte packing and restore the caller's setting.
#pragma pack(push, 2)
struct pci_dev {
    void* bus;
    struct pci_dev* next;
    struct pci_dev* pred;
    UBYTE devfn;
    UBYTE kludgefill;
    UWORD vendor;
    UWORD device;
    ULONG devclass;
    ULONG hdr_type;
    UWORD master;
    ULONG irq;
    ULONG base_address[6];
    ULONG base_size[6];
    ULONG rom_address;
    ULONG rom_size;
    void* reserved;
    UBYTE* legacy_io;
    struct Node* owner;
};
#pragma pack(pop)
