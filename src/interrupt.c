/* SPDX-License-Identifier: MPL-2.0 */
#include "interrupt.h"
#define __NOLIBBASE__
#include <proto/devicetree.h>

enum DtInterruptStatus lookupPciInterrupt(APTR DeviceTreeBase, APTR root, APTR bridge, ULONG device,
    ULONG pin, struct DtInterrupt* result)
{
    if (!DeviceTreeBase || !root || !bridge || !result || device > 65535 || pin < 1 || pin > 4 ||
        DT_FindProperty(bridge, (CONST_STRPTR)"interrupt-controller")) {
        return DT_INTERRUPT_INVALID;
    }

    // PCI configuration-space unit address: bus/device/function in phys.hi;
    // phys.mid and phys.lo are zero. Encode explicitly for both build hosts.
    UBYTE address[12] = {0}, specifier[4] = {0};

    address[1] = device >> 8;
    address[2] = device;
    specifier[3] = pin;

    struct DtInterrupt interrupt = {bridge, specifier, 1};

    return dtResolveInterruptStatus(DeviceTreeBase, root, &interrupt, address, 3, result);
}

BOOL resolvePciInterrupt(APTR tree_base, APTR root, APTR bridge, ULONG device,
    ULONG pin, struct DtInterrupt* result)
{
    return lookupPciInterrupt(tree_base, root, bridge, device, pin, result) == DT_INTERRUPT_FOUND;
}
