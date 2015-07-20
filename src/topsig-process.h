#ifndef TOPSIG_PROCESS_H
#define TOPSIG_PROCESS_H

#include "topsig-signature.h"
#include "topsig-document.h"

void InitProcessConfig();
void ProcessFile(SignatureCache *, Document *);

#endif
