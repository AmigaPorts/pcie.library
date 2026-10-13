/* SPDX-License-Identifier: MPL-2.0 */
#include "attributes.h"

#include <libraries/pcitags.h>

static BOOL attribute(const struct PciDevice* device, const struct PciMemoryWindow* window, ULONG tag, ULONG* value)
{
    const struct pci_dev* board = &device->board;
    const struct PciDeviceInfo* info = &device->info;

    if (tag >= PRM_MemoryAddr0 && tag <= PRM_MemoryAddr5) {
        *value = board->base_address[tag - PRM_MemoryAddr0];
    } else if (tag >= PRM_MemoryFlags0 && tag <= PRM_MemoryFlags5) {
        *value = board->base_size[tag - PRM_MemoryFlags0] & 15;
    } else if (tag >= PRM_MemorySize0 && tag <= PRM_MemorySize5) {
        unsigned index = tag - PRM_MemorySize0;
        ULONG raw = board->base_size[index];

        // Public shadows, including the address's I/O bit, are observable ABI.
        ULONG mask = board->base_address[index] & 1 ? UINT32_C(0xfffffffc) : UINT32_C(0xfffffff0);
        *value = raw ? (ULONG)(0 - (raw & mask)) : 0;
    } else {
        switch (tag) {
            case PRM_Vendor:
                *value = board->vendor;
                break;
            case PRM_Device:
                *value = board->device;
                break;
            case PRM_Class:
                *value = board->devclass >> 16 & 255;
                break;
            case PRM_SubClass:
                *value = board->devclass >> 8 & 255;
                break;
            case PRM_Interface:
                *value = board->devclass & 255;
                break;
            case PRM_Revision:
                *value = info->function.revision;
                break;
            case PRM_HeaderType:
                *value = info->function.header_type;
                break;
            case PRM_SubsysVendor:
                *value = info->subsystem_vendor;
                break;
            case PRM_SubsysID:
                *value = info->subsystem_device;
                break;
            case PRM_BusNumber:
                *value = info->function.device >> 8;
                break;
            case PRM_SlotNumber:
                *value = board->devfn >> 3;
                break;
            case PRM_FunctionNumber:
                *value = board->devfn & 7;
                break;
            case PRM_InterruptLine:
                *value = device->interrupt_irq;
                break;
            case PRM_InterruptPin:
                *value = info->interrupt_pin;
                break;
            case PRM_LegacyIOSpace:
                *value = (ULONG)(uintptr_t)board->legacy_io;
                break;
            case PRM_PCIToHostOffset:
                *value = 0;
                break;
            case PRM_ROM_Address:
                *value = (ULONG)(uintptr_t)board->rom_address;
                break;
            case PRM_ROM_Size:
                *value = board->rom_size;
                break;
            case PRM_ROM_Flags: {
                ULONG raw = 0;

                readConfig(device->config, info->function.device, 0x30, 4, &raw);
                *value = raw & 1;
                break;
            }

            case PRM_ConfigArea: {
                const struct PciConfig* config = device->config;
                ULONG bus = info->function.device >> 8;

                if (!config || !config->address || info->function.device > 65535 ||
                    bus < config->first_bus || bus > config->last_bus) {
                    return FALSE;
                }

                ULONG offset = (info->function.device - ((ULONG)config->first_bus << 8)) << 12;
                uintptr_t address = (uintptr_t)config->address;

                if (offset >= config->size || config->size - offset < 4096 ||
                    address > UINT32_MAX || offset > UINT32_MAX - address ||
                    UINT32_MAX - address - offset < 4095) {
                    return FALSE;
                }

                *value = address + offset;
                break;
            }

            case PRM_BoardOwner:
                *value = (ULONG)(uintptr_t)board->owner;
                break;
            case PRM_PCIMemWindowLow:
                *value = window->address;
                break;
            case PRM_PCIMemWindowHigh:
                *value = (ULONG)window->address + (ULONG)window->size;
                break;
            default:
                return FALSE;
        }
    }

    return TRUE;
}

ULONG getCatalogAttrs(const struct PciCatalog* catalog, const struct pci_dev* board,
    const struct PciMemoryWindow* window, const struct TagItem* tags)
{
    const struct PciDevice* device = catalogDevice(catalog, board);

    if (!device || !window || !tags) {
        return 0;
    }

    ULONG count = 0;

    for (const struct TagItem* item = tags; item->ti_Tag != TAG_DONE; ++item) {
        if (item->ti_Tag == TAG_IGNORE || !item->ti_Data) {
            continue;
        }

        ULONG value;

        if (attribute(device, window, item->ti_Tag, &value)) {
            *(ULONG*)(uintptr_t)item->ti_Data = value;
            ++count;
        }
    }

    return count;
}
