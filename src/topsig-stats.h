#ifndef TOPSIG_STATS_H
#define TOPSIG_STATS_H

void Stats_Initcfg();

extern int totalTerms;
int TermFrequencyStats(const char *);
int TermFrequencyDF(const char *);

void AddTermStat(const char *, int);
void WriteStats();

#endif
