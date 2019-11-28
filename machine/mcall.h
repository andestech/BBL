// See LICENSE for license details.

#ifndef _RISCV_SBI_H
#define _RISCV_SBI_H

#define SBI_SET_TIMER 0
#define SBI_CONSOLE_PUTCHAR 1
#define SBI_CONSOLE_GETCHAR 2
#define SBI_CLEAR_IPI 3
#define SBI_SEND_IPI 4
#define SBI_REMOTE_FENCE_I 5
#define SBI_REMOTE_SFENCE_VMA 6
#define SBI_REMOTE_SFENCE_VMA_ASID 7
#define SBI_SHUTDOWN 8
#define SBI_TRIGGER 9
#define SBI_SET_PFM 10
#define SBI_READ_POWERBRAKE 11
#define SBI_WRITE_POWERBRAKE 12
#define SBI_SUSPEND_PREPARE 13
#define SBI_SUSPEND_MEM 14
#define SBI_DCACHE_OP 15
#define SBI_ICACHE_OP 16
#define SBI_L1CACHE_STATUS 17
#endif
