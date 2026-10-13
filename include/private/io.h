/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <exec/types.h>

struct ExecBase;

// Classic accessors consume guest-visible addresses already obtained through
// PCI resource mapping. Each call performs exactly one volatile transaction of
// the named width; word/long values are little endian on the device. NULL reads
// return all ones and NULL writes do nothing. The caller retains the mapping and
// obeys the device's alignment/access requirements. No discovery or controller I/O.
UBYTE readPciByte(CONST_APTR address);
UWORD readPciWord(CONST_APTR address);
ULONG readPciLong(CONST_APTR address);
void writePciByte(APTR address, UBYTE value);
void writePciWord(APTR address, UWORD value);
void writePciLong(APTR address, ULONG value);

// The three classic copy vectors share Exec CopyMem semantics: raw bytes, no
// endian conversion, DMA operation or cache-management promise. Source and
// destination must be eight-byte aligned, as required by the reference API.
// The caller retains both ranges and supplies the real Exec base.
void copyPciMemory(struct ExecBase* exec_base, CONST_APTR source, APTR destination, ULONG size);
