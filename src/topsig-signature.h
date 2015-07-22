#ifndef TOPSIG_SIGNATUREWRITE_H
#define TOPSIG_SIGNATUREWRITE_H

#include "topsig-document.h"

struct Signature;
typedef struct Signature Signature;

struct SignatureCache;
typedef struct SignatureCache SignatureCache;

void InitSignatureConfig();

Signature *NewSignature(const char *docId);
void SignatureFillDoubles(Signature *, double *);
void SignatureDestroy(Signature *sig);
void SignatureAdd(SignatureCache *, Signature *, const char *term, int count, int totalCount, int termWeightSuffixes);
void SignatureAddWeighted(SignatureCache *, Signature *, const char *term, int count, int totalCount, double weight);
void SignatureAddOffset(SignatureCache *, Signature *, const char *term, int count, int totalCount, int offsetBegin, int offsetEnd, int termWeightSuffixes);
void SignatureSetValues(Signature *sig, Document *doc);
void SignatureWrite(SignatureCache *, Signature *);
void SignatureFlush();
void SignaturePrint(Signature *);
void FlattenSignature(Signature *, void *, void *);

SignatureCache *NewSignatureCache(int iswriter, int iscached);
void DestroySignatureCache(SignatureCache *);

#endif
