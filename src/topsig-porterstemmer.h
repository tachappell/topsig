#ifndef TOPSIG_PORTERSTEMMER_H
#define TOPSIG_PORTERSTEMMER_H

struct stemmer;

extern struct stemmer * create_stemmer(void);
extern void free_stemmer(struct stemmer * z);

extern int stem_ts(struct stemmer * z, char * b, int k);
extern int stem_ts2(char * b, int k);

#endif
