/* SPDX-License-Identifier: GPL-3.0-only */
/* The AC79's second core. Jangada 1.0, after X0X (github.com/charlesvestal/fm1-x0x, GPL-3.0: hal/fm1_cpu1.h),
 * which found it with Melodee's dual-core audio (github.com/keremimo/melodee, GPL-3.0) and the AC79 SDK.
 *
 * Start (the AC79 SDK's EnableOtherCpu): core 1's interrupt bank off (0x1EEF300..), its entry
 * address into the word at 0x01C7FFF8, bit 3 of 0x10008 held while it starts, C1_CON (0x1EEE004)
 * bit 3 set (enabled) and bit 1 cleared (out of hold). It boots through the chip's ROM, which the
 * PC limits refuse, so they are open until it has reported in from fm1_c1_main. Hold is bit 1
 * set, then bit 3 cleared. Started later than power-on (from the main loop) it never reaches its entry.
 *
 * Work: one job at a time, a function and an argument; core 0 hands it over (fm1_cpu1_run) and waits
 * for it (fm1_cpu1_wait), single writer each way, csync around each (the cores share the caches).
 *
 * Flash: the waiting loop is in RAM (.c1_text), so an idle core 1 never fetches from the flash;
 * it works only for the audio interrupt, which waits for it before it returns, and core 0 turns
 * the flash off only with interrupts off (in the main loop). So the flash is never off under it. */
#pragma once
#include <stdint.h>

#define FM1_C1_CON (*(volatile uint32_t *)0x1EEE004u)
#define FM1_C1_CLK (*(volatile uint32_t *)0x00010008u)
#define FM1_C1_T4 (*(volatile uint32_t *)0x10804u)       /* TIMER4, 24 MHz */
#define FM1_C1_SYNC() __asm__ volatile("csync" ::: "memory")

extern void fm1_c1_entry(void);
extern uint32_t _c1_ustack[], _c1_sstack_top[];
#define FM1_C1_STACK_WORDS (4096u / 4u)          /* fm1_cpu1.S; a part's render: ~1.6 KB at the deepest (PHYS SYMP) */
#define FM1_C1_MARK 0x43314D4Bu                  /* "C1MK": how deep core 1's stack has gone */
/* the mailbox: what core 0 writes and what core 1 writes on cache lines of their own, nothing else on them */
typedef struct {
    volatile uint32_t job, arg;                 /* core 0: job + 1 hands fn(arg) over */
    void (*volatile fn)(uint32_t);
    uint32_t pad0[13];
    volatile uint32_t alive, done;              /* core 1: started; the last job it finished */
    volatile uint32_t bad, bad_job, bad_done, bad_fn;   /* a job it would not run: how often, the last one seen */
    uint32_t pad1[10];
} fm1_c1_mb_t;
static fm1_c1_mb_t fm1_c1_mb __attribute__((aligned(64)));
volatile uint32_t fm1_c1_trace;                 /* breadcrumbs (fm1_cpu1.S, fm1_c1_main) */
static uint8_t fm1_c1_on;                       /* started and answering: core 0 may hand it work */
static uint8_t fm1_c1_failed;                   /* held for this session: it misread its mailbox or a job timed out */
static uint32_t fm1_c1_wait_max, fm1_c1_timeouts; /* the longest wait for a job (TIMER4 ticks), jobs given up */

/* only the next job, and only with a function: anything else is counted and noted, not run */
void fm1_c1_main(void);
void __attribute__((section(".c1_text"), noreturn, used)) fm1_c1_main(void)
{
    /* its own guards, as Melodee sets them (its fm1_core1_main): core 1's EMU bank (core 0's is at 0x1EEF0D0)
     * gets a stack limit over both of its stacks, less the 256 lowest bytes (an overflow traps before it
     * reaches anything else), and EMU_CON: bit 2 and bits 16..20 off, bit 3 (the stack limit) on. What the
     * chip's ROM left there otherwise stays */
    uint32_t lo = (uint32_t)(uintptr_t)_c1_ustack + 256u, hi = (uint32_t)(uintptr_t)_c1_sstack_top - 1u;
    *(volatile uint32_t *)0x1EEF2D8u = hi;
    *(volatile uint32_t *)0x1EEF2DCu = lo;
    *(volatile uint32_t *)0x1EEF2E0u = hi;
    *(volatile uint32_t *)0x1EEF2E4u = lo;
    *(volatile uint32_t *)0x1EEF2D0u = (*(volatile uint32_t *)0x1EEF2D0u & ~((1u << 2) | (0x1Fu << 16))) | (1u << 3);
    fm1_c1_trace = 0xC1000002;
    fm1_c1_mb.alive = 1;
    FM1_C1_SYNC();
    for (;;) {
        uint32_t j = fm1_c1_mb.job, d;
        FM1_C1_SYNC();
        d = fm1_c1_mb.done;
        if (j != d) {
            void (*fn)(uint32_t) = fm1_c1_mb.fn;
            if (fn && j == d + 1u) {
                fn(fm1_c1_mb.arg);              /* the work itself runs from the flash */
            } else {
                fm1_c1_mb.bad++;
                fm1_c1_mb.bad_job = j;
                fm1_c1_mb.bad_done = d;
                fm1_c1_mb.bad_fn = (uint32_t)(uintptr_t)fn;
            }
            FM1_C1_SYNC();
            fm1_c1_mb.done = j;
            FM1_C1_SYNC();
        }
    }
}

/* hold core 1, registers only: also at the very start of a boot, before the RAM it may still be reading is
 * cleared (a reset that did not hold it: a watchdog, a fault) */
static inline void fm1_cpu1_halt(void)
{
    FM1_C1_CON |= 0x2u;
    FM1_C1_CON &= ~0x8u;
    FM1_C1_SYNC();
}

static void fm1_cpu1_hold(void)
{
    fm1_cpu1_halt();
    fm1_c1_on = 0;
}

/* 0 = core 1 runs and answered; else it is held again. The top of RAM must be writable
 * (fm1_guard_unlock_top) and the PC limits open (fm1_guard_pc_open): the caller's */
static int fm1_cpu1_start(void)
{
    uint32_t saved, t0, i;
    if (fm1_c1_on)
        return 0;
    fm1_cpu1_hold();
    for (i = 0; i < FM1_C1_STACK_WORDS; i++)
        _c1_ustack[i] = FM1_C1_MARK;
    fm1_c1_mb.alive = 0;
    fm1_c1_trace = 0;
    fm1_c1_mb.done = fm1_c1_mb.job;
    fm1_c1_mb.fn = 0;
    FM1_C1_SYNC();
    for (i = 0; i < 32u; i++)                   /* its interrupt bank: all off (core 0's is at 0x1EEF100) */
        *(volatile uint32_t *)(0x1EEF300u + 4u * i) = 0;
    *(volatile uint32_t *)0x01C7FFF8u = (uint32_t)(uintptr_t)&fm1_c1_entry;
    saved = FM1_C1_CLK;
    FM1_C1_CLK = saved | 0x8u;
    FM1_C1_SYNC();
    FM1_C1_CON |= 0x8u;
    FM1_C1_CON &= ~0x2u;
    t0 = FM1_C1_T4;
    while (!fm1_c1_mb.alive && FM1_C1_T4 - t0 < 24000u * 20u)   /* 20 ms */
        FM1_C1_SYNC();
    FM1_C1_CLK = saved;
    if (!fm1_c1_mb.alive) {
        fm1_cpu1_hold();
        return -1;
    }
    fm1_c1_on = 1;
    return 0;
}

/* hand core 1 a job: 1 = it took it (fm1_cpu1_wait before touching what it works on) */
static inline int fm1_cpu1_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!fm1_c1_on)
        return 0;
    if (fm1_c1_mb.bad) {                        /* it misread the mailbox (a unit that needs more supply, fm1_sys.h):
                                                 * held before it runs anything else, as Melodee and X0X do */
        fm1_cpu1_hold();
        fm1_c1_failed = 1;
        return 0;
    }
    fm1_c1_mb.fn = fn;
    fm1_c1_mb.arg = arg;
    FM1_C1_SYNC();
    fm1_c1_mb.job = fm1_c1_mb.job + 1u;
    FM1_C1_SYNC();
    return 1;
}

/* 0 = done; -1 = it did not finish in time: core 1 is held from now on (the caller does the work) */
static inline int fm1_cpu1_wait(void)
{
    uint32_t t0 = FM1_C1_T4;
    for (;;) {
        uint32_t dt;
        FM1_C1_SYNC();
        dt = FM1_C1_T4 - t0;
        if (fm1_c1_mb.done == fm1_c1_mb.job) {
            if (dt > fm1_c1_wait_max)
                fm1_c1_wait_max = dt;
            return 0;
        }
        if (dt > 24000u * 4u) {                 /* 4 ms: past any block (a half is 5.8 ms) */
            fm1_c1_timeouts++;
            fm1_cpu1_hold();
            fm1_c1_failed = 1;
            return -1;
        }
    }
}

/* bytes of core 1's stack ever used (the mark left untouched below them) */
static uint32_t fm1_cpu1_stack_used(void)
{
    uint32_t i = 0;
    while (i < FM1_C1_STACK_WORDS && _c1_ustack[i] == FM1_C1_MARK)
        i++;
    return (FM1_C1_STACK_WORDS - i) * 4u;
}
