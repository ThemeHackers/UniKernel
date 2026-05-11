#ifndef SHELL_H
#define SHELL_H

#include "common.h"

#define MAX_ALIAS 6
#define MAX_ENV 8
#define ENV_KEY_LEN 12
#define ENV_VAL_LEN 24

typedef struct {
  char name[NAME_LEN];
  char cmd[32];
  bool active;
} Alias;

typedef struct {
  char key[ENV_KEY_LEN];
  char val[ENV_VAL_LEN];
  bool active;
} EnvVar;

extern Alias aliasTable[MAX_ALIAS];
extern EnvVar envTable[MAX_ENV];

void initShell();
bool resolveAlias(char *line, char *resolved);
void setEnv(const char *key, const char *val);
const char *getEnv(const char *key);

#endif
