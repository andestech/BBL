// See LICENSE for license details.

#include "mtrap.h"
#include "mcall.h"
#include "htif.h"
#include "atomic.h"
#include "bits.h"
#include "vm.h"
#include "uart.h"
#include "uart16550.h"
#include "finisher.h"
#include "fdt.h"
#include "unprivileged_memory.h"
#include "disabled_hart_mask.h"
#include "trigger.h"
#include "smu.h"
#include "pma.h"
#include "encoding.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

void __attribute__((noreturn)) bad_trap(uintptr_t* regs, uintptr_t dummy, uintptr_t mepc)
{
  die("machine mode: unhandlable trap %d @ %p", read_csr(mcause), mepc);
}

static uintptr_t mcall_console_putchar(uint8_t ch)
{
  if (uart) {
    uart_putchar(ch);
  } else if (uart16550) {
    uart16550_putchar(ch);
  } else if (htif) {
    htif_console_putchar(ch);
  }
  return 0;
}

void putstring(const char* s)
{
  while (*s)
    mcall_console_putchar(*s++);
}

void vprintm(const char* s, va_list vl)
{
  char buf[256];
  vsnprintf(buf, sizeof buf, s, vl);
  putstring(buf);
}

void printm(const char* s, ...)
{
  va_list vl;

  va_start(vl, s);
  vprintm(s, vl);
  va_end(vl);
}

static void send_ipi(uintptr_t recipient, int event)
{
  if (((disabled_hart_mask >> recipient) & 1)) return;
  atomic_or(&OTHER_HLS(recipient)->mipi_pending, event);
  mb();
  plic_sw_pending(recipient);
}

static uintptr_t mcall_console_getchar()
{
  if (uart) {
    return uart_getchar();
  } else if (uart16550) {
    return uart16550_getchar();
  } else if (htif) {
    return htif_console_getchar();
  } else {
    return '\0';
  }
}

static uintptr_t mcall_clear_ipi()
{
  return clear_csr(mip, MIP_SSIP) & MIP_SSIP;
}

static uintptr_t mcall_shutdown()
{
  poweroff(0);
}

static uintptr_t mcall_set_timer(uint64_t when)
{
  *HLS()->timecmp = when;
  clear_csr(mip, MIP_STIP);
  set_csr(mie, MIP_MTIP);
  return 0;
}

static uintptr_t mcall_set_pfm()
{
  clear_csr(slip, MIP_SOVFIP);
  set_csr(mie, MIP_MOVFIP);
  return 0;
}

static uintptr_t mcall_read_powerbrake()
{
  return read_csr(mpft_ctl);
}

static uintptr_t mcall_write_powerbrake(int val)
{
  write_csr(mpft_ctl, val);
  return 0;
}

static uintptr_t mcall_suspend_prepare(char main_core, char enable)
{
  if (main_core) {
	/* 
	 * Clear the mie.mtie, mie.msie, mie.meie,
	 * it's bit field is align with mip.
	 */
	if (enable) {
		set_csr(mie, MIP_MTIP);
		set_csr(mie, MIP_MSIP);
		set_csr(mie, MIP_MEIP);
	} else {
		clear_csr(mie, MIP_MTIP);
		clear_csr(mie, MIP_MSIP);
		clear_csr(mie, MIP_MEIP);
	}
  } else {
	if (enable) {
		set_csr(mie, MIP_MEIP);
		set_csr(mie, MIP_MTIP);
	} else {
		clear_csr(mie, MIP_MEIP);
		clear_csr(mie, MIP_MTIP);
	}
  }
  return 0;
}

static uintptr_t mcall_l1cache_status(void)
{
	return read_csr(mcache_ctl);
}

static uintptr_t mcall_dcache_op(unsigned int enable)
{
	if (enable)
		set_csr(mcache_ctl, V5_MCACHE_CTL_DC_EN);
	else {
		clear_csr(mcache_ctl, V5_MCACHE_CTL_DC_EN);
		write_csr(ucctlcommand, V5_MCACHE_L1D_WBINVAL_ALL);
	}
	return 0;
}

static uintptr_t mcall_icache_op(unsigned int enable)
{
	if (enable)
		set_csr(mcache_ctl, V5_MCACHE_CTL_IC_EN);
	else {
		clear_csr(mcache_ctl, V5_MCACHE_CTL_IC_EN);
		asm volatile("fence.i\n\t");
	}
	return 0;
}

extern void cpu_suspend2ram(void);
extern uint64_t hart_mask;

static uintptr_t mcall_suspend_backup(void)
{
	cpu_suspend2ram();
	return 0;
}

static uintptr_t mcall_set_trigger(long type, uintptr_t data, unsigned int m,
                                   unsigned int s, unsigned int u)
{
  int ret;

  switch (type)
  {
    case TRIGGER_TYPE_ICOUNT:
      ret = trigger_set_icount(data, m, s, u);
      break;
    case TRIGGER_TYPE_ITRIGGER:
      ret = trigger_set_itrigger(data, m, s, u);
      break;
    case TRIGGER_TYPE_ETRIGGER:
      ret = trigger_set_etrigger(data, m, s, u);
      break;
    default:
      ret = -1;
      break;
  }
  return ret;
}

static void send_ipi_many(uintptr_t* pmask, int event)
{
  _Static_assert(MAX_HARTS <= 8 * sizeof(*pmask), "# harts > uintptr_t bits");
  uintptr_t mask = hart_mask;
  if (pmask)
    mask &= load_uintptr_t(pmask, read_csr(mepc));

  // send IPIs to everyone
  for (uintptr_t i = 0, m = mask; m; i++, m >>= 1)
    if (m & 1)
      send_ipi(i, event);

  if (event == IPI_SOFT)
    return;

  // wait until all events have been handled.
  // prevent deadlock by consuming incoming IPIs.
  uint32_t incoming_ipi = 0;
  for (uintptr_t i = 0, m = mask; m; i++, m >>= 1)
    if (m & 1)
      while (plic_sw_get_pending(i)) {
        plic_sw_claim();
        if (HLS()->plic_sw.source_id) {
          incoming_ipi |= 1 << (HLS()->plic_sw.source_id);
          plic_sw_complete();
        }
      }

  // if we got an IPI, restore it; it will be taken after returning
  if (incoming_ipi) {
    *(HLS()->plic_sw.pending) = incoming_ipi;
    mb();
  }
}

static void mcall_restart(int cpu_nums)
{
  int i;
  unsigned int *dev_ptr;
  unsigned char *tmp;

  for (i = 0; i < cpu_nums; i++) {
    dev_ptr = (unsigned int *)((unsigned long)SMU_BASE + SMU_RESET_VEC_OFF
				+ SMU_RESET_VEC_PER_CORE*i);
    *dev_ptr = DRAM_BASE;
  }

  dev_ptr = (unsigned int *)((unsigned int)SMU_BASE + PCS0_CTL_OFF);
  tmp = (unsigned char *)dev_ptr;
  *tmp = RESET_CMD;
  __asm__("wfi");
  while(1){};
}

extern void cpu_resume(void);
static void mcall_set_reset_vec(int cpu_nums)
{
  int i;
  unsigned int *dev_ptr;
  unsigned int *tmp = (unsigned int *)&cpu_resume;

  for (i = 0; i < cpu_nums; i++) {
    dev_ptr = (unsigned int *)((unsigned long)SMU_BASE + SMU_RESET_VEC_OFF
                                + SMU_RESET_VEC_PER_CORE*i);

    *dev_ptr = (unsigned long)tmp;
  }
}

static void mcall_set_pma(unsigned int pa, unsigned long va, unsigned long size)
{
  int i, power = 0;
  unsigned long size_tmp, shift = 0, pmacfg_val;
  char *pmaxcfg;
  unsigned long mmsc = read_csr(mmsc_cfg);

  if ((mmsc & PMA_MMSC_CFG) == 0)
    return;

  if ((pa & (size - 1)) !=0) {
    pa = pa & ~(size - 1);
    size = size << 1;
  }

  /* Calculate the NAPOT table for pmaaddr */
  size_tmp = size;
  while (size_tmp != 0x1) {
    size_tmp = size_tmp >> 1;
    power++;
    if (power > 3)
      shift = (shift << 1) | 0x1;
  }

  for (i = 0; i < PMA_NUM; i++) {
    if (!pma_used_table[i]) {
      pma_used_table[i] = va;
      pa = pa >> 2;
      pa = pa | shift;
      pa = pa & ~(0x1 << (power - 3));
#if __riscv_xlen == 64
      pmacfg_val = read_pmacfg(i / 8);
      pmaxcfg = (char *)&pmacfg_val;
      pmaxcfg = pmaxcfg + (i % 8);
      *pmaxcfg = 0;
      *pmaxcfg = *pmaxcfg | PMA_NAPOT;
      *pmaxcfg = *pmaxcfg | PMA_NOCACHE_BUFFER;
      write_pmacfg(i / 8, pmacfg_val);
#else
      pmacfg_val = read_pmacfg(i / 4);
      pmaxcfg = (char *)&pmacfg_val;
      pmaxcfg = pmaxcfg + (i % 4);
      *pmaxcfg = 0;
      *pmaxcfg = *pmaxcfg | PMA_NAPOT;
      *pmaxcfg = *pmaxcfg | PMA_NOCACHE_BUFFER;
      write_pmacfg(i / 4, pmacfg_val);
#endif
      write_pmaaddr(i, pa);
      return;
    }
  }
  /* There is no available pma register */
  __asm__("ebreak");
}

static void mcall_free_pma(unsigned long va)
{
  int i;

  for(i = 0 ; i < PMA_NUM; i++) {
    if(pma_used_table[i] == va) {
      pma_used_table[i] = 0;
#if __riscv_xlen == 64
      write_pmacfg(i / 8, 0);
#else
      write_pmacfg(i / 4, 0);
#endif
      return;
    }
  }
}

void mcall_trap(uintptr_t* regs, uintptr_t mcause, uintptr_t mepc)
{
  write_csr(mepc, mepc + 4);

  uintptr_t n = regs[17], arg0 = regs[10], arg1 = regs[11], arg2 = regs[12], retval, ipi_type;

  switch (n)
  {
    case SBI_CONSOLE_PUTCHAR:
      retval = mcall_console_putchar(arg0);
      break;
    case SBI_CONSOLE_GETCHAR:
      retval = mcall_console_getchar();
      break;
    case SBI_SEND_IPI:
      ipi_type = IPI_SOFT;
      goto send_ipi;
    case SBI_REMOTE_SFENCE_VMA:
    case SBI_REMOTE_SFENCE_VMA_ASID:
      ipi_type = IPI_SFENCE_VMA;
      goto send_ipi;
    case SBI_REMOTE_FENCE_I:
      ipi_type = IPI_FENCE_I;
send_ipi:
      send_ipi_many((uintptr_t*)arg0, ipi_type);
      retval = 0;
      break;
    case SBI_CLEAR_IPI:
      retval = mcall_clear_ipi();
      break;
    case SBI_SHUTDOWN:
      retval = mcall_shutdown();
      break;
    case SBI_SET_TIMER:
#if __riscv_xlen == 32
      retval = mcall_set_timer(arg0 + ((uint64_t)arg1 << 32));
#else
      retval = mcall_set_timer(arg0);
#endif
      break;
    case SBI_TRIGGER:
      retval = mcall_set_trigger(arg0, arg1, 0, 0, arg2);
      break;
    case SBI_SET_PFM:
      retval = mcall_set_pfm();
      break;
    case SBI_READ_POWERBRAKE:
      retval = mcall_read_powerbrake();
      break;
    case SBI_WRITE_POWERBRAKE:
      retval = mcall_write_powerbrake(arg0);
      break;
    case SBI_SUSPEND_PREPARE:
      retval = mcall_suspend_prepare(arg0, arg1);
      break;
    case SBI_SUSPEND_MEM:
      retval = mcall_suspend_backup();
      break;
    case SBI_DCACHE_OP:
      retval = mcall_dcache_op(arg0);
      break;
    case SBI_ICACHE_OP:
      retval = mcall_icache_op(arg0);
      break;
    case SBI_L1CACHE_STATUS:
      retval = mcall_l1cache_status();
      break;
    case SBI_RESTART:
      mcall_restart(arg0);
      retval = 0;
      break;
    case SBI_SET_RESET_VEC:
      mcall_set_reset_vec(arg0);
      retval = 0;
      break;
    case SBI_SET_PMA:
      mcall_set_pma(arg0, arg1, arg2);
      retval = 0;
      break;
    case SBI_FREE_PMA:
      mcall_free_pma(arg0);
      retval = 0;
      break;
    case SBI_PROBE_PMA:
      /* PPMA bit [30] */
      retval = ((read_csr(mmsc_cfg) & 0x40000000) != 0);
      break;
    default:
      retval = -ENOSYS;
      break;
  }
  regs[10] = retval;
}

void redirect_trap(uintptr_t epc, uintptr_t mstatus, uintptr_t badaddr)
{
  write_csr(sbadaddr, badaddr);
  write_csr(sepc, epc);
  write_csr(scause, read_csr(mcause));
  write_csr(mepc, read_csr(stvec));

  uintptr_t new_mstatus = mstatus & ~(MSTATUS_SPP | MSTATUS_SPIE | MSTATUS_SIE);
  uintptr_t mpp_s = MSTATUS_MPP & (MSTATUS_MPP >> 1);
  new_mstatus |= (mstatus * (MSTATUS_SPIE / MSTATUS_SIE)) & MSTATUS_SPIE;
  new_mstatus |= (mstatus / (mpp_s / MSTATUS_SPP)) & MSTATUS_SPP;
  new_mstatus |= mpp_s;
  write_csr(mstatus, new_mstatus);

  extern void __redirect_trap();
  return __redirect_trap();
}

void pmp_trap(uintptr_t* regs, uintptr_t mcause, uintptr_t mepc)
{
  redirect_trap(mepc, read_csr(mstatus), read_csr(mbadaddr));
}

static void machine_page_fault(uintptr_t* regs, uintptr_t dummy, uintptr_t mepc)
{
  // MPRV=1 iff this trap occurred while emulating an instruction on behalf
  // of a lower privilege level. In that case, a2=epc and a3=mstatus.
  if (read_csr(mstatus) & MSTATUS_MPRV) {
    return redirect_trap(regs[12], regs[13], read_csr(mbadaddr));
  }
  bad_trap(regs, dummy, mepc);
}

void trap_from_machine_mode(uintptr_t* regs, uintptr_t dummy, uintptr_t mepc)
{
  uintptr_t mcause = read_csr(mcause);

  switch (mcause)
  {
    case CAUSE_LOAD_PAGE_FAULT:
    case CAUSE_STORE_PAGE_FAULT:
    case CAUSE_FETCH_ACCESS:
    case CAUSE_LOAD_ACCESS:
    case CAUSE_STORE_ACCESS:
      return machine_page_fault(regs, dummy, mepc);
    default:
      bad_trap(regs, dummy, mepc);
  }
}

void poweroff(uint16_t code)
{
  printm("Power off\r\n");
  finisher_exit(code);
  if (htif) {
    htif_poweroff();
  } else {
    send_ipi_many(0, IPI_HALT);
    while (1) { asm volatile ("wfi\n"); }
  }
}
