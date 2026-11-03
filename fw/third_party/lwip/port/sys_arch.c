/* sys_arch.c -- the lwIP system-architecture shim for the Pano G1, NO_SYS mode.
 *
 * NO_SYS=1: the whole stack is polled from one thread in the shell's main loop,
 * so there are no mailboxes, no threads and nothing to protect.  The only
 * real service lwIP needs is time -- sys_now(), which is the board's own
 * millisecond base (fw/timer_ms.c), already the shell countdown's clock.
 */
#include "lwip/opt.h"
#include "lwip/sys.h"
#include "timer_ms.h"

u32_t sys_now(void)
{
    return get_millis();
}

/* Single-threaded: no preemption reaches lwIP state, so the critical-section
 * hooks are intentionally empty.  (The timer ISR only bumps a counter it owns.) */
sys_prot_t sys_arch_protect(void)
{
    return 0;
}

void sys_arch_unprotect(sys_prot_t pval)
{
    (void)pval;
}

/* The millisecond clock is owned by fw/timer_ms.c and initialised by main(), not
 * by lwIP -- a program chooses its timer mode once, not per library. */
void sys_init(void)
{
}
