#include "trigger.h"
#include "encoding.h"
#include "mtrap.h"

static struct trigger_module trigger_modules[TRIGGER_MAX];
static int total_triggers;

void trigger_init(void)
{
  uintptr_t tselect, tinfo;
  int i;

  for (i = 0; i < TRIGGER_MAX; i++) {
    write_csr(tselect, i);
    tselect = read_csr(tselect);
    if (i != tselect)
      break;

    tinfo = read_csr(tinfo);
    if (tinfo == 1)
      break;
  }

  total_triggers = i;
}

static int trigger_get_free(void)
{
  int i;
  for (i = 0; i < total_triggers; i++) {
    if (!trigger_modules[i].used)
      break;
  }
  return i;
}

static int trigger_get_by_type(int type)
{
  int i;
  for (i = 0; i < total_triggers; i++) {
    if (trigger_modules[i].type == type && trigger_modules[i].used)
      break;
  }

  if (i == total_triggers)
    return trigger_get_free();

  return i;
}

static int trigger_set_tselect(int num)
{
  if (!TRIGGER_SUPPORT(num)) {
    printm("machine mode: trigger %d is not supported.\n", num);
    return -1;
  }
  write_csr(tselect, num);
  return 0;
}

static int trigger_set_tdata1(uintptr_t val)
{
  uintptr_t ret;

  write_csr(tdata1, val);
  ret = read_csr(tdata1);

  if (ret != val)
    return -1;

  return 0;
}

static int trigger_set_tdata2(uintptr_t val)
{
  uintptr_t ret;

  write_csr(tdata2, val);
  ret = read_csr(tdata2);

  if (ret != val)
    return -1;

  return 0;
}

int trigger_set_icount(uintptr_t count, unsigned int m,
                       unsigned int s, unsigned int u)
{
  uintptr_t val;
  int num, err;

  num = trigger_get_by_type(TRIGGER_TYPE_ICOUNT);
  if (num == total_triggers) {
    return -1;
  }

  err = trigger_set_tselect(num);
  if (err)
    return -1;

  if (!TRIGGER_SUPPORT_TYPE(num, TRIGGER_TYPE_ICOUNT)) {
    printm("machine mode: trigger %d is not support %d type.\n",
            num, TRIGGER_TYPE_ICOUNT);
    return -1;
  }

  val = (TRIGGER_TYPE_ICOUNT << TDATA1_OFFSET_TYPE) |
        (count << ICOUNT_OFFSET_COUNT) |
        (m << ICOUNT_OFFSET_M) |
        (s << ICOUNT_OFFSET_S) |
        (u << ICOUNT_OFFSET_U);

  err = trigger_set_tdata1(val);
  if (err)
    return -1;

  trigger_modules[num].used = 1;
  trigger_modules[num].type = TRIGGER_TYPE_ICOUNT;

  return err;
}

int trigger_set_itrigger(uintptr_t interrupt, unsigned int m,
                         unsigned int s, unsigned int u)
{
  uintptr_t val;
  int num, err;

  num = trigger_get_by_type(TRIGGER_TYPE_ITRIGGER);
  if (num == total_triggers)
    return -1;

  err = trigger_set_tselect(num);
  if (err)
    return -1;

  if (!TRIGGER_SUPPORT_TYPE(num, TRIGGER_TYPE_ITRIGGER)) {
    printm("machine mode: trigger %d is not support %d type.\n",
            num, TRIGGER_TYPE_ITRIGGER);
    return -1;
  }

  val = (uintptr_t)((TRIGGER_TYPE_ITRIGGER << TDATA1_OFFSET_TYPE) |
        (m << ITRIGGER_OFFSET_M) |
        (s << ITRIGGER_OFFSET_S) |
        (u << ITRIGGER_OFFSET_U));
  err = trigger_set_tdata1(val);
  if (err)
    return -1;

  err = trigger_set_tdata2(interrupt);
  if (err)
    return -1;

  trigger_modules[num].used = 1;
  trigger_modules[num].type = TRIGGER_TYPE_ITRIGGER;

  return err;
}

int trigger_set_etrigger(uintptr_t exception, unsigned int m,
                         unsigned int s, unsigned int u)
{
  uintptr_t val;
  int num, err;

  num = trigger_get_by_type(TRIGGER_TYPE_ETRIGGER);
  if (num == total_triggers)
    return -1;

  err = trigger_set_tselect(num);
  if (err)
    return -1;

  if (!TRIGGER_SUPPORT_TYPE(num, TRIGGER_TYPE_ETRIGGER)) {
    printm("machine mode: trigger %d is not support %d type.\n",
            num, TRIGGER_TYPE_ETRIGGER);
    return -1;
  }

  val = (TRIGGER_TYPE_ETRIGGER << TDATA1_OFFSET_TYPE) |
        (m << ETRIGGER_OFFSET_M) |
        (s << ETRIGGER_OFFSET_S) |
        (u << ETRIGGER_OFFSET_U);
  err = trigger_set_tdata1(val);
  if (err)
    return -1;

  err = trigger_set_tdata2(exception);
  if (err)
    return -1;

  trigger_modules[num].used = 1;
  trigger_modules[num].type = TRIGGER_TYPE_ETRIGGER;

  return err;
}
