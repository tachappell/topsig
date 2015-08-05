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
  
  // Open results file for writing
  const char *topicoutput = Config("RESULTS-PATH");
  FILE *fo;
  if (topicoutput) {
    fo = fopen(topicoutput, "wb");
    if (!fo) {
      fprintf(stderr, "The results file \"%s\" could not be opened for writing.\n", topicoutput);
      exit(1);
    }
  } else {
    fo = stdout;
  }
  
  char *Q = GetMandatoryConfig("QUERY-TEXT", "Error: a text query must be provided through the -query-text (query) option.");
  
  Search *S = InitSearch();
  
  int topK = GetIntegerConfig("K", 10);
  int topKOutput = GetIntegerConfig("K-OUTPUT", topK);
  
  Results *R = SearchCollectionQuery(S, Q, topK);
  //PrintResults(R, topKOutput);
  OutputResults(fo, "0", 0, R);
  fclose(fo);
  FreeResults(R);
  
  FreeSearch(S);
}
