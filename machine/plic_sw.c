#include <string.h>
#include "mtrap.h"
#include "plic_sw.h"

inline void plic_sw_claim(plic_sw_t *plic)
{
  plic->source_id = *(plic->claim);
}

inline void plic_sw_complete(plic_sw_t *plic)
{
  *(plic->claim) = plic->source_id;
}

inline uint32_t plic_sw_get_pending(plic_sw_t *plic, uint32_t who)
{
  return *(plic->pending) & (SW_HART_MASK >> who);
}

inline void plic_sw_pending(plic_sw_t *plic, int to)
{
  /* The pending array registers are w1s type.
   *
   * IPI pending array mapping as following:
   *
   * Pending array start address: base + 0x1000
   * -------------------------------------
   * | core 3 | core 2 | core 1 | core 0 |
   * -------------------------------------
   * Each core X can send IPI to another core by setting the correspending bit
   * in core X own region(see the below).
   *
   * In each core region:
   * -----------------------------------------------
   * | bit 7 | bit 6 | bit 5 | bit 4 | ... | bit 0 |
   * -----------------------------------------------
   * The bit 7 is used to send IPI to core 0
   * The bit 6 is used to send IPI to core 1
   * The bit 5 is used to send IPI to core 2
   * The bit 4 is used to send IPI to core 3
   */
  assert(to < MAX_HARTS);
  uint32_t core_offset = (SW_PENDING_PER_HART - 1) - to;
  uint32_t per_hart_offset = SW_PENDING_PER_HART * plic->hart_id;
  *(plic->pending) = 1 << core_offset << per_hart_offset;
}
