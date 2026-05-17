#ifndef VFS_H
#define VFS_H

#include "common.h"

typedef struct {
  char name[NAME_LEN];
  char content[CONTENT_LEN];
  char parentDir[PATH_LEN];
  uint16_t mode;
  uint8_t flags;
  uint8_t ownerId;
} RAMFile;

extern RAMFile vfs[MAX_FILES];

void initFS();
bool isValidFsName(const char *name);
bool checkPermission(int fileIdx, int mode, bool fromSerial);
int findFile(const char *name, const char *parentDir);

#endif