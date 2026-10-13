/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <stdint.h>

// Private library representation. Addresses are PCI bus addresses; mappings
// into the 68k address space are managed separately by the library.
struct PciBar {
    uint64_t config, mask;
    uint64_t address, size;
};

// Decode hardware sizing responses, including unassigned and absent BARs.
// Invalid encodings leave output unchanged. No hardware access or allocation.
int decodeBar(uint32_t low, uint32_t high, uint32_t low_mask,
    uint32_t high_mask, struct PciBar* bar);
int decodeRomBar(uint32_t value, uint32_t mask, struct PciBar* bar);
