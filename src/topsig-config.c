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

void ConfigUpdate()
{
  Process_InitCfg();
  Stop_InitCfg();
  Stem_InitCfg();
  Signature_InitCfg();
  Progress_InitCfg();
  Index_InitCfg();
}

typedef struct {
  char var[128];
  char val[1024];
  
  UT_hash_handle hh;
} ConfigOption;

ConfigOption *configmap = NULL;

char *Config(const char *var)
{
  ConfigOption *cfg = NULL;
  char lcvar[128];
  strcpy(lcvar, var);
  strtolower(lcvar);

  HASH_FIND_STR(configmap, lcvar, cfg);
  if (cfg) {
    return cfg->val;
  } else {
    return NULL;
  }
}

static void configadd(const char *var, const char *val)
{
  // Special case: if 'var' is config, load a new config file
  if (strcmp(var, "config")==0) {
    ConfigFile(val);
    return;
  }
    
  ConfigOption *cfg;
  
  HASH_FIND_STR(configmap, var, cfg);
  if (cfg == NULL) {
    cfg = malloc(sizeof(ConfigOption));
    strcpy(cfg->var, var);
    HASH_ADD_STR(configmap, var, cfg);
  }
  strcpy(cfg->val, val);
}

void ConfigOverride(const char *var, const char *val)
{
  char lcvar[128];
  strcpy(lcvar, var);
  strtolower(lcvar);

  configadd(lcvar, val);
}

// Remove whitespace from the beginning and end of this string
char *trim(char *string)
{
  int s_whitespace = 0;
  int len = strlen(string);
  
  for (int i = 0; isspace(string[i]); i++) {
    s_whitespace++;
  }
  
  memmove(string, string+s_whitespace, len+1-s_whitespace);
  
  len -= s_whitespace;
  
  for (int i = 0; isspace(string[len - 1 - i]); i++) {
    string[len - 1 - i] = '\0';
  }
  
  return string;
}

void ConfigFile(const char *configFile)
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
      strtolower(cfg_varname);
        
      strcpy(cfg_value, eqpos + 1);
      trim(cfg_value);
      
      configadd(cfg_varname, cfg_value);      
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
  char current_arg[16384] = "";
  char current_val[16384] = "";
  
  for (int i = 2; i < argc; i++) {
    if (argv[i][0] == '-') {
      // Config variable
      strcpy(current_arg, argv[i]+1);
    } else {
      // Value
      if (current_arg[0] != '\0') {
        strcpy(current_val, argv[i]);
        
        trim(current_arg);
        trim(current_val);
        
        strtolower(current_arg);
                
        configadd(current_arg, current_val);
        
        current_val[0] = '\0';
        current_arg[0] = '\0';
      } else {
        fprintf(stderr, "Error in passed argument: %s\n", argv[i]);
        exit(1);
      }
    }
  }
}
