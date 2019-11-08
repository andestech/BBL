// See LICENSE for license details.

#ifndef _RISCV_SMU_H
#define _RISCV_SMU_H

#define SMU_BASE		0xf0100000
#define PCS0_STATUS_OFF		0x98
#define CN_PCS_STATUS_OFF(n)	(n + 3) * 0x20 + PCS0_STATUS_OFF
#define PD_TYPE_MASK		0x7
#define PD_STATUS_MASK		0xf8
#define GET_PD_TYPE(val)	val & PD_TYPE_MASK
#define GET_PD_STATUS(val)	(val & PD_STATUS_MASK) >> 3
// PD_type
#define SLEEP   2
// PD_status for sleep type
#define DeepSleep_STATUS	16

#endif
