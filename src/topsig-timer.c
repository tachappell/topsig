#include <sys/time.h>
#include <stdlib.h>
#include "topsig-timer.h"


Timer StartTimer() {
  Timer t;
  gettimeofday(&t.startTime, NULL);
  t.previousTime = t.startTime;
  return t;
}
double TickTimer(Timer *t) {
  struct timeval currTime;
  gettimeofday(&currTime, NULL);

  double last_elapsed_ms = (currTime.tv_sec - t->previousTime.tv_sec) * 1000.0 + (currTime.tv_usec - t->previousTime.tv_usec) / 1000.0;
  t->previousTime = currTime;

  return last_elapsed_ms;
}
double GetTotalTime(Timer *t) {
  double total_elapsed_ms = (t->previousTime.tv_sec - t->startTime.tv_sec) * 1000.0 + (t->previousTime.tv_usec - t->startTime.tv_usec) / 1000.0;

  return total_elapsed_ms;
}
