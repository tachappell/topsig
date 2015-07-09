#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "topsig-global.h"
#include "topsig-document.h"

typedef struct {
  int dummy;
} Document_private;

Document *NewDocument(const char *docid, const char *data)
{
  Document *newDoc = malloc(sizeof(Document));
  Document_private *p = malloc(sizeof(Document_private));
  
  newDoc->docid = NULL;
  newDoc->data = NULL;
  newDoc->data_length = 0;
  newDoc->stats.total_terms = 0;
  newDoc->stats.unique_terms = 0;
  newDoc->p = p;
  
  if (docid) {
    int docid_len = strlen(docid);
    newDoc->docid = malloc(docid_len + 1);
    strcpy(newDoc->docid, docid);
  }
  if (data) {
    int data_len = strlen(data);
    newDoc->data = malloc(data_len + 1);
    newDoc->data_length = data_len;
  }
  
  return newDoc;
}

void FreeDocument(Document *doc)
{
  free(doc->p);
  if (doc->docid) free(doc->docid);
  if (doc->data) free(doc->data);
  free(doc);
}

int DocumentQuality(const Document *doc)
{
  return doc->data_length;
}