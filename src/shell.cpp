#include "../include/shell.h"

Alias aliasTable[MAX_ALIAS];
EnvVar envTable[MAX_ENV];

void initShell() {
    memset(aliasTable, 0, sizeof(aliasTable));
    memset(envTable, 0, sizeof(envTable));
}

bool resolveAlias(char *line, char *resolved) {
    for (int i = 0; i < MAX_ALIAS; i++) {
        if (aliasTable[i].active && strncmp(line, aliasTable[i].name, strlen(aliasTable[i].name)) == 0) {
            char *space = strchr(line, ' ');
            if (space) {
                snprintf(resolved, MAX_INPUT_LEN, "%s %s", aliasTable[i].cmd, space + 1);
            } else {
                strncpy(resolved, aliasTable[i].cmd, MAX_INPUT_LEN - 1);
            }
            return true;
        }
    }
    return false;
}

void setEnv(const char *key, const char *val) {
    for (int i = 0; i < MAX_ENV; i++) {
        if (envTable[i].active && strcmp(envTable[i].key, key) == 0) {
            strncpy(envTable[i].val, val, ENV_VAL_LEN - 1);
            return;
        }
    }
    for (int i = 0; i < MAX_ENV; i++) {
        if (!envTable[i].active) {
            strncpy(envTable[i].key, key, ENV_KEY_LEN - 1);
            strncpy(envTable[i].val, val, ENV_VAL_LEN - 1);
            envTable[i].active = true;
            return;
        }
    }
}

const char *getEnv(const char *key) {
    for (int i = 0; i < MAX_ENV; i++) {
        if (envTable[i].active && strcmp(envTable[i].key, key) == 0) {
            return envTable[i].val;
        }
    }
    return NULL;
}