#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "uthash.h"
#include "topsig-config.h"
#include "topsig-global.h"

#include "topsig-index.h"
#include "topsig-process.h"
#include "topsig-stop.h"
#include "topsig-stem.h"
#include "topsig-stats.h"
#include "topsig-signature.h"
#include "topsig-progress.h"

typedef struct {
  char var[128];
  char newVar[128];
  char comment[16384];

  UT_hash_handle hh;
} DeprecatedConfigOption;

DeprecatedConfigOption *deprecatedConfigs = NULL;

static void addDeprecatedConfig(char *var, char *newVar, char *comment)
{
  DeprecatedConfigOption *deprecated = malloc(sizeof(DeprecatedConfigOption));
  
  strcpy(deprecated->var, var);
  strcpy(deprecated->newVar, newVar);
  strcpy(deprecated->comment, comment);
  
  HASH_ADD_STR(deprecatedConfigs, var, deprecated);
}

void InitConfigDeprecated()
{
  if (deprecatedConfigs) return;
  
  addDeprecatedConfig("dinesha-termweights", "termweight-suffixes", "");
  addDeprecatedConfig("duplicates_ok", "allow-duplicates", "");
  addDeprecatedConfig("isl-max-dist", "issl-expansion", "");
  addDeprecatedConfig("isl-max-dist-nonew", "issl-consideration-radius", "");
  addDeprecatedConfig("isl_slicewidth", "issl-slicewidth", "");
  addDeprecatedConfig("isl-path", "issl-path", "");
  addDeprecatedConfig("query-top-k", "k", "");
  addDeprecatedConfig("query-top-k-output", "k-output", "");
  addDeprecatedConfig("search-doc-jobs", "jobs", "");
  addDeprecatedConfig("search-doc-threads", "threads", "");
  addDeprecatedConfig("search-jobs", "jobs", "");
  addDeprecatedConfig("search-threads", "threads", "");
  addDeprecatedConfig("index-threads", "threads", "");
  addDeprecatedConfig("termstats-path-output", "termstats-path", "");
  addDeprecatedConfig("topic-output-k", "k", "");
  addDeprecatedConfig("search-doc-topk", "k-output", "");
  addDeprecatedConfig("search-doc-rerank", "k", "");
  
  addDeprecatedConfig("search-threading", "", "This option is no longer required as a THREADS value greater than 1 is sufficient to enable multithreading.");
  addDeprecatedConfig("index-threading", "", "This option is no longer required as a THREADS value greater than 1 is sufficient to enable multithreading.");
}

typedef struct {
  char var[128];
  char val[1024];

  UT_hash_handle hh;
} ConfigOption;

ConfigOption *configMap = NULL;

char *Config(const char *var)
{
  ConfigOption *cfg = NULL;
  char lcvar[128];
  strcpy(lcvar, var);
  strToLower(lcvar);

  HASH_FIND_STR(configMap, lcvar, cfg);
  if (cfg) {
    return cfg->val;
  } else {
    return NULL;
  }
}

static void addConfigParam(const char *var, const char *val)
{
  // Check to see if the passed configuration option is in the deprecated list
  DeprecatedConfigOption *deprecated;
    
  HASH_FIND_STR(deprecatedConfigs, var, deprecated);
  
  if (deprecated) {
    fprintf(stderr, "The -%s configuration option has been deprecated.\n", var);
    if (deprecated->newVar[0]) {
      fprintf(stderr, "Please use the -%s configuration option instead.\n", deprecated->newVar);
      addConfigParam(deprecated->newVar, val);
    } else {
      fprintf(stderr, "%s\n", deprecated->comment);
    }
    return;
  }
  
  
  // Special case: if 'var' is config, load a new config file
  if (strcmp(var, "config")==0) {
    ConfigFromFile(val);
    return;
  }

  ConfigOption *cfg;

  HASH_FIND_STR(configMap, var, cfg);
  if (cfg == NULL) {
    cfg = malloc(sizeof(ConfigOption));
    strcpy(cfg->var, var);
    HASH_ADD_STR(configMap, var, cfg);
  }
  strcpy(cfg->val, val);
}

void OverrideConfigParam(const char *var, const char *val)
{
  char lcvar[128];
  strcpy(lcvar, var);
  strToLower(lcvar);

  addConfigParam(lcvar, val);
}

void ConfigFromFile(const char *configFile)
{
  FILE *fp = fopen(configFile, "r");
  if (fp == NULL) {
    fprintf(stderr, "Config file %s not found\n", configFile);
    return;
  }
  char linebuf[16384];
  int linenum = 0;

  // Read the config file, one line at a time.

  while (fgets(linebuf, 16384, fp)) {
    linenum++;
    trim(linebuf);

    // If this line is either blank or a comment, ignore this
    if ((linebuf[0] == '\0') || (linebuf[0] == '#')) continue;

    // Locate the = that divides the assignment
    char *eqpos = strchr(linebuf, '=');

    if (eqpos != NULL) {
      char cfg_varname[1024];
      char cfg_value[8192];

      *eqpos = '\0';

      strcpy(cfg_varname, linebuf);
      trim(cfg_varname);
      strToLower(cfg_varname);

      strcpy(cfg_value, eqpos + 1);
      trim(cfg_value);

      addConfigParam(cfg_varname, cfg_value);
    } else {
      fprintf(stderr, "Error in config file %s line %d:\n   = not found.\n", configFile, linenum);
      exit(1);
    }
  }

  fclose(fp);
}

void ConfigCLI(int argc, const char **argv)
{
  // If a cmdline arg begins with -, it is a config variable
  // Otherwise, it is a value.
  char currentArgument[16384] = "";
  char currentValue[16384] = "";

  for (int i = 2; i < argc; i++) {
    if (argv[i][0] == '-') {
      // Config variable
      strcpy(currentArgument, argv[i]+1);
    } else {
      // Value
      if (currentArgument[0] != '\0') {
        strcpy(currentValue, argv[i]);

        trim(currentArgument);
        trim(currentValue);

        strToLower(currentArgument);

        addConfigParam(currentArgument, currentValue);

        currentValue[0] = '\0';
        currentArgument[0] = '\0';
      } else {
        fprintf(stderr, "Error in passed argument: %s\n", argv[i]);
        exit(1);
      }
    }
  }
}

void ConfigInit()
{
  InitProcessConfig();
  InitStoplistConfig();
  InitStemmingConfig();
  InitSignatureConfig();
  InitProgressConfig();
  InitIndexerConfig();
}

const char *GetOptionalConfig(const char *var, const char *def)
{
  const char *c = Config(var);
  if (!c) return def;
  return c;
}

const char *GetMandatoryConfig(const char *var, const char *err)
{
  const char *c = Config(var);
  if (!c) {
    fprintf(stderr, "%s\n", err);
    exit(1);
    return NULL;
  }
  return c;
}

int GetBooleanConfig(const char *var, int def)
{
  const char *c = Config(var);
  if (c == NULL) return def;
  if (strcmp_lc(c, "true")==0) return 1;
  if (strcmp_lc(c, "1")==0) return 1;
  if (strcmp_lc(c, "yes")==0) return 1;
  
  if (strcmp_lc(c, "false")==0) return 0;
  if (strcmp_lc(c, "0")==0) return 0;
  if (strcmp_lc(c, "no")==0) return 0;
  
  fprintf(stderr, "Error: configuration variable %s only accepts boolean values (\"true\" and \"false\")\n", var);
  exit(1);
  return def;
}


int GetIntegerConfig(const char *var, int def)
{
  const char *c = Config(var);
  if (c == NULL) return def;
  char *endPtr;
  int v = strtol(c, &endPtr, 0);
  
  if (*endPtr != '\0') {
    fprintf(stderr, "Error: configuration variable %s only accepts integer values\n", var);
    exit(1);
  }
  return v;
}
