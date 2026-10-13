/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <exec/types.h>

struct PciConfig {
    volatile UBYTE* address;
    ULONG size;
    UBYTE first_bus, last_bus;
};

// The caller finds a pci-host-ecam-generic node through devicetree.resource.
// reg/ranges must describe an existing guest-visible mapping; never substitute
// an emulator pointer or a guessed physical address. Failure leaves output alone.
BOOL readConfigWindow(APTR tree_base, APTR key, struct PciConfig* config);

// Device = bus << 8 | slot << 3 | function. Values are native 68k integers;
// hardware transactions retain their exact byte/word/longword width. Invalid
// requests do not touch hardware or alter read output. Absent functions return
// the hardware response. The library owns serialization of compound operations.
BOOL readConfig(const struct PciConfig* config, ULONG device, ULONG offset, unsigned width, ULONG* value);
BOOL writeConfig(const struct PciConfig* config, ULONG device, ULONG offset, unsigned width, ULONG value);
