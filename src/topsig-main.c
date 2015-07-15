#include <stdio.h>
#include <string.h>
#include "topsig-config.h"
#include "topsig-index.h"
#include "topsig-query.h"
#include "topsig-topic.h"
#include "topsig-issl.h"
#include "topsig-stats.h"
#include "topsig-histogram.h"
#include "topsig-exhaustive-docsim.h"

#include "topsig-experimental-rf.h"

static void usage();

int main(int argc, const char **argv)
{
  if (argc == 1) {
    usage();
    return 0;
  }
  
  ConfigFile("config.txt");
  ConfigCLI(argc, argv);

  ConfigUpdate();
  
  if (strcmp(argv[1], "index")==0 ||
      strcmp(argv[1], "query")==0 ||
      strcmp(argv[1], "topic")==0 ||
      strcmp(argv[1], "experimental-rf")==0) Stats_InitCfg();

  if (strcmp(argv[1], "index")==0) RunIndex();
  else if (strcmp(argv[1], "query")==0) RunQuery();
  else if (strcmp(argv[1], "topic")==0) RunTopic();
  else if (strcmp(argv[1], "termstats")==0) RunTermStats();
  
  // Experimental modes are not listed in the usage() function
  else if (strcmp(argv[1], "experimental-rf")==0) RunExperimentalRF();
  else if (strcmp(argv[1], "createisl")==0) RunCreateISL();
  else if (strcmp(argv[1], "docsim")==0) RunSearchISLTurbo();
  else if (strcmp(argv[1], "exhaustive-docsim")==0) RunExhaustiveDocsimSearch();
  else if (strcmp(argv[1], "experimental-reranktop")==0) ExperimentalRerankTopFile();
  else if (strcmp(argv[1], "histogram")==0) RunHistogram();

  else usage();
  return 0;
}

static void usage()
{
  fprintf(stderr, "Usage: ./topsig [mode] {options}\n");
  fprintf(stderr, "Valid options for [mode] are:\n");
  fprintf(stderr, "  index\n");
  fprintf(stderr, "  query\n");
  fprintf(stderr, "  topic\n");
  fprintf(stderr, "  termstats\n\n");
  fprintf(stderr, "Configuration information is by default read from\n");
  fprintf(stderr, "config.txt in the current working directory.\n");
  fprintf(stderr, "Additional configuration files can be added through\n");
  fprintf(stderr, "the -config [path] option.\n");
}
