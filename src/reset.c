/* SPDX-License-Identifier: MPL-2.0 */
// Independent PCIe FLR implementation; no platform-private timing service.
#include "reset.h"
#include "capability.h"

#include <devices/timer.h>
#include <libraries/bcmpcie_errors.h>
#define __NOLIBBASE__
#include <proto/exec.h>

LONG resetDeviceFunction(struct ExecBase* SysBase, struct PciCatalog* catalog, struct pci_dev* board)
{
    struct PciDevice* device = catalogDevice(catalog, board);

    if (!SysBase || !device) {
        return PCIE_ERR_INVAL;
    }

    ULONG offset, capabilities, control;
    ULONG slot = device->info.function.device;

    if (!readCapability(device->config, slot, 0x10, &offset)) {
        return PCIE_ERR_IO;
    }

    if (!offset) {
        return PCIE_ERR_NOTSUPP;
    }

    // Device Capabilities and Device Control must fit in the conventional
    // capability area. Do not follow a truncated capability into extended space.
    if (offset > 0xf4 || !readConfig(device->config, slot, offset + 4, 4, &capabilities)) {
        return PCIE_ERR_IO;
    }

    if (!(capabilities & (1UL << 28))) {
        return PCIE_ERR_NOTSUPP;
    }

    if (!readConfig(device->config, slot, offset + 8, 2, &control)) {
        return PCIE_ERR_IO;
    }

    struct MsgPort* port = CreateMsgPort();

    if (!port) {
        return PCIE_ERR_NOMEM;
    }

    struct timerequest* request = (void*)CreateIORequest(port, sizeof(*request));

    if (!request) {
        DeleteMsgPort(port);

        return PCIE_ERR_NOMEM;
    }

    LONG result = PCIE_ERR_IO;

    if (OpenDevice(TIMERNAME, UNIT_MICROHZ, &request->tr_node, 0) == 0) {
        // Word access preserves the adjacent write-one-to-clear Device Status.
        BOOL written = writeConfig(device->config, slot, offset + 8, 2, control | 0x8000);

        request->tr_node.io_Command = TR_ADDREQUEST;
        request->tr_time.tv_secs = 0;
        request->tr_time.tv_micro = 100000;

        if (DoIO(&request->tr_node) == 0 && written) {
            result = PCIE_OK;
        }

        CloseDevice(&request->tr_node);
    }

    DeleteIORequest(&request->tr_node);
    DeleteMsgPort(port);

    return result;
}
