#ifndef TOPSIG_ATOMIC_H
#define TOPSIG_ATOMIC_H

// Atomic operations for threading sync
// These should work under gcc and icc at least, new things can be defined
// in for other platforms as necessary


static inline int atomicFetchAndAdd(volatile int *i, int j) {
  return __sync_fetch_and_add(i,j);
}
static inline int atomicFetchAndSub(volatile int *i, int j) {
  return __sync_fetch_and_sub(i,j);
}

static inline int atomicCompareAndSwap(volatile int *i, int before, int after) {
  return __sync_bool_compare_and_swap(i, before, after);
}

static inline int atomicFetch(volatile int *i) {
  // Horrible magic number used so the swap rarely happens. This routine is not required on x86 / AMD64, but exists as a defensive mechanism just in case this code is used on a platform that requires a synced fetch
  return __sync_bool_compare_and_swap(i, 123897213, 123897213);
}

#endif
