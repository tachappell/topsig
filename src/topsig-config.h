#ifndef TOPSIG_CONFIG_H
#define TOPSIG_CONFIG_H

void InitConfigDeprecated();
void ConfigFromFile(const char *configFile, int errIfNotPresent);
void ConfigCLI(int argc, const char **argv);

char *Config(const char *var);

void OverrideConfigParam(const char *var, const char *val);

void ConfigInit();

const char *GetOptionalConfig(const char *var, const char *def);
const char *GetMandatoryConfig(const char *var, const char *err);
int GetBooleanConfig(const char *var, int def);
int GetIntegerConfig(const char *var, int def);

void CheckConfigPresent(const char *var, const char *err);

#endif
