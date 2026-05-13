#ifndef COMMON_H
#define COMMON_H

#include <Arduino.h>
#include <ArduinoJson.h>

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
#define MAX_FILES 16
#define CONTENT_LEN 128
#define DMESG_LINES 6
#define MAX_INPUT_LEN 256
#define MAX_TASKS 6
#else
#define MAX_FILES 4
#define CONTENT_LEN 64
#define DMESG_LINES 4
#define MAX_INPUT_LEN 64
#define MAX_TASKS 2
#endif

#define NAME_LEN 12
#define PATH_LEN 16
#define DMESG_LEN 32
#define FLAG_ACTIVE 0x01
#define FLAG_ISDIR 0x02
#define XOR_KEY 0x5A
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
#define BOARD_NAME "esp8266"
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#define BOARD_NAME "esp32"
#elif defined(ARDUINO_ARCH_AVR)
#define BOARD_NAME "uno"
#else
#define BOARD_NAME "arduino"
#endif

extern bool useColor;

#define EEPROM_PASS_ADDR 512
#define EEPROM_LOCKOUT_ADDR 530
#define EEPROM_SALT_ADDR 538
#define EEPROM_OTA_PASS_ADDR 550
#define EEPROM_FAIL_COUNT_ADDR 566
#define EEPROM_BOOT_FILE_ADDR 580
#define EEPROM_VFS_ADDR 1024
#define VFS_MAGIC 0x55AA

#define PASS_SALT_LEN 16

extern char currentPath[PATH_LEN];
extern bool serialAuthenticated;
extern bool telnetAuthenticated;
extern uint8_t loginFailCount;
extern bool isLockedOut;
extern bool isSerialSession;
extern char whitelistIP[16];
extern unsigned long lastLoginAttempt;
extern unsigned long loginCooldown;
extern bool needsSetup;

typedef struct {
    uint32_t timestamp;
    char message[DMESG_LEN];
} DmesgEntry;

#define MAX_TRIGS 4
typedef struct {
    int16_t val;
    char cond[16];
    char action[32];
    char op;
    bool active;
} Trigger;

#define MAX_CRON 4
typedef struct {
    char cmd[32];
    uint8_t h;
    uint8_t m;
    bool active;
} CronEntry;

typedef struct {
    void (*func)(void);
    uint32_t interval;
    uint32_t lastRun;
    uint32_t executionCount;
    char name[NAME_LEN];
    bool active;
} Task;

extern DmesgEntry dmesg[DMESG_LINES];
extern Trigger triggerTable[MAX_TRIGS];
extern CronEntry cronTable[MAX_CRON];
extern Task taskTable[MAX_TASKS];
extern int dmesgIndex;

typedef void (*CommandHandler)(char *args, bool fromSerial);

typedef struct CommandDef {
    const char *name;
    CommandHandler handler;
    bool authRequired;
    const char *help;

    CommandDef(const char *n, CommandHandler h, bool a, const char *hlp)
        : name(n), handler(h), authRequired(a), help(hlp) {}
} CommandDef;

void kprint(char c);
void kprint(const char *s);
void kprint(String s);
void kprint(const __FlashStringHelper *s);
void kprint(int n);
void kprint(unsigned int n);
void kprint(long n);
void kprint(unsigned long n);
void kprintln(char c);
void kprintln(const char *s);
void kprintln(String s);
void kprintln(const __FlashStringHelper *s);
void kprintln(int n);
void kprintln(unsigned int n);
void kprintln(long n);
void kprintln(unsigned long n);
void kprintln();
void kprintColor(const char *code);

void sendResponse(bool ok, int code, const char *message, JsonDocument *data = nullptr);

int kParseArgs(char *line, char **argv, int maxArgs);
char *kTrim(char *s);
void stripQuotes(char *s);
void toLowercase(char *s);
void safeStrncpy(char *dest, const char *src, size_t n);
void safeStrncat(char *dest, const char *src, size_t n);
int atoi_safe(const char *s);
int indexOf(const char *s, const char *target);
void addDmesg(const __FlashStringHelper *msg);
void addDmesg(const char *msg);
void safeConcatPath(char *base, const char *extra);

extern int shellDepth;
extern const char CLR_RED[];
extern const char CLR_GRN[];
extern const char CLR_YLW[];
extern const char CLR_BLU[];
extern const char CLR_MAG[];
extern const char CLR_CYN[];
extern const char CLR_WHT[];
extern const char CLR_RST[];

void printPermissions(uint16_t m, bool isDir);
int freeMemory();
void emergencyMemoryCleanup();
void optimizeMemoryUsage();
void preventiveMemoryCleanup();
void taskMemoryMonitor();

#endif
