/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

// Independent declarations of the OpenPCI/Prometheus tag ABI used by rondoval's
// library. Values are interface identifiers; no reference implementation is used.
// PRM_Class is the base-class byte. The classic pci_find_class uses all 24 bits.
#define PRM_Vendor               0x6eda0000UL
#define PRM_Device               0x6eda0001UL
#define PRM_Revision             0x6eda0002UL
#define PRM_Class                0x6eda0003UL
#define PRM_SubClass             0x6eda0004UL
#define PRM_BoardOwner           0x6eda0005UL
#define PRM_SlotNumber           0x6eda0006UL
#define PRM_FunctionNumber       0x6eda0007UL
#define PRM_BusNumber            0x6eda0008UL
#define PRM_HeaderType           0x6eda0009UL
#define PRM_SubsysVendor         0x6eda000aUL
#define PRM_SubsysID             0x6eda000bUL
#define PRM_Interface            0x6eda000cUL
#define PRM_InterruptPin         0x6eda000dUL
#define PRM_LegacyIOSpace        0x6eda000eUL
#define PRM_PCIToHostOffset      0x6eda000fUL
#define PRM_MemoryAddr0          0x6eda0010UL
#define PRM_MemoryAddr1          0x6eda0011UL
#define PRM_MemoryAddr2          0x6eda0012UL
#define PRM_MemoryAddr3          0x6eda0013UL
#define PRM_MemoryAddr4          0x6eda0014UL
#define PRM_MemoryAddr5          0x6eda0015UL
#define PRM_ROM_Address          0x6eda0016UL
#define PRM_InterruptLine        0x6eda001dUL
#define PRM_ConfigArea           0x6eda001eUL
#define PRM_MemorySize0          0x6eda0020UL
#define PRM_MemorySize1          0x6eda0021UL
#define PRM_MemorySize2          0x6eda0022UL
#define PRM_MemorySize3          0x6eda0023UL
#define PRM_MemorySize4          0x6eda0024UL
#define PRM_MemorySize5          0x6eda0025UL
#define PRM_ROM_Size             0x6eda0026UL
#define PRM_MemoryFlags0         0x6eda0030UL
#define PRM_MemoryFlags1         0x6eda0031UL
#define PRM_MemoryFlags2         0x6eda0032UL
#define PRM_MemoryFlags3         0x6eda0033UL
#define PRM_MemoryFlags4         0x6eda0034UL
#define PRM_MemoryFlags5         0x6eda0035UL
#define PRM_ROM_Flags            0x6eda0036UL
#define PRM_PCIMemWindowLow      0x6eda0050UL
#define PRM_PCIMemWindowHigh     0x6eda0051UL
#define PRM_MemRangeLower        0x6eda1000UL
#define PRM_MemRangeUpper        0x6eda1001UL
