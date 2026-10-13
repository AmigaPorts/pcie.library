/* SPDX-License-Identifier: MPL-2.0 */
#include "interrupt.h"
#include "catalog.h"
#include <exec/libraries.h>
#define __NOLIBBASE__
#include <proto/interrupt.h>

BOOL prepareDeviceInterrupt(APTR tree_base, APTR root, APTR bridge, struct Library* InterruptBase,
    struct PciCatalog* catalog, struct pci_dev* board)
{
    if (!tree_base || !root || !bridge || !InterruptBase || InterruptBase->lib_Version < 1 ||
        (InterruptBase->lib_Version == 1 && InterruptBase->lib_Revision < 12) ||
        InterruptBase->lib_NegSize < 180) {
        return FALSE;
    }

    struct PciDevice* device = catalogDevice(catalog, board);

    if (!device || device->interrupt_set || device->interrupt_allocation || device->interrupt_server ||
        device->interrupt_type || device->interrupt_provider || device->interrupt_library ||
        device->info.interrupt_pin > 4) {
        return FALSE;
    }

    struct InterruptRoute route = {0};

    if (device->info.interrupt_pin) {
        struct DtInterrupt description;

        if (!resolveDeviceInterrupt(tree_base, root, bridge, catalog, device, &description) ||
            ResolveInterrupt(description.domain, description.specifier, description.cells, &route)) {
            return FALSE;
        }
    }

    // A zero pin explicitly means no INTx source. Otherwise the public library
    // query supplies the namespace and binding interpretation; PCI never guesses
    // a GSI from the configuration line byte or inspects controller internals.
    device->board.irq = route.irq;
    device->interrupt_irq = route.irq;
    device->interrupt_edge = route.edge;
    device->interrupt_active_low = route.active_low;
    device->interrupt_prepared = TRUE;

    return TRUE;
}
