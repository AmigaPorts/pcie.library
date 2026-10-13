/* SPDX-License-Identifier: MPL-2.0 */
#include "io.h"

#define __NOLIBBASE__
#include <proto/exec.h>

static UWORD littleWord(UWORD value)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return __builtin_bswap16(value);
#else
    return value;
#endif
}

static ULONG littleLong(ULONG value)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return __builtin_bswap32(value);
#else
    return value;
#endif
}

UBYTE readPciByte(CONST_APTR address)
{
    return address ? *(const volatile UBYTE*)address : 0xff;
}

UWORD readPciWord(CONST_APTR address)
{
    return address ? littleWord(*(const volatile UWORD*)address) : 0xffff;
}

ULONG readPciLong(CONST_APTR address)
{
    return address ? littleLong(*(const volatile ULONG*)address) : 0xffffffffUL;
}

void writePciByte(APTR address, UBYTE value)
{
    if (address) {
        *(volatile UBYTE*)address = value;
    }
}

void writePciWord(APTR address, UWORD value)
{
    if (address) {
        *(volatile UWORD*)address = littleWord(value);
    }
}

void writePciLong(APTR address, ULONG value)
{
    if (address) {
        *(volatile ULONG*)address = littleLong(value);
    }
}

void copyPciMemory(struct ExecBase* SysBase, CONST_APTR source, APTR destination, ULONG size)
{
    CopyMem(source, destination, size);
}
