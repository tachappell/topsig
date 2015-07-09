#ifndef TOPSIG_CONFIG_H
#define TOPSIG_CONFIG_H

void ConfigFile(const char *configFile);
void ConfigCLI(int argc, const char **argv);

char *Config(const char *var);

void ConfigOverride(const char *var, const char *val);

void ConfigUpdate();

char *trim(char *string);

#endif
