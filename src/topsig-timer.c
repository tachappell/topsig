#include <sys/time.h>
#include <stdlib.h>
#include "topsig-timer.h"


timer timer_start() {
  timer t;
  gettimeofday(&t.start_time, NULL);
  t.last_time = t.start_time;
  return t;
}
double timer_tick(timer *t) {
  struct timeval curr_time;
  gettimeofday(&curr_time, NULL);

  double last_elapsed_ms = (curr_time.tv_sec - t->last_time.tv_sec) * 1000.0 + (curr_time.tv_usec - t->last_time.tv_usec) / 1000.0;
  t->last_time = curr_time;

  return last_elapsed_ms;
}
double get_total_time(timer *t) {
  double total_elapsed_ms = (t->last_time.tv_sec - t->start_time.tv_sec) * 1000.0 + (t->last_time.tv_usec - t->start_time.tv_usec) / 1000.0;

  return total_elapsed_ms;
}
