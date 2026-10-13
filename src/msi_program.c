/* SPDX-License-Identifier: MPL-2.0 */
#include "msi.h"
#include <libraries/interrupt.h>

static BOOL valid(const struct PciMsi* capability)
{
    if (!capability || capability->offset < 0x40 || (capability->offset & 3)) {
        return FALSE;
    }

    unsigned supported = (capability->control >> 1) & 7;
    unsigned relative = capability->control & 0x80 ? 12 : 8;
    unsigned length = relative + (capability->control & 0x100 ? 12 : 2);

    return supported <= 5 && capability->vectors == (1UL << supported) &&
        capability->offset <= 256 - length && capability->data_offset == capability->offset + relative;
}

static BOOL writeChecked(const struct PciConfig* config, ULONG device, ULONG offset, unsigned width, ULONG value)
{
    ULONG actual;

    return writeConfig(config, device, offset, width, value) &&
        readConfig(config, device, offset, width, &actual) && actual == value;
}

BOOL programMsi(const struct PciConfig* config, ULONG device, const struct PciMsi* capability,
    ULONG count, const struct InterruptMessage* message)
{
    if (!valid(capability) || !message || !count || count > capability->vectors ||
        (count & (count - 1)) || (message->address_low & 3) || message->data > 0xffff ||
        (message->data & (count - 1)) || (!(capability->control & 0x80) && message->address_high)) {
        return FALSE;
    }

    ULONG control;

    if (!readConfig(config, device, capability->offset + 2, 2, &control) ||
        (control & 1) || ((control ^ capability->control) & 0x18e)) {
        return FALSE;
    }

    unsigned enabled = 0;

    for (ULONG vectors = count; vectors > 1; vectors >>= 1) {
        ++enabled;
    }

    // MSI stays disabled across every partial write and failed readback. The
    // caller retains the block until hardware teardown has completed.
    return writeChecked(config, device, capability->offset + 4, 4, message->address_low) &&
        (!(control & 0x80) || writeChecked(config, device, capability->offset + 8, 4, message->address_high)) &&
        writeChecked(config, device, capability->data_offset, 2, message->data) &&
        writeChecked(config, device, capability->offset + 2, 2, (control & ~0x70UL) | (enabled << 4));
}

BOOL enableMsi(const struct PciConfig* config, ULONG device, const struct PciMsi* capability, BOOL enabled)
{
    if (!valid(capability)) {
        return FALSE;
    }

    ULONG control;

    if (!readConfig(config, device, capability->offset + 2, 2, &control) ||
        ((control ^ capability->control) & 0x18e) ||
        (enabled && ((control >> 4) & 7) > ((control >> 1) & 7))) {
        return FALSE;
    }

    ULONG requested = enabled ? control | 1 : control & ~1UL;

    return requested == control || writeChecked(config, device, capability->offset + 2, 2, requested);
}
