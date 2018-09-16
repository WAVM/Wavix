#ifndef __NR_restart_syscall
#include <syscall.h>
#endif

#include <stdlib.h>

#define a_barrier() syscall(__NR_membarrier)

#define a_cas a_cas
static inline int a_cas(volatile int *p, int t, int s)
{
  int old = *p;
  if (old == t)
    *p = s;
  return old;
}

#define a_crash() __builtin_trap()

#define a_ctz_32(x) __builtin_ctzl(x)
#define a_ctz_64(x) __builtin_ctzll(x)
#define a_clz_32(x) __builtin_clzl(x)
#define a_clz_64(x) __builtin_clzll(x)
