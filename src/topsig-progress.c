#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "topsig-config.h"
#include "topsig-global.h"
#include "topsig-atomic.h"
#include "topsig-progress.h"
#include "topsig-semaphore.h"

static struct {
  enum {PROGRESS_NONE, PROGRESS_PERIODIC, PROGRESS_FULL} type;
  int period;
  int totaldocs;
} cfg;

TSemaphore sem_progress;
struct timeval startTime;

void InitProgressConfig()
{
  cfg.type = PROGRESS_NONE;
  cfg.period = 1000;
  cfg.totaldocs = 0;

  if (strcmp_lc(Config("OUTPUT-PROGRESS"),"none")==0) cfg.type = PROGRESS_NONE;
  if (strcmp_lc(Config("OUTPUT-PROGRESS"),"periodic")==0) cfg.type = PROGRESS_PERIODIC;
  if (strcmp_lc(Config("OUTPUT-PROGRESS"),"full")==0) cfg.type = PROGRESS_FULL;

  gettimeofday(&startTime, NULL);

  if (Config("OUTPUT-PERIOD")) cfg.period = atoi(Config("OUTPUT-PERIOD"));
  if (Config("OUTPUT-PROGRESS-DOCUMENTS")) cfg.totaldocs = atoi(Config("OUTPUT-PROGRESS-DOCUMENTS"));

  InitSemaphore(&sem_progress, 0, 1);

  if (cfg.totaldocs > 0) fprintf(stderr, "\n");
}

static int current_docs = 0;

void ProgressTick(const char *identifier)
{
  int c = atomicFetchAndAdd(&current_docs, 1) + 1;

  if (cfg.type == PROGRESS_NONE) return;
  if ((cfg.type == PROGRESS_PERIODIC) && (c % cfg.period != 0)) return;
  WaitSemaphore(&sem_progress);

  struct timeval curr_time;
  gettimeofday(&curr_time, NULL);
  double elapsed_secs = curr_time.tv_sec - startTime.tv_sec;
  elapsed_secs += (curr_time.tv_usec - startTime.tv_usec) / 1000000.0;

  char time_est[256] = "";

  int time_secs = (int)elapsed_secs;
  int time_ms = (int)((elapsed_secs - time_secs) * 1000.0);
  int time_mins = time_secs / 60;
  int dps = 0;
  dps = (double) c / elapsed_secs;
  time_secs = time_secs % 60;

  sprintf(time_est, "- %d:%02d.%03d (%d per sec)", time_mins, time_secs, time_ms, dps);

  if (cfg.totaldocs <= 0) {

    if (cfg.type == PROGRESS_FULL) printf("[%s]  ", identifier);
    printf("%d %s\n", c, time_est);

  } else {
    char meter[11] = "          ";
    int meter_prg = (c * 10 + (cfg.totaldocs / 2)) / cfg.totaldocs;
    if (meter_prg > 10) meter_prg = 10;
    for (int i = 0; i < meter_prg; i++) {
      meter[i] = '*';
    }
    fprintf(stderr, "\r[%s] %d/%d %s", meter, c, cfg.totaldocs, time_est);
  }

  PostSemaphore(&sem_progress);
}
