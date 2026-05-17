#include "../include/vfs.h"

RAMFile vfs[MAX_FILES];
char currentPath[PATH_LEN] = "/";

void initFS() {
    memset(vfs, 0, sizeof(vfs));
    const char *sysDirs[] = {"home", "dev", "sys", "bin", "mnt/host"};
    int dirsCreated = 0;


    for (int d = 0; d < 4; d++) {
        bool found = false;
        for (int i = 0; i < MAX_FILES; i++) {
            if (!(vfs[i].flags & FLAG_ACTIVE)) {
                strncpy(vfs[i].name, sysDirs[d], NAME_LEN - 1);
                vfs[i].name[NAME_LEN - 1] = '\0';
                strncpy(vfs[i].parentDir, "/", PATH_LEN - 1);
                vfs[i].parentDir[PATH_LEN - 1] = '\0';
                vfs[i].flags = FLAG_ACTIVE | FLAG_ISDIR;
                vfs[i].mode = 0755;
                vfs[i].ownerId = 0;
                vfs[i].content[0] = '\0'; 
                dirsCreated++;
                found = true;
                break;
            }
        }
        if (!found) {
            addDmesg(F("Warning: Failed to create system directory"));
        }
    }


    bool errorLogCreated = false;
    for (int i = 0; i < MAX_FILES; i++) {
        if (!(vfs[i].flags & FLAG_ACTIVE)) {
            strcpy(vfs[i].name, "error.log");
            strcpy(vfs[i].parentDir, "/sys");
            vfs[i].flags = FLAG_ACTIVE;
            vfs[i].mode = 0644;
            vfs[i].ownerId = 0;
            strcpy(vfs[i].content, "--- UniKernel Error Log ---\n");
            errorLogCreated = true;
            break;
        }
    }


    int activeFiles = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (vfs[i].flags & FLAG_ACTIVE) {
            activeFiles++;
        }
    }

    addDmesg(F("Kernel initialized"));
    addDmesg(F("Filesystem mounted"));
    if (dirsCreated == 4) {
        addDmesg(F("System directories created"));
    } else {
        addDmesg(F("Warning: Some directories missing"));
    }
    if (errorLogCreated) {
        addDmesg(F("Error log initialized"));
    }
    addDmesg(F("Ready for commands"));
}

bool isValidFsName(const char *name) {
    if (name == NULL || name[0] == '\0') return false;
    size_t n = strlen(name);
    if (n == 0 || n >= NAME_LEN) return false;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        if (c == '/' || c == '\\' || c == ' ' || c < 33 || c > 126)
            return false;
    }
    return true;
}

int findFile(const char *name, const char *parentDir) {
    for (int i = 0; i < MAX_FILES; i++) {
        if ((vfs[i].flags & FLAG_ACTIVE) && strcmp(vfs[i].name, name) == 0 &&
            strcmp(vfs[i].parentDir, parentDir) == 0) {
            return i;
        }
    }
    return -1;
}