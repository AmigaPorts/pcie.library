/* SPDX-License-Identifier: MPL-2.0 */
#include "interrupt_vectors.h"

#ifdef __mc68000__
#define CALLBACK_REGISTER(name) asm(name)
#else
#define CALLBACK_REGISTER(name)
#endif

static ULONG dispatch(ULONG irq CALLBACK_REGISTER("d0"), struct PciInterruptServer* server CALLBACK_REGISTER("a1"),
    struct ExecBase* exec_base CALLBACK_REGISTER("a6"))
{
    struct Interrupt* client = server->client;

    if (!client || !client->is_Code) {
        return 0;
    }

#ifdef __mc68000__
    register ULONG result asm("d0") = irq;
    register APTR data asm("a1") = client->is_Data;
    register struct ExecBase* sys_base asm("a6") = exec_base;

    __asm__ volatile("jsr (%[code])"
                     : "+r"(result), "+r"(data), "+r"(sys_base)
                     : [code] "a"(client->is_Code)
                     : "d1", "a0", "a5", "cc", "memory");

    return result;
#else
    // Host tests exercise ownership and argument forwarding with the C ABI;
    // the real 68k fixture validates the register ABI above.
    return ((ULONG(*)(ULONG, APTR, struct ExecBase*))client->is_Code)(irq, client->is_Data, exec_base);
#endif
}

void initPciInterruptServer(struct PciInterruptServer* server)
{
    *server = (struct PciInterruptServer){0};
#ifdef __mc68000__
    server->server.is_Node.ln_Type = NT_INTERRUPT;
#endif
    server->server.is_Code = (void (*)())dispatch;
    server->server.is_Data = server;
}
