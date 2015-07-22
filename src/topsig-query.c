#include <stdio.h>
#include <stdlib.h>
#include "topsig-config.h"
#include "topsig-query.h"
#include "topsig-search.h"

void RunQuery()
{
  char *Q = Config("QUERY-TEXT");
  
  Search *S = InitSearch();
  
  int topK = GetIntegerConfig("K", 10);
  int topKOutput = GetIntegerConfig("K-OUTPUT", topK);
  
  for (int i = 0; i < 10; i++) {
    Results *R = SearchCollectionQuery(S, Q, topK);
    PrintResults(R, topKOutput);
    FreeResults(R);
  }
  
  FreeSearch(S);
}
