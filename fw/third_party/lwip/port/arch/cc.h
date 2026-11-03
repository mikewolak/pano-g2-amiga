/* arch/cc.h -- lwIP architecture definitions for the Pano G1 (VexRiscv RV32IM).
 *
 * Standard RISC-V little-endian, bare-metal.  No PicoRV32 maskirq here: VexRiscv
 * is ordinary machine mode, and NO_SYS means the whole stack is polled from one
 * thread, so there is nothing for the arch to protect.
 */
#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef uint8_t     u8_t;
typedef int8_t      s8_t;
typedef uint16_t    u16_t;
typedef int16_t     s16_t;
typedef uint32_t    u32_t;
typedef int32_t     s32_t;

typedef uintptr_t   mem_ptr_t;
typedef u32_t       sys_prot_t;

#define PACK_STRUCT_FIELD(x)    x
#define PACK_STRUCT_STRUCT      __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

#define BYTE_ORDER              LITTLE_ENDIAN

/* mcycle, not rdcycle: we run in machine mode (see fw/console.h).  A cheap,
 * always-advancing entropy source for DHCP xid and TCP ISNs -- good enough for
 * bring-up, and it costs no state and no libc rand(). */
static inline u32_t lwip_rand_u32(void)
{
    u32_t v;
    __asm__ volatile ("csrr %0, mcycle" : "=r"(v));
    return v;
}
#define LWIP_RAND()             lwip_rand_u32()

/* Diagnostics go to the JTAG console.  Debug is compiled off in the shipping
 * build (lwipopts.h), but an assert must be visible. */
#include "console.h"
#define LWIP_PLATFORM_DIAG(x)   do { } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { con_puts("LWIP ASSERT "); con_puts(x); con_puts("\r\n"); while (1) { } } while (0)

#endif /* LWIP_ARCH_CC_H */
