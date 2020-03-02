// See LICENSE for license details.

#ifndef _RISCV_PMA_H
#define _RISCV_PMA_H

#define PMA_MMSC_CFG (1 << 30)
#define PMA_NUM 16
#define PMA_NAPOT 0x3
#define PMA_NOCACHE_BUFFER (0x3 << 2)

unsigned long pma_used_table[PMA_NUM];

static inline void write_pmaaddr(int i, unsigned long val)
{
  if (i == 0)
        write_csr(pmaaddr0, val);
  else if (i == 1)
        write_csr(pmaaddr1, val);
  else if (i == 2)
        write_csr(pmaaddr2, val);
  else if (i == 3)
        write_csr(pmaaddr3, val);
  else if (i == 4)
        write_csr(pmaaddr4, val);
  else if (i == 5)
        write_csr(pmaaddr5, val);
  else if (i == 6)
        write_csr(pmaaddr6, val);
  else if (i == 7)
        write_csr(pmaaddr7, val);
  else if (i == 8)
        write_csr(pmaaddr8, val);
  else if (i == 9)
        write_csr(pmaaddr9, val);
  else if (i == 10)
        write_csr(pmaaddr10, val);
  else if (i == 11)
        write_csr(pmaaddr11, val);
  else if (i == 12)
        write_csr(pmaaddr12, val);
  else if (i == 13)
        write_csr(pmaaddr13, val);
  else if (i == 14)
        write_csr(pmaaddr14, val);
  else if (i == 15)
        write_csr(pmaaddr15, val);
}

static inline unsigned long read_pmacfg(int i)
{
  unsigned long val;

#if __riscv_xlen == 64
  if (i == 0)
        val = read_csr(0xBC0);
  else if (i == 1)
        val = read_csr(0xBC2);
#else
  if (i == 0)
        val = read_csr(0xBC0);
  else if (i == 1)
        val = read_csr(0xBC1);
  else if (i == 2)
        val = read_csr(0xBC2);
  else if (i == 3)
        val = read_csr(0xBC3);
#endif
  return val;
}

static inline void write_pmacfg(int i, unsigned long val)
{
#if __riscv_xlen == 64
  if (i == 0)
        write_csr(0xBC0, val);
  else if (i == 1)
        write_csr(0xBC2, val);
#else
  if (i == 0)
        write_csr(0xBC0, val);
  else if (i == 1)
        write_csr(0xBC1, val);
  else if (i == 2)
        write_csr(0xBC2, val);
  else if (i == 3)
        write_csr(0xBC3, val);
#endif
}


#endif
