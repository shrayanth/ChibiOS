/*
    ChibiOS/RT - Copyright (C) 2006,2007,2008,2009,2010 Giovanni Di Sirio.

    This file is part of ChibiOS/RT.

    ChibiOS/RT is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    ChibiOS/RT is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file    ARMCMx/chcore.c
 * @brief   ARM Cortex-Mx architecture port code.
 *
 * @addtogroup ARMCMx_CORE
 * @{
 */

#include "ch.h"
#include "nvic.h"

/**
 * @brief   Halts the system.
 * @note    The function is declared as a weak symbol, it is possible
 *          to redefine it in your application code.
 */
#if !defined(__DOXYGEN__)
__attribute__((weak))
#endif
void port_halt(void) {

  port_disable();
  while (TRUE) {
  }
}

#if !CH_OPTIMIZE_SPEED
void _port_lock(void) {
  register uint32_t tmp asm ("r3") = BASEPRI_KERNEL;
  asm volatile ("msr     BASEPRI, %0" : : "r" (tmp));
}

void _port_unlock(void) {
  register uint32_t tmp asm ("r3") = BASEPRI_USER;
  asm volatile ("msr     BASEPRI, %0" : : "r" (tmp));
}
#endif

/**
 * @brief   System Timer vector.
 * @details This interrupt is used as system tick.
 * @note    The timer must be initialized in the startup code.
 */
void SysTickVector(void) {

  chSysLockFromIsr();
  chSysTimerHandlerI();
  if (chSchIsRescRequiredExI())
    SCB_ICSR = ICSR_PENDSVSET;
  chSysUnlockFromIsr();
}

#if CORTEX_MODEL == CORTEX_M0
#define PUSH_CONTEXT(sp, prio) {                                        \
  asm volatile ("mrs     %0, PSP                                \n\t"   \
                "sub     %0, %0, #20                            \n\t"   \
                "push    {r3-r7}                                \n\t"   \
                "mov     r0, r8                                 \n\t"   \
                "str     r0, [%0, #20]                          \n\t"   \
                "mov     r0, r9                                 \n\t"   \
                "str     r0, [%0, #24]                          \n\t"   \
                "mov     r0, r10                                \n\t"   \
                "str     r0, [%0, #28]                          \n\t"   \
                "mov     r0, r11                                \n\t"   \
                "str     r0, [%0, #32]                          \n\t"   \
                "mov     r0, lr                                 \n\t"   \
                "str     r0, [%0, #36]                          \n\t"   \
                : "=r" (sp) : "r" (sp), "r" (prio));                    \
}

#define POP_CONTEXT(sp) {                                               \
  asm volatile ("ldr     r0, [%0, #20]                          \n\t"   \
                "mov     r8, r0                                 \n\t"   \
                "ldr     r0, [%0, #24]                          \n\t"   \
                "mov     r9, r0                                 \n\t"   \
                "ldr     r0, [%0, #28]                          \n\t"   \
                "mov     r10, r0                                \n\t"   \
                "ldr     r0, [%0, #32]                          \n\t"   \
                "mov     r11, r0                                \n\t"   \
                "ldr     r0, [%0, #36]                          \n\t"   \
                "mov     lr, r0                                 \n\t"   \
                "pop     {r3-r7}                                \n\t"   \
                "add     %0, %0, #20                            \n\t"   \
                "msr     PSP, %0                                \n\t"   \
                "msr     BASEPRI, r3                            \n\t"   \
                "bx      lr" : "=r" (sp) : "r" (sp));                   \
}
#else /* CORTEX_MODEL != CORTEX_M0 */
#if !defined(CH_CURRP_REGISTER_CACHE)
#define PUSH_CONTEXT(sp, prio) {                                        \
  asm volatile ("mrs     %0, PSP                                \n\t"   \
                "stmdb   %0!, {r3-r11,lr}" :                            \
                "=r" (sp) : "r" (sp), "r" (prio));                      \
}

#define POP_CONTEXT(sp) {                                               \
  asm volatile ("ldmia   %0!, {r3-r11, lr}                      \n\t"   \
                "msr     PSP, %0                                \n\t"   \
                "msr     BASEPRI, r3                            \n\t"   \
                "bx      lr" : "=r" (sp) : "r" (sp));                   \
}
#else /* defined(CH_CURRP_REGISTER_CACHE) */
#define PUSH_CONTEXT(sp, prio) {                                        \
  asm volatile ("mrs     %0, PSP                                \n\t"   \
                "stmdb   %0!, {r3-r6,r8-r11, lr}" :                     \
                "=r" (sp) : "r" (sp), "r" (prio));                      \
}

#define POP_CONTEXT(sp) {                                               \
  asm volatile ("ldmia   %0!, {r3-r6,r8-r11, lr}                \n\t"   \
                "msr     PSP, %0                                \n\t"   \
                "msr     BASEPRI, r3                            \n\t"   \
                "bx      lr" : "=r" (sp) : "r" (sp));                   \
}
#endif /* defined(CH_CURRP_REGISTER_CACHE) */
#endif /* CORTEX_MODEL != CORTEX_M0 */

/**
 * @brief   SVC vector.
 * @details The SVC vector is used for commanded context switch. Structures
 *          @p intctx are saved and restored from the process stacks of the
 *          switched threads.
 *
 * @param[in] ntp       the thread to be switched it
 * @param[in] otp       the thread to be switched out
 */
#if !defined(__DOXYGEN__)
__attribute__((naked))
#endif
void SVCallVector(Thread *ntp, Thread *otp) {
  register struct intctx *sp_thd asm("r2");
  register uint32_t prio asm ("r3");

  asm volatile ("mrs     r3, BASEPRI" : "=r" (prio) : );
  PUSH_CONTEXT(sp_thd, prio)

  otp->p_ctx.r13 = sp_thd;
  sp_thd = ntp->p_ctx.r13;

  POP_CONTEXT(sp_thd)
}

/**
 * @brief   Preemption code.
 */
#if !defined(__DOXYGEN__)
__attribute__((naked))
#endif
void PendSVVector(void) {
  register struct intctx *sp_thd asm("r2");
  register uint32_t prio asm ("r3");
  Thread *otp, *ntp;

  chSysLockFromIsr();

  prio = CORTEX_BASEPRI_USER;
  PUSH_CONTEXT(sp_thd, prio)

  (otp = currp)->p_ctx.r13 = sp_thd;
  ntp = fifo_remove(&rlist.r_queue);
  setcurrp(ntp);
  ntp->p_state = THD_STATE_CURRENT;
  chSchReadyI(otp);
#if CH_TIME_QUANTUM > 0
  /* Set the round-robin time quantum.*/
  rlist.r_preempt = CH_TIME_QUANTUM;
#endif
  chDbgTrace(otp);
  sp_thd = ntp->p_ctx.r13;

  POP_CONTEXT(sp_thd)
}

/** @} */