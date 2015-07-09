#ifndef TOPSIG_SIGNATUREWRITE_H
#define TOPSIG_SIGNATUREWRITE_H

#include "topsig-document.h"

extern volatile int cache_readers;

struct Signature;
typedef struct Signature Signature;

struct SignatureCache;
typedef struct SignatureCache SignatureCache;


void Signature_InitCfg();

Signature *NewSignature(const char *docid);
void SignatureFillDoubles(Signature *, double *);
void SignatureDestroy(Signature *sig);
void SignatureAdd(SignatureCache *, Signature *, const char *term, int count, int total_count, int dinesha);
void SignatureAddWeighted(SignatureCache *, Signature *, const char *term, int count, int total_count, double weight);
void SignatureAddOffset(SignatureCache *, Signature *, const char *term, int count, int total_count, int offset_begin, int offset_end, int dinesha);
void SignatureSetValues(Signature *sig, Document *doc);
void SignatureWrite(SignatureCache *, Signature *, const char *docid);
void SignatureFlush();
void SignaturePrint(Signature *);
void FlattenSignature(Signature *, void *, void *);

SignatureCache *NewSignatureCache(int iswriter, int iscached);
void DestroySignatureCache(SignatureCache *);

#endif
