/* SPDX-License-Identifier: MPL-2.0 */
// Independent implementation of the public PCI ABI documented in pcie.sfd.
#include "state.h"
#include "attributes.h"
#include "capability.h"
#include "interrupt_vectors.h"
#include "io.h"
#include "region.h"
#include "reset.h"

#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/resident.h>
#include <libraries/bcmpcie_errors.h>
#include <libraries/pci_constants.h>
#include <utility/hooks.h>
#define __NOLIBBASE__
#include <proto/exec.h>

struct PciLibraryState;
struct PciLibraryBase {
    struct Library library;
    struct PciLibraryState* shared;
};

struct PciLibraryState {
    struct PciState pci;
    struct PciLibraryBase* bases[2];
    ULONG seglist;
};

LONG __attribute__((used, section(".text.entry"))) doNotExecute(void)
{
    return -1;
}

extern const UBYTE rom_end;
static const char library_name[] = LIBRARY_NAME;
static const char compatibility_name[] = "bcmpcie.library";
static const char library_id[] = VERSION_STRING;
static const char compatibility_id[] = "$VER: bcmpcie.library 2.0 (06.10.2026) pcie.library alias";
static struct Library* residentInit(ULONG seglist asm("a0"), struct ExecBase* SysBase asm("a6"));
const struct Resident pcieResident __attribute__((used)) = {
    RTC_MATCHWORD, (struct Resident*)&pcieResident, (APTR)&rom_end,
    RTF_COLDSTART, LIBRARY_VERSION, NT_LIBRARY, 101,
    (APTR)library_name, (APTR)library_id, (APTR)residentInit
};

static struct PciState* lockLibrary(struct PciLibraryBase* base)
{
    struct PciState* state = &base->shared->pci;
    struct ExecBase* SysBase = state->exec_base;

    ObtainSemaphore(&state->semaphore);

    return state;
}

static void unlockLibrary(struct PciState* state)
{
    struct ExecBase* SysBase = state->exec_base;

    ReleaseSemaphore(&state->semaphore);
}

static void freeBase(struct ExecBase* SysBase, struct PciLibraryBase* base)
{
    FreeMem((UBYTE*)base - base->library.lib_NegSize, base->library.lib_NegSize + base->library.lib_PosSize);
}

static ULONG libExpunge(struct PciLibraryBase* base asm("a6"))
{
    struct PciLibraryState* shared = base->shared;
    struct ExecBase* SysBase = shared->pci.exec_base;
    ULONG seglist = 0;

    Forbid();
    shared->bases[0]->library.lib_Flags |= LIBF_DELEXP;
    shared->bases[1]->library.lib_Flags |= LIBF_DELEXP;

    if (!shared->bases[0]->library.lib_OpenCnt && !shared->bases[1]->library.lib_OpenCnt) {
        ObtainSemaphore(&shared->pci.semaphore);

        BOOL released = releasePciState(&shared->pci);

        ReleaseSemaphore(&shared->pci.semaphore);

        if (released) {
            seglist = shared->seglist;
            Remove(&shared->bases[0]->library.lib_Node);
            Remove(&shared->bases[1]->library.lib_Node);
            freeBase(SysBase, shared->bases[0]);
            freeBase(SysBase, shared->bases[1]);
            FreeMem(shared, sizeof(*shared));
        }
    }

    Permit();

    return seglist;
}

static struct PciLibraryBase* libOpen(ULONG version asm("d0"), struct PciLibraryBase* base asm("a6"))
{
    (void)version;
    ++base->library.lib_OpenCnt;
    base->shared->bases[0]->library.lib_Flags &= (UBYTE)~LIBF_DELEXP;
    base->shared->bases[1]->library.lib_Flags &= (UBYTE)~LIBF_DELEXP;

    return base;
}

static ULONG libClose(struct PciLibraryBase* base asm("a6"))
{
    if (--base->library.lib_OpenCnt == 0 && (base->library.lib_Flags & LIBF_DELEXP)) {
        return libExpunge(base);
    }

    return 0;
}

static ULONG libNull(void)
{
    return 0;
}

// Retained OpenPCI compatibility identifier, not hardware discovery.
static UWORD pciBus(void)
{
    return BCM2711PCIeBus;
}

static UBYTE inputByte(APTR address asm("a0"))
{
    return readPciByte(address);
}

static void outputByte(UBYTE value asm("d0"), APTR address asm("a0"))
{
    writePciByte(address, value);
}

static UWORD inputWord(APTR address asm("a0"))
{
    return readPciWord(address);
}

static void outputWord(UWORD value asm("d0"), APTR address asm("a0"))
{
    writePciWord(address, value);
}

static ULONG inputLong(APTR address asm("a0"))
{
    return readPciLong(address);
}

static void outputLong(ULONG value asm("d0"), APTR address asm("a0"))
{
    writePciLong(address, value);
}

static void copyMemory(APTR source asm("a0"), APTR destination asm("a1"), ULONG size asm("d0"),
    struct PciLibraryBase* base asm("a6"))
{
    copyPciMemory(base->shared->pci.exec_base, source, destination, size);
}

// Walk retained records, comparing the public identity shadows as the ABI
// specifies. A foreign continuation is never dereferenced.
static struct pci_dev* findClassic(struct PciState* state, struct pci_dev* previous,
    const struct PciMatch* match)
{
    struct PciDevice* device = state->hosts.catalog.first;

    if (previous) {
        device = catalogDevice(&state->hosts.catalog, previous);

        if (!device) {
            return NULL;
        }

        device = device->next;
    }

    for (; device; device = device->next) {
        if ((match->fields & PCI_MATCH_VENDOR) && device->board.vendor != match->vendor) {
            continue;
        }

        if ((match->fields & PCI_MATCH_DEVICE) && device->board.device != match->device) {
            continue;
        }

        if ((match->fields & PCI_MATCH_CLASS) && device->board.devclass != match->class_code) {
            continue;
        }

        if ((match->fields & PCI_MATCH_SLOT) &&
            ((device->info.function.device >> 8) != (match->slot >> 8) ||
                device->board.devfn != (UBYTE)match->slot)) {
            continue;
        }

        return &device->board;
    }

    return NULL;
}

static struct pci_dev* findDevice(UWORD vendor asm("d0"), UWORD product asm("d1"),
    struct pci_dev* previous asm("a0"), struct PciLibraryBase* base asm("a6"))
{
    struct PciMatch match = { .fields = (vendor != 0xffff ? PCI_MATCH_VENDOR : 0) |
        (product != 0xffff ? PCI_MATCH_DEVICE : 0), .vendor = vendor, .device = product };
    struct PciState* state = lockLibrary(base);
    struct pci_dev* result = findClassic(state, previous, &match);

    unlockLibrary(state);

    return result;
}

static struct pci_dev* findClass(ULONG class_code asm("d0"), struct pci_dev* previous asm("a0"),
    struct PciLibraryBase* base asm("a6"))
{
    struct PciMatch match = {.fields = PCI_MATCH_CLASS, .class_code = class_code};
    struct PciState* state = lockLibrary(base);
    struct pci_dev* result = findClassic(state, previous, &match);

    unlockLibrary(state);

    return result;
}

static struct pci_dev* findSlot(UBYTE bus asm("d0"), ULONG devfn asm("d1"), struct PciLibraryBase* base asm("a6"))
{
    struct PciMatch match = {.fields = PCI_MATCH_SLOT, .slot = (ULONG)bus << 8 | (UBYTE)devfn};
    struct PciState* state = lockLibrary(base);
    struct pci_dev* result = findClassic(state, NULL, &match);

    unlockLibrary(state);

    return result;
}

static ULONG readBoardConfig(struct PciLibraryBase* base, struct pci_dev* board, ULONG offset, unsigned width)
{
    struct PciState* state = lockLibrary(base);
    struct PciDevice* device = catalogDevice(&state->hosts.catalog, board);
    ULONG value = ~0UL;

    if (device) {
        readConfig(device->config, device->info.function.device, offset, width, &value);
    }

    unlockLibrary(state);

    return value;
}

static void writeBoardConfig(struct PciLibraryBase* base, struct pci_dev* board,
    ULONG offset, unsigned width, ULONG value)
{
    struct PciState* state = lockLibrary(base);
    struct PciDevice* device = catalogDevice(&state->hosts.catalog, board);

    if (device) {
        writeConfig(device->config, device->info.function.device, offset, width, value);
    }

    unlockLibrary(state);
}

static UBYTE readConfigByte(UBYTE offset asm("d0"), struct pci_dev* board asm("a0"),
    struct PciLibraryBase* base asm("a6"))
{
    return readBoardConfig(base, board, offset, 1);
}

static void writeConfigByte(UBYTE offset asm("d0"), UBYTE value asm("d1"),
    struct pci_dev* board asm("a0"), struct PciLibraryBase* base asm("a6"))
{
    writeBoardConfig(base, board, offset, 1, value);
}

static UWORD readConfigWord(UBYTE offset asm("d0"), struct pci_dev* board asm("a0"),
    struct PciLibraryBase* base asm("a6"))
{
    return readBoardConfig(base, board, offset, 2);
}

static void writeConfigWord(UBYTE offset asm("d0"), UWORD value asm("d1"),
    struct pci_dev* board asm("a0"), struct PciLibraryBase* base asm("a6"))
{
    writeBoardConfig(base, board, offset, 2, value);
}

static ULONG readConfigLong(UBYTE offset asm("d0"), struct pci_dev* board asm("a0"),
    struct PciLibraryBase* base asm("a6"))
{
    return readBoardConfig(base, board, offset, 4);
}

static void writeConfigLong(UBYTE offset asm("d0"), ULONG value asm("d1"),
    struct pci_dev* board asm("a0"), struct PciLibraryBase* base asm("a6"))
{
    writeBoardConfig(base, board, offset, 4, value);
}

static UBYTE readExtConfigByte(ULONG offset asm("d0"), struct pci_dev* board asm("a0"),
    struct PciLibraryBase* base asm("a6"))
{
    return readBoardConfig(base, board, offset, 1);
}

static void writeExtConfigByte(ULONG offset asm("d0"), UBYTE value asm("d1"),
    struct pci_dev* board asm("a0"), struct PciLibraryBase* base asm("a6"))
{
    writeBoardConfig(base, board, offset, 1, value);
}

static UWORD readExtConfigWord(ULONG offset asm("d0"), struct pci_dev* board asm("a0"),
    struct PciLibraryBase* base asm("a6"))
{
    return readBoardConfig(base, board, offset, 2);
}

static void writeExtConfigWord(ULONG offset asm("d0"), UWORD value asm("d1"),
    struct pci_dev* board asm("a0"), struct PciLibraryBase* base asm("a6"))
{
    writeBoardConfig(base, board, offset, 2, value);
}

static ULONG readExtConfigLong(ULONG offset asm("d0"), struct pci_dev* board asm("a0"),
    struct PciLibraryBase* base asm("a6"))
{
    return readBoardConfig(base, board, offset, 4);
}

static void writeExtConfigLong(ULONG offset asm("d0"), ULONG value asm("d1"),
    struct pci_dev* board asm("a0"), struct PciLibraryBase* base asm("a6"))
{
    writeBoardConfig(base, board, offset, 4, value);
}

static BOOL setMaster(struct pci_dev* board asm("a0"), struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    BOOL result = setCatalogMaster(&state->hosts.catalog, board);

    unlockLibrary(state);

    return result;
}

static BOOL obtainCard(struct pci_dev* board asm("a0"), struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    BOOL result = obtainCatalogCard(state->exec_base, &state->hosts.catalog, board);

    unlockLibrary(state);

    return result;
}

static void releaseCard(struct pci_dev* board asm("a0"), struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);

    releaseCatalogCard(state->exec_base, &state->hosts.catalog, board);
    unlockLibrary(state);
}

static struct pci_dev* findBoard(struct pci_dev* previous asm("a0"), struct TagItem* tags asm("a1"),
    struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    struct pci_dev* result = findCatalogBoard(&state->hosts.catalog, previous, tags);

    unlockLibrary(state);

    return result;
}

static ULONG getBoardAttrs(struct pci_dev* board asm("a0"), struct TagItem* tags asm("a1"),
    struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    struct PciDevice* device = catalogDevice(&state->hosts.catalog, board);
    ULONG result = 0;

    for (struct PciHost* host = state->hosts.first; device && host; host = host->next) {
        if (device->config == &host->config) {
            result = getCatalogAttrs(&state->hosts.catalog, board, &host->window, tags);
            break;
        }
    }

    unlockLibrary(state);

    return result;
}

static BOOL setBoardAttrs(struct pci_dev* board asm("a0"), struct TagItem* tags asm("a1"),
    struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    BOOL result = setCatalogAttrs(&state->hosts.catalog, board, tags);

    unlockLibrary(state);

    return result;
}

static APTR allocateDma(ULONG size asm("d0"), ULONG flags asm("d1"), struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    APTR result = allocateBoardDma(state->exec_base, &state->hosts, NULL, size, flags);

    unlockLibrary(state);

    return result;
}

static void releaseDma(APTR memory asm("a0"), ULONG size asm("d0"), struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);

    releaseBoardDma(state->exec_base, &state->hosts, NULL, memory, size);
    unlockLibrary(state);
}

static APTR allocateForBoard(struct pci_dev* board asm("a0"), ULONG size asm("d0"), ULONG flags asm("d1"),
    struct PciLibraryBase* base asm("a6"))
{
    (void)board;

    return allocateDma(size, flags, base);
}

static void releaseForBoard(struct pci_dev* board asm("a0"), APTR memory asm("a1"), ULONG size asm("d0"),
    struct PciLibraryBase* base asm("a6"))
{
    (void)board;
    releaseDma(memory, size, base);
}

static APTR logicalToPhysical(APTR address asm("a0"), struct pci_dev* board asm("a1"),
    struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    APTR result = boardLogicalToPhysical(&state->hosts, board, address);

    unlockLibrary(state);

    return result;
}

static APTR physicalToLogical(APTR address asm("a0"), struct pci_dev* board asm("a1"),
    struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    APTR result = boardPhysicalToLogical(&state->hosts, board, address);

    unlockLibrary(state);

    return result;
}

// The reference ABI accepts these hooks without retaining or calling them.
static BOOL addMemoryHandler(struct pci_dev* board asm("a0"), struct Hook* hook asm("a1"), BYTE priority asm("d0"))
{
    (void)board;
    (void)hook;
    (void)priority;

    return TRUE;
}

static BOOL remMemoryHandler(struct pci_dev* board asm("a0"), struct Hook* hook asm("a1"))
{
    (void)board;
    (void)hook;

    return TRUE;
}

static APTR obtainRegion(struct pci_dev* board asm("a0"), ULONG region asm("a1"), ULONG size asm("d0"),
    struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    APTR result = obtainDeviceRegion(&state->hosts.catalog, board, region, size);

    unlockLibrary(state);

    return result;
}

// BAR mappings last for the device catalog's lifetime; this API does not unmap.
static void releaseRegion(struct pci_dev* board asm("a0"), APTR address asm("a1"))
{
    (void)board;
    (void)address;
}

static ULONG capability(struct pci_dev* board asm("a0"), UBYTE id asm("d0"), struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    struct PciDevice* device = catalogDevice(&state->hosts.catalog, board);
    ULONG result = device ? findCapability(device->config, device->info.function.device, id) : 0;

    unlockLibrary(state);

    return result;
}

static ULONG extCapability(struct pci_dev* board asm("a0"), UWORD id asm("d0"), struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    struct PciDevice* device = catalogDevice(&state->hosts.catalog, board);
    ULONG result = device ? findExtCapability(device->config, device->info.function.device, id) : 0;

    unlockLibrary(state);

    return result;
}

static LONG resetFunction(struct pci_dev* board asm("a0"), struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    LONG result = resetDeviceFunction(state->exec_base, &state->hosts.catalog, board);

    unlockLibrary(state);

    return result;
}

// Task-level IRQ operations hold the catalog lock and exclude guest dispatch.
// The provider alone allocates, routes and controls the native interrupt source.
static LONG allocVectors(struct pci_dev* board asm("a0"), ULONG minimum asm("d0"), ULONG maximum asm("d1"),
    ULONG flags asm("d2"), struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    struct ExecBase* SysBase = state->exec_base;

    Disable();

    LONG result = allocateInterruptVectors(SysBase, state->interrupt_base, &state->hosts.catalog,
        board, minimum, maximum, flags);
    Enable();
    unlockLibrary(state);

    return result;
}

static void freeVectors(struct pci_dev* board asm("a0"), struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    struct ExecBase* SysBase = state->exec_base;

    Disable();
    freeInterruptVectors(SysBase, &state->hosts.catalog, board);
    Enable();
    unlockLibrary(state);
}

static LONG addVector(struct pci_dev* board asm("a0"), ULONG vector asm("d0"), struct Interrupt* server asm("a1"),
    struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    struct ExecBase* SysBase = state->exec_base;

    Disable();

    LONG result = addInterruptVectorServer(SysBase, &state->hosts.catalog, board, vector, server);

    Enable();
    unlockLibrary(state);

    return result;
}

static void remVector(struct pci_dev* board asm("a0"), ULONG vector asm("d0"), struct Interrupt* server asm("a1"),
    struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    struct ExecBase* SysBase = state->exec_base;

    Disable();
    remInterruptVectorServer(&state->hosts.catalog, board, vector, server);
    Enable();
    unlockLibrary(state);
}

// ISR-callable operations must never wait for a task-owned semaphore.
static BOOL maskVector(struct pci_dev* board asm("a0"), ULONG vector asm("d0"), struct PciLibraryBase* base asm("a6"))
{
    return maskInterruptVector(&base->shared->pci.hosts.catalog, board, vector, TRUE);
}

static BOOL unmaskVector(struct pci_dev* board asm("a0"), ULONG vector asm("d0"), struct PciLibraryBase* base asm("a6"))
{
    return maskInterruptVector(&base->shared->pci.hosts.catalog, board, vector, FALSE);
}

static ULONG vectorType(struct pci_dev* board asm("a0"), struct PciLibraryBase* base asm("a6"))
{
    return getInterruptVectorType(&base->shared->pci.hosts.catalog, board);
}

static BOOL checkIntx(struct pci_dev* board asm("a0"), BOOL mask asm("d0"), struct PciLibraryBase* base asm("a6"))
{
    return checkCatalogIntxMask(&base->shared->pci.hosts.catalog, board, mask);
}

static LONG preferMsi(struct pci_dev* board asm("a0"), struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    struct PciDevice* device = catalogDevice(&state->hosts.catalog, board);
    LONG result = PCIE_ERR_INVAL;

    if (device) {
        result = PCIE_ERR_NODEV;

        if (findCapability(device->config, device->info.function.device, 5)) {
            // Initialization retains a message-capable interrupt.library.
            device->prefer_msi = TRUE;
            result = PCIE_OK;
        }
    }

    unlockLibrary(state);

    return result;
}

static void preferIntx(struct pci_dev* board asm("a0"), struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    struct PciDevice* device = catalogDevice(&state->hosts.catalog, board);

    if (device) {
        device->prefer_msi = FALSE;
    }

    unlockLibrary(state);
}

static void maskLegacy(struct pci_dev* board asm("a0"), struct PciLibraryBase* base asm("a6"))
{
    maskVector(board, 0, base);
}

static void unmaskLegacy(struct pci_dev* board asm("a0"), struct PciLibraryBase* base asm("a6"))
{
    unmaskVector(board, 0, base);
}

static BOOL addLegacyServer(struct Interrupt* server asm("a0"), struct pci_dev* board asm("a1"),
    struct PciLibraryBase* base asm("a6"))
{
    struct PciState* state = lockLibrary(base);
    struct ExecBase* SysBase = state->exec_base;
    struct PciDevice* device = catalogDevice(&state->hosts.catalog, board);
    BOOL result = FALSE;

    if (device && device->board.irq && server) {
        ULONG flags = PCI_IRQ_INTX | (device->prefer_msi ? PCI_IRQ_MSI : 0);

        Disable();

        if (allocateInterruptVectors(SysBase, state->interrupt_base, &state->hosts.catalog, board, 1, 1, flags) > 0) {
            result = addInterruptVectorServer(SysBase, &state->hosts.catalog, board, 0, server) == PCIE_OK;

            if (!result) {
                freeInterruptVectors(SysBase, &state->hosts.catalog, board);
            }
        }

        Enable();
    }

    unlockLibrary(state);

    return result;
}

static void remLegacyServer(struct Interrupt* server asm("a0"), struct pci_dev* board asm("a1"),
    struct PciLibraryBase* base asm("a6"))
{
    if (!server) {
        return;
    }

    struct PciState* state = lockLibrary(base);
    struct ExecBase* SysBase = state->exec_base;

    Disable();
    remInterruptVectorServer(&state->hosts.catalog, board, 0, server);
    freeInterruptVectors(SysBase, &state->hosts.catalog, board);
    Enable();
    unlockLibrary(state);
}

static const APTR function_table[] = {
    (APTR)libOpen, (APTR)libClose, (APTR)libExpunge, (APTR)libNull,
    (APTR)pciBus, (APTR)inputByte, (APTR)outputByte, (APTR)inputWord, (APTR)outputWord,
    (APTR)inputLong, (APTR)outputLong, (APTR)copyMemory, (APTR)copyMemory, (APTR)copyMemory,
    (APTR)findDevice, (APTR)findClass, (APTR)findSlot,
    (APTR)readConfigByte, (APTR)readConfigWord, (APTR)readConfigLong,
    (APTR)writeConfigByte, (APTR)writeConfigWord, (APTR)writeConfigLong,
    (APTR)setMaster, (APTR)addLegacyServer, (APTR)remLegacyServer,
    (APTR)allocateDma, (APTR)releaseDma, (APTR)logicalToPhysical, (APTR)physicalToLogical,
    (APTR)obtainCard, (APTR)releaseCard, (APTR)libNull,
    (APTR)findBoard, (APTR)getBoardAttrs, (APTR)setBoardAttrs, (APTR)allocateForBoard, (APTR)releaseForBoard,
    (APTR)addMemoryHandler, (APTR)remMemoryHandler, (APTR)obtainRegion, (APTR)releaseRegion,
    (APTR)capability, (APTR)extCapability, (APTR)preferMsi, (APTR)preferIntx, (APTR)resetFunction,
    (APTR)readExtConfigByte, (APTR)readExtConfigWord, (APTR)readExtConfigLong,
    (APTR)writeExtConfigByte, (APTR)writeExtConfigWord, (APTR)writeExtConfigLong,
    (APTR)maskLegacy, (APTR)unmaskLegacy, (APTR)checkIntx,
    (APTR)allocVectors, (APTR)freeVectors, (APTR)addVector, (APTR)remVector,
    (APTR)maskVector, (APTR)unmaskVector, (APTR)vectorType, (APTR)-1
};
_Static_assert(sizeof(function_table) / sizeof(*function_table) == 64, "PCI vector table must end at -378");

static struct Library* residentInit(ULONG seglist asm("a0"), struct ExecBase* SysBase asm("a6"))
{
    Forbid();

    struct Library* library = (void*)FindName(&SysBase->LibList, (STRPTR)library_name);

    if (!library && !FindName(&SysBase->LibList, (STRPTR)compatibility_name)) {
        struct PciLibraryState* shared = AllocMem(sizeof(*shared), MEMF_PUBLIC | MEMF_CLEAR);

        if (shared) {
            shared->seglist = seglist;

            for (unsigned i = 0; i < 2; ++i) {
                struct PciLibraryBase* base = (void*)MakeLibrary((APTR)function_table, NULL, NULL,
                    sizeof(*base), seglist);

                if (!base) {
                    break;
                }

                shared->bases[i] = base;
                base->shared = shared;
                base->library.lib_Node.ln_Type = NT_LIBRARY;
                base->library.lib_Node.ln_Name = (char*)(i ? compatibility_name : library_name);
                base->library.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
                base->library.lib_Version = LIBRARY_VERSION;
                base->library.lib_Revision = LIBRARY_REVISION;
                base->library.lib_IdString = (STRPTR)(i ? compatibility_id : library_id);
            }

            if (shared->bases[0] && shared->bases[1] && initializePciState(SysBase, &shared->pci)) {
                library = &shared->bases[0]->library;
                AddLibrary(library);
                AddLibrary(&shared->bases[1]->library);
            } else {
                for (unsigned i = 0; i < 2; ++i) {
                    if (shared->bases[i]) {
                        freeBase(SysBase, shared->bases[i]);
                    }
                }

                FreeMem(shared, sizeof(*shared));
            }
        }
    }

    Permit();

    return library;
}
