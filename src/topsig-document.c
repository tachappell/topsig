#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "topsig-global.h"
#include "topsig-document.h"

Document *NewDocument(const char *docId, const char *data)
{
  Document *newDoc = malloc(sizeof(Document));

  newDoc->docId = NULL;
  newDoc->data = NULL;
  newDoc->dataLength = 0;
  newDoc->stats.totalTerms = 0;
  newDoc->stats.uniqueTerms = 0;

  if (docId) {
    int docIdLength = strlen(docId);
    newDoc->docId = malloc(docIdLength + 1);
    strcpy(newDoc->docId, docId);
  }
  if (data) {
    int data_len = strlen(data);
    newDoc->data = malloc(data_len + 1);
    newDoc->dataLength = data_len;
  }

  return newDoc;
}

void FreeDocument(Document *doc)
{
  if (doc->docId) free(doc->docId);
  if (doc->data) free(doc->data);
  free(doc);
}

int DocumentQuality(const Document *doc)
{
  return doc->dataLength;
}
