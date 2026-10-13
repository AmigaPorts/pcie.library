/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "catalog.h"

// Implementation of the existing FLR operation. Task context only: the caller
// retains the catalog/configuration mapping and serializes access for the whole
// call, including its timer.device wait. The driver must quiesce its device and
// exclude callbacks before reset, and reinitialize it afterwards. This does not
// reset an upstream bridge, restore configuration or alter public shadows.
// Allocate/open the timer before writing hardware. A failed reset write may
// already have reached the device, so wait even when that write reports failure.
LONG resetDeviceFunction(struct ExecBase* exec_base, struct PciCatalog* catalog, struct pci_dev* board);
