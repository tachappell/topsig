#include <stdio.h>
#include <stdlib.h>
#include "topsig-config.h"
#include "topsig-query.h"
#include "topsig-search.h"

void RunQuery()
{
  char *Q = Config("QUERY-TEXT");
  
  Search *S = InitSearch();
  
  for (int i = 0; i < 10; i++) {
    Results *R = SearchCollectionQuery(S, Q, atoi(Config("QUERY-TOP-K")));
    PrintResults(R, atoi(Config("QUERY-TOP-K-OUTPUT")));
    FreeResults(R);
  }
  
  FreeSearch(S);
}
