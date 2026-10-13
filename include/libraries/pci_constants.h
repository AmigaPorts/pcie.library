/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

// Independent declarations of the existing interrupt-allocation ABI.
#define PCI_IRQ_INTX 1UL
#define PCI_IRQ_MSI 2UL
#define PCI_IRQ_MSIX 4UL
#define PCI_IRQ_ALL_TYPES (PCI_IRQ_INTX | PCI_IRQ_MSI | PCI_IRQ_MSIX)
