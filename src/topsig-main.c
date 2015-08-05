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

typedef struct {
  const char *mode;
  void (*function)(void);
  const char *desc;
} ExecutionMode;

static const ExecutionMode executionModes[] = {
  {"index", RunIndex, "Create a signature file from a document collection"},
  {"query", RunQuery, "Search a signature file with a text query"},
  {"topic", RunTopic, "Search a signature file with a topic file"},
  {"termstats", RunTermStats, "Create a database of global term statistics"},
  {"create-issl", CreateISSLTable, "Create an ISSL table from a signature file"},
  {"search-issl", SearchISSLTable, "Search an ISSL table for pairwise-similar signatures"},
  
  // These are old modes, maintained for the sake of backwards compatibility but not documented
  {"histogram", RunHistogram, NULL},
  {"experimental-rf", RunExperimentalRF, NULL},
  {"createisl", CreateISSLTable, NULL},
  {"docsim", SearchISSLTable, NULL},
  {"exhaustive-docsim", RunExhaustiveDocsimSearch, NULL},
};

static const int executionModeCount = sizeof(executionModes) / sizeof(executionModes[0]);

static void runExecutionMode(const char *mode)
{
  for (int i = 0; i < executionModeCount; i++) {
    if (strcmp(mode, executionModes[i].mode)==0) {
      executionModes[i].function();
      return;
    }
  }
  
  usage();
}

int main(int argc, const char **argv)
{
  if (argc == 1) {
    usage();
    return 0;
  }
  
  InitConfigDeprecated();
  ConfigFromFile("config.txt", 0);
  ConfigCLI(argc, argv);
  
  ConfigInit();
  
  runExecutionMode(argv[1]);
  
  return 0;
}

static void usage()
{
  fprintf(stderr, "Usage: ./topsig [mode] {options}\n");
  fprintf(stderr, "Valid options for [mode] are:\n");
  /*
  fprintf(stderr, "  index\n");
  fprintf(stderr, "  query\n");
  fprintf(stderr, "  topic\n");
  fprintf(stderr, "  termstats\n\n");
  */
  for (int i = 0; i < executionModeCount; i++) {
    if (executionModes[i].desc) {
      fprintf(stderr, "  %s: %s\n", executionModes[i].mode, executionModes[i].desc);
    }
  }
  
  fprintf(stderr, "Configuration information is by default read from\n");
  fprintf(stderr, "config.txt in the current working directory.\n");
  fprintf(stderr, "Additional configuration files can be added through\n");
  fprintf(stderr, "the -config [path] option.\n");
}
