#ifndef TOPSIG_STATS_H
#define TOPSIG_STATS_H

void Stats_InitCfg();

extern int total_terms;
int TermFrequencyStats(const char *);
int TermFrequencyDF(const char *);

void AddTermStat(const char *, int);
void WriteStats();

#endif
