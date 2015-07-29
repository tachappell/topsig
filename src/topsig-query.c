#include <stdio.h>
#include <stdlib.h>
#include "topsig-config.h"
#include "topsig-query.h"
#include "topsig-search.h"
#include "topsig-stats.h"

void RunQuery()
{
  // Initialise term statistics (if relevant)
  Stats_Initcfg();
  
  char *Q = Config("QUERY-TEXT");
  
  Search *S = InitSearch();
  
  int topK = GetIntegerConfig("K", 10);
  int topKOutput = GetIntegerConfig("K-OUTPUT", topK);
  
  Results *R = SearchCollectionQuery(S, Q, topK);
  PrintResults(R, topKOutput);
  FreeResults(R);
  
  FreeSearch(S);
}
