#ifndef TOPSIG_TIMER_H
#define TOPSIG_TIMER_H

#include <sys/time.h>

typedef struct {
  struct timeval start_time;
  struct timeval last_time;
} timer;

timer timer_start();
double timer_tick(timer *t);
double get_total_time(timer *t);

#endif /* TOPSIG_TIMER_H */
