#ifndef TOPSIG_TIMER_H
#define TOPSIG_TIMER_H

#include <sys/time.h>

typedef struct {
  struct timeval startTime;
  struct timeval previousTime;
} Timer;

Timer StartTimer();
double TickTimer(Timer *t);
double GetTotalTime(Timer *t);

#endif /* TOPSIG_TIMER_H */
