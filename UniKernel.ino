#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <avr/pgmspace.h>
#include <string.h>

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h> 
#endif

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
#define MAX_FILES 32      
#define CONTENT_LEN 256    
#define DMESG_LINES 15     
#define MAX_INPUT_LEN 128 
#define MAX_TASKS 8       
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
#define MAX_ENV 8          
#define ENV_KEY_LEN 10
#define ENV_VAL_LEN 32
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
#define BOARD_NAME "esp8266"
#include <time.h> 
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#define BOARD_NAME "esp32"
#elif defined(ARDUINO_ARCH_AVR)
#define BOARD_NAME "uno"
#else
#define BOARD_NAME "arduino"
#endif

#define FLAG_ACTIVE 0x01
#define FLAG_ISDIR 0x02

typedef struct {
  char name[NAME_LEN];
  char content[CONTENT_LEN];
  char parentDir[PATH_LEN];
  uint8_t flags;
  uint16_t mode;
  uint8_t ownerId;
} RAMFile;

typedef struct {
  unsigned long timestamp;
  char message[DMESG_LEN];
} DmesgEntry;

typedef struct {
  void (*func)(void);
  unsigned long interval;
  unsigned long lastRun;
  bool active;
  char name[NAME_LEN];
} Task;

Task taskTable[MAX_TASKS];
typedef struct {
  char key[ENV_KEY_LEN];
  char val[ENV_VAL_LEN];
  bool active;
} EnvVar;
EnvVar envTable[MAX_ENV];

#define MAX_CRON 4
typedef struct {
  uint8_t h, m;
  char cmd[32];
  bool active;
} CronEntry;
CronEntry cronTable[MAX_CRON];

RAMFile vfs[MAX_FILES];
char currentPath[PATH_LEN] = "/";
char inputBuffer[MAX_INPUT_LEN] = ""; 
int inputLen = 0;
DmesgEntry dmesg[DMESG_LINES];
int dmesgIndex = 0;
int shellDepth = 0;
bool serialAuthenticated = false;
bool telnetAuthenticated = false;
uint8_t loginFailCount = 0;
bool isLockedOut = false;
bool needsSetup = false;
bool telnetEnabled = false;
bool webEnabled = false;
int redirectionFileIdx = -1;
unsigned long lastSerialActivity = 0;
unsigned long lastTelnetActivity = 0;
unsigned long lastLoginAttempt = 0;
unsigned long loginCooldown = 0;
#define MAX_SHELL_DEPTH 2
#define SESSION_TIMEOUT 300000
#define MAX_FAIL_COUNT 5
#define LOCKOUT_DURATION 300000

#define EEPROM_PASS_ADDR 512
#define EEPROM_LOCKOUT_ADDR 522
#define EEPROM_SALT_ADDR 530

#define PASS_SALT_LEN 4
#if !defined(ICACHE_FLASH_ATTR)
#define ICACHE_FLASH_ATTR
#endif

#define KERNEL_KEY 0x5A


ICACHE_FLASH_ATTR void executeCommand(char *line, bool fromSerial);
ICACHE_FLASH_ATTR void runScript(const char *content);
ICACHE_FLASH_ATTR void setupWebServer();

ICACHE_FLASH_ATTR bool checkWiFi() {
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
  if (WiFi.status() != WL_CONNECTED) {
    kprintln(F("Error: WiFi not connected."));
    kprintln(F("Please use: wifi connect [ssid] [pass]"));
    return false;
  }
#endif
  return true;
}

ICACHE_FLASH_ATTR void hashPass(const char *input, char *output) {
  uint32_t hash = 0;
  int i;
  char salt[PASS_SALT_LEN + 1];

  EEPROM.get(EEPROM_SALT_ADDR, salt);
  salt[PASS_SALT_LEN] = '\0';

  for (i = 0; i < PASS_SALT_LEN && salt[i] != '\0'; i++) {
    hash += salt[i];
    hash += (hash << 10);
    hash ^= (hash >> 6);
  }

  for (i = 0; input[i] != '\0' && i < 32; i++) {
    hash += input[i];
    hash += (hash << 10);
    hash ^= (hash >> 6);
  }

  for (i = 0; i < 8; i++) {
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
  }

  memset(output, 0, 10);
  for (i = 0; i < 9; i++) {

    output[i] = ((hash >> (i * 3)) & 0xFF) ^ KERNEL_KEY;
  }
  output[9] = '\0';
}

ICACHE_FLASH_ATTR bool secureEquals(const char *a, const char *b, size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) {
    diff |= ((uint8_t)a[i]) ^ ((uint8_t)b[i]);
  }
  return diff == 0;
}

ICACHE_FLASH_ATTR bool isValidFsName(const char *name) {
  if (name == NULL || name[0] == '\0')
    return false;
  size_t n = strlen(name);
  if (n == 0 || n >= NAME_LEN)
    return false;
  for (size_t i = 0; i < n; i++) {
    char c = name[i];
    if (c == '/' || c == '\\' || c == ' ' || c < 33 || c > 126)
      return false;
  }
  return true;
}

ICACHE_FLASH_ATTR bool isTimeout(unsigned long lastActivity,
                                 unsigned long timeout) {
  unsigned long currentTime = millis();
  return (currentTime - lastActivity) >= timeout;
}

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
WiFiServer telnetServer(23);
WiFiClient telnetClient;
ESP8266WebServer webServer(80);
#endif

ICACHE_FLASH_ATTR void kprint(const __FlashStringHelper *s) {
  if (redirectionFileIdx != -1) {
    strncat_P(vfs[redirectionFileIdx].content, (PGM_P)s, CONTENT_LEN - strlen(vfs[redirectionFileIdx].content) - 1);
    return;
  }
  Serial.print(s);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetClient && telnetClient.connected())
    telnetClient.print(s);
#endif
}
ICACHE_FLASH_ATTR void kprint(const char *s) {
  if (redirectionFileIdx != -1) {
    strncat(vfs[redirectionFileIdx].content, s, CONTENT_LEN - strlen(vfs[redirectionFileIdx].content) - 1);
    return;
  }
  Serial.print(s);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetClient && telnetClient.connected())
    telnetClient.print(s);
#endif
}
ICACHE_FLASH_ATTR void kprint(int n) {
  if (redirectionFileIdx != -1) {
    char buf[12]; itoa(n, buf, 10);
    strncat(vfs[redirectionFileIdx].content, buf, CONTENT_LEN - strlen(vfs[redirectionFileIdx].content) - 1);
    return;
  }
  Serial.print(n);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetClient && telnetClient.connected())
    telnetClient.print(n);
#endif
}
ICACHE_FLASH_ATTR void kprintln(const __FlashStringHelper *s) {
  kprint(s); kprintln();
}
ICACHE_FLASH_ATTR void kprintln(const char *s) {
  kprint(s); kprintln();
}
ICACHE_FLASH_ATTR void kprintln() {
  kprint(F("\r\n"));
}
ICACHE_FLASH_ATTR void kprintln(int n) {
  kprint(n); kprintln();
}
ICACHE_FLASH_ATTR void kprintln(unsigned long n) {
  if (redirectionFileIdx != -1) {
    char buf[16]; ltoa(n, buf, 10);
    strncat(vfs[redirectionFileIdx].content, buf, CONTENT_LEN - strlen(vfs[redirectionFileIdx].content) - 1);
    kprint(F("\r\n"));
    return;
  }
  Serial.println(n);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetClient && telnetClient.connected())
    telnetClient.println(n);
#endif
}
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
ICACHE_FLASH_ATTR void kprintln(IPAddress ip) {
  Serial.println(ip);
  if (telnetClient && telnetClient.connected())
    telnetClient.println(ip);
}
#endif
void kprint(String s) {
  Serial.print(s);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetClient && telnetClient.connected())
    telnetClient.print(s);
#endif
}
void kprintln(String s) {
  Serial.println(s);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetClient && telnetClient.connected())
    telnetClient.println(s);
#endif
}

ICACHE_FLASH_ATTR int freeMemory() {
#if defined(ARDUINO_ARCH_AVR)
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
#elif defined(ESP8266) || defined(ESP32)
  return ESP.getFreeHeap();
#else
  return 0;
#endif
}

#if defined(ARDUINO_ARCH_AVR)
void (*resetFunc)(void) = 0;
#endif

ICACHE_FLASH_ATTR void addDmesg(const __FlashStringHelper *msg) {
  if (dmesgIndex >= DMESG_LINES)
    dmesgIndex = 0;
  dmesg[dmesgIndex].timestamp = millis() / 1000;
  strncpy_P(dmesg[dmesgIndex].message, (PGM_P)msg, DMESG_LEN - 1);
  dmesg[dmesgIndex].message[DMESG_LEN - 1] = '\0';
  dmesgIndex++;
}

ICACHE_FLASH_ATTR void addDmesgRam(const char *msg) {
  if (dmesgIndex >= DMESG_LINES)
    dmesgIndex = 0;
  dmesg[dmesgIndex].timestamp = millis() / 1000;
  strncpy(dmesg[dmesgIndex].message, msg, DMESG_LEN - 1);
  dmesg[dmesgIndex].message[DMESG_LEN - 1] = '\0';
  dmesgIndex++;
}

ICACHE_FLASH_ATTR void initFS() {
  int d, i;

  memset(vfs, 0, sizeof(vfs));

#if defined(ARDUINO_ARCH_AVR)
  const char sysDirs_P[] PROGMEM = "home\0dev\0sys\0bin\0";
  const char *sysDirs = sysDirs_P;
#else
  const char *sysDirs[] = {"home", "dev", "sys", "bin"};
#endif

  for (d = 0; d < 4; d++) {
#if defined(ARDUINO_ARCH_AVR)
    const char *dirName = sysDirs;
    for (int skip = 0; skip < d; skip++) {
      while (pgm_read_byte(dirName) != '\0')
        dirName++;
      dirName++;
    }
#endif
    for (i = 0; i < MAX_FILES; i++) {
      if (!(vfs[i].flags & FLAG_ACTIVE)) {
#if defined(ARDUINO_ARCH_AVR)
        strncpy_P(vfs[i].name, dirName, NAME_LEN - 1);
#else
        strncpy(vfs[i].name, sysDirs[d], NAME_LEN - 1);
#endif
        vfs[i].name[NAME_LEN - 1] = '\0';
        strncpy(vfs[i].parentDir, "/", PATH_LEN - 1);
        vfs[i].flags = FLAG_ACTIVE | FLAG_ISDIR;
        vfs[i].mode = 0755;
        vfs[i].ownerId = 0;
        break;
      }
    }
  }

  addDmesg(F("Kernel initialized"));
  addDmesg(F("Filesystem mounted"));
  addDmesg(F("Ready for commands"));
}

ICACHE_FLASH_ATTR void printPrompt() {
  Serial.print(F("root@"));
  Serial.print(F(BOARD_NAME));
  Serial.print(F(":"));
  Serial.print(currentPath);
  Serial.print(F("# "));
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetClient) {
    telnetClient.print(F("root@"));
    telnetClient.print(F(BOARD_NAME));
    telnetClient.print(F(":"));
    telnetClient.print(currentPath);
    telnetClient.print(F("# "));
  }
#endif
}

ICACHE_FLASH_ATTR void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n\n[System] Booting UniKernel..."));

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(100);
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
  digitalWrite(LED_BUILTIN, HIGH);

  initFS();
  Wire.begin();
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
  WiFi.mode(WIFI_STA);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin();
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  ESP.wdtEnable(5000);
#endif
  EEPROM.begin(1024);

  char check;
  EEPROM.get(EEPROM_PASS_ADDR, check);

  if (check == 0xFF || check == 0x00) {
    needsSetup = true;
    addDmesg(F("Security Setup Required"));
  }

  unsigned long lockoutTime;
  EEPROM.get(EEPROM_LOCKOUT_ADDR, lockoutTime);
  unsigned long currentTime = millis();
  if (lockoutTime > 0 && ((currentTime - lockoutTime) < LOCKOUT_DURATION)) {
    isLockedOut = true;
    addDmesg(F("System locked from previous session"));
  } else {
    EEPROM.put(EEPROM_LOCKOUT_ADDR, (unsigned long)0);
#if defined(ESP8266) || defined(ESP32)
    EEPROM.commit();
#endif
  }

  if (WiFi.status() == WL_CONNECTED) {
    addDmesg(F("WiFi Connected Successfully"));
    Serial.print(F("IP: "));
    Serial.println(WiFi.localIP());
  } else {
    addDmesg(F("WiFi Not Connected (Auto)"));
  }
  telnetServer.begin();
  setupWebServer();
  addDmesg(F("Secure Boot Complete"));
#endif

  for (int t = 0; t < MAX_TASKS; t++) {
    taskTable[t].active = false;
  }

  delay(500);
  executeCommand((char*)"neofetch", true);
  

  for (int i = 0; i < MAX_FILES; i++) {
    if ((vfs[i].flags & FLAG_ACTIVE) && strcmp(vfs[i].name, "rc.local") == 0 && strcmp(vfs[i].parentDir, "/") == 0) {
      addDmesg(F("Running rc.local..."));
      runScript(vfs[i].content);
      break;
    }
  }

  printPrompt();
}

ICACHE_FLASH_ATTR void setupWebServer() {
  webServer.on("/", []() {
    String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>UniKernel</title><style>"
                    "body{background:#0f172a;color:#38bdf8;font-family:sans-serif;display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
                    "h1{font-size:3rem;font-weight:200;letter-spacing:4px;animation:fade 2s ease-in-out;} @keyframes fade{from{opacity:0;}to{opacity:1;}}"
                    "</style></head><body><h1>Hello UniKernel</h1></body></html>");
    webServer.send(200, "text/html", html);
  });

  webServer.on("/api/gpio", []() {
    int pin = webServer.arg("pin").toInt();
    int val = webServer.arg("val").toInt();
    if (pin >= 0 && pin <= 16) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, val);
      webServer.send(200, "text/plain", "OK");
    } else webServer.send(400, "text/plain", "Invalid Pin");
  });

  webServer.on("/api/stats", []() {
    String json = "{\"free\":" + String(ESP.getFreeHeap()) + ",\"up\":" + String(millis() / 1000) + "}";
    webServer.send(200, "application/json", json);
  });

  webServer.begin();
}

ICACHE_FLASH_ATTR void loop() {
  if (webEnabled) webServer.handleClient();

  unsigned long now = millis();
  static uint8_t lastM = 99;
  time_t tNow = time(nullptr);
  struct tm *ti = localtime(&tNow);
  if (ti->tm_year > 100 && ti->tm_min != lastM) {
    lastM = ti->tm_min;
    for (int i = 0; i < MAX_CRON; i++) {
      if (cronTable[i].active && cronTable[i].h == ti->tm_hour && cronTable[i].m == ti->tm_min) {
        char buf[32]; strcpy(buf, cronTable[i].cmd);
        executeCommand(buf, true);
      }
    }
  }
  for (int t = 0; t < MAX_TASKS; t++) {
    if (taskTable[t].active &&
        (now - taskTable[t].lastRun >= taskTable[t].interval)) {
      taskTable[t].lastRun = now;
      taskTable[t].func();
    }
  }

  if (isLockedOut) {
    delay(1000);
    return;
  }

  if (serialAuthenticated && isTimeout(lastSerialActivity, SESSION_TIMEOUT)) {
    serialAuthenticated = false;
    Serial.println(F("\nSerial session timeout. Logged out."));
    printPrompt();
  }
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetAuthenticated && isTimeout(lastTelnetActivity, SESSION_TIMEOUT)) {
    telnetAuthenticated = false;
    if (telnetClient && telnetClient.connected()) {
      telnetClient.println(F("\nTelnet session timeout. Logged out."));
    }
  }
#endif

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetEnabled && telnetServer.hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      telnetClient = telnetServer.available();

      telnetClient.write(255);
      telnetClient.write(251);
      telnetClient.write(1);
      telnetClient.println(F("\nWelcome to UniKernel NetShell"));
      telnetClient.println(F("Access Denied. Use: login [pass]"));
    } else {
      telnetServer.available().stop();
    }
  }
#endif

  static char lastChar = 0;
  char c = 0;
  bool hasInput = false;
  bool fromSerial = false;

  if (Serial.available() > 0) {
    c = Serial.read();
    hasInput = true;
    fromSerial = true;
  }
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  else if (telnetEnabled && telnetClient && telnetClient.available() > 0) {
    c = telnetClient.read();

    if (c == 255) {
      unsigned long startWait = millis();
      while (telnetClient.available() < 2 && millis() - startWait < 50)
        yield();
      if (telnetClient.available() >= 2) {
        telnetClient.read();
        telnetClient.read();
      }
      return;
    }

    hasInput = true;
    fromSerial = false;
  }
#endif

  if (hasInput) {
    if (millis() - lastLoginAttempt < loginCooldown) {
      if (c == '\r' || c == '\n') {
        kprint(F("\rSystem Cooldown: "));
        kprint((loginCooldown - (millis() - lastLoginAttempt)) / 1000);
        kprintln(F("s remaining."));
      }
      return;
    }

    if (fromSerial) {
      lastSerialActivity = millis();
    } else {
      lastTelnetActivity = millis();
    }
    if (c == '\r' || c == '\n') {

      if ((c == '\n' && lastChar == '\r') || (c == '\r' && lastChar == '\n')) {
        lastChar = 0;
        return;
      }
      lastChar = c;

      if (inputLen > 0) {
        inputBuffer[inputLen] = '\0';
        kprintln();

        bool isLogin = (strncmp_P(inputBuffer, PSTR("login"), 5) == 0);
        bool isHelp = (strncmp_P(inputBuffer, PSTR("help"), 4) == 0);
        bool isPasswd = (strncmp_P(inputBuffer, PSTR("passwd"), 6) == 0);
        bool currentAuth = fromSerial ? serialAuthenticated : telnetAuthenticated;
        
        if (!currentAuth && !isLogin && !isHelp && !(needsSetup && isPasswd && fromSerial)) {
          kprintln(F("--- ACCESS DENIED ---"));
          kprintln(F("System is protected. Please type: login [your_password]"));
          if (needsSetup) kprintln(F("Initial setup: type 'passwd [new_pass]' first via Serial."));
        } else {
          executeCommand(inputBuffer, fromSerial);
        }

        inputLen = 0;
        memset(inputBuffer, 0, sizeof(inputBuffer));
        printPrompt();
      } else {
        kprintln();
        printPrompt();
      }
    } else {
      lastChar = c;
      if (c == 8 || c == 127) {
        if (inputLen > 0) {
          inputLen--;
          inputBuffer[inputLen] = '\0';
          kprint(F("\b \b"));
        }
      } else if (inputLen < MAX_INPUT_LEN - 1) {
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
        ESP.wdtFeed();
#endif

        if (fromSerial) {
          Serial.print(c);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
          if (telnetClient && telnetClient.connected())
            telnetClient.print(c);
#endif
        } else {
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
          if (telnetClient && telnetClient.connected())
            telnetClient.print(c);
#endif
          Serial.print(c);
        }

        inputBuffer[inputLen] = c;
        inputLen++;
      }
    }
  }
}

ICACHE_FLASH_ATTR int indexOf(const char *str, const char *substr) {
  int i, j, slen = strlen(str), sublen = strlen(substr);
  for (i = 0; i <= slen - sublen; i++) {
    int match = 1;
    for (j = 0; j < sublen; j++) {
      if (str[i + j] != substr[j]) {
        match = 0;
        break;
      }
    }
    if (match)
      return i;
  }
  return -1;
}

ICACHE_FLASH_ATTR int atoi_safe(const char *str) {
  int num = 0;
  while (*str >= '0' && *str <= '9') {
    num = num * 10 + (*str - '0');
    str++;
  }
  return num;
}

ICACHE_FLASH_ATTR void toLowercase(char *str) {
  int i;
  for (i = 0; str[i] != '\0'; i++) {
    if (str[i] >= 'A' && str[i] <= 'Z')
      str[i] = str[i] - 'A' + 'a';
  }
}

ICACHE_FLASH_ATTR int safeConcatPath(char *dest, const char *add) {
  int destLen = strlen(dest);
  int addLen = strlen(add);
  if (destLen + addLen + 2 >= PATH_LEN)
    return 0;
  strncat(dest, add, PATH_LEN - destLen - 1);
  strncat(dest, "/", PATH_LEN - strlen(dest) - 1);
  return 1;
}

ICACHE_FLASH_ATTR bool checkPermission(int fileIdx, uint8_t action,
                                       bool fromSerial) {
  bool currentAuth = fromSerial ? serialAuthenticated : telnetAuthenticated;
  uint8_t currentUser = currentAuth ? 0 : 1;
  if (currentUser == 0)
    return true;
  uint16_t m = vfs[fileIdx].mode;
  if (vfs[fileIdx].ownerId == currentUser)
    return (m >> 6) & action;
  return m & action;
}

ICACHE_FLASH_ATTR void printPermissions(uint16_t m, bool isDir) {
  kprint(isDir ? F("d") : F("-"));
  const char chars[] = "rwx";
  for (int i = 6; i >= 0; i -= 3) {
    for (int j = 2; j >= 0; j--) {
      if ((m >> (i + j)) & 1) {
        char tmp[2] = {chars[2 - j], '\0'};
        kprint(tmp);
      } else {
        kprint(F("-"));
      }
    }
  }
}

ICACHE_FLASH_ATTR bool isTelnetSafeCommand(const char *cmd) {
  if (strcmp_P(cmd, PSTR("ls")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("cd")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("pwd")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("cat")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("info")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("dmesg")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("uptime")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("df")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("free")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("whoami")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("uname")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("ps")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("date")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("help")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("neofetch")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("clear")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("read")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("ping")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("ifconfig")) == 0) return true;
  if (strcmp_P(cmd, PSTR("wifi")) == 0) return true;
  if (strcmp_P(cmd, PSTR("on")) == 0) return true;
  if (strcmp_P(cmd, PSTR("off")) == 0) return true;
  if (strcmp_P(cmd, PSTR("gpio")) == 0) return true;
  if (strcmp_P(cmd, PSTR("login")) == 0) return true;
  if (strcmp_P(cmd, PSTR("logout")) == 0) return true;
  return false;
}

ICACHE_FLASH_ATTR void expandVars(char *line) {
  char temp[MAX_INPUT_LEN];
  char *p = line, *t = temp;
  while (*p && (t - temp < MAX_INPUT_LEN - 1)) {
    if (*p == '$') {
      p++;
      char key[ENV_KEY_LEN] = {0};
      int ki = 0;
      while (isalnum(*p) && ki < ENV_KEY_LEN - 1) key[ki++] = *p++;
      for (int i = 0; i < MAX_ENV; i++) {
        if (envTable[i].active && strcmp(envTable[i].key, key) == 0) {
          char *v = envTable[i].val;
          while (*v && (t - temp < MAX_INPUT_LEN - 1)) *t++ = *v++;
          break;
        }
      }
    } else *t++ = *p++;
  }
  *t = '\0';
  strcpy(line, temp);
}

ICACHE_FLASH_ATTR void executeCommand(char *line, bool fromSerial) {
  expandVars(line); 
  
  char *redir = strchr(line, '>');
  int savedRedirIdx = -1;
  if (redir) {
    *redir = '\0';
    char *filename = redir + 1;
    while (*filename == ' ') filename++;
    

    int found = -1, empty = -1;
    for (int j = 0; j < MAX_FILES; j++) {
      if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(vfs[j].name, filename) == 0 && strcmp(vfs[j].parentDir, currentPath) == 0) { found = j; break; }
      if (!(vfs[j].flags & FLAG_ACTIVE) && empty == -1) empty = j;
    }
    int target = (found != -1) ? found : empty;
    if (target != -1) {
      if (found == -1) {
        strncpy(vfs[target].name, filename, NAME_LEN - 1);
        vfs[target].flags = FLAG_ACTIVE;
        vfs[target].mode = 0644;
        vfs[target].ownerId = 0;
        strncpy(vfs[target].parentDir, currentPath, PATH_LEN - 1);
      }
      memset(vfs[target].content, 0, CONTENT_LEN);
      savedRedirIdx = redirectionFileIdx;
      redirectionFileIdx = target;
    }
  }

  char *cmd = line;
  char *args = NULL;
  int i, sp, pin, count;

  for (i = 0; line[i] != '\0'; i++) {
    if (line[i] == ' ') {
      line[i] = '\0';
      args = line + i + 1;
      break;
    }
  }

  if (args == NULL) {
    static char emptyArgs[] = "";
    args = emptyArgs;
  }

  while (*args == ' ')
    args++;

  toLowercase(cmd);

  bool currentAuth = fromSerial ? serialAuthenticated : telnetAuthenticated;
  if (!fromSerial && !currentAuth && !isTelnetSafeCommand(cmd)) {
    kprintln(F("Telnet: Command not allowed (Please login for full access)"));
    return;
  }

  if (strcmp_P(cmd, PSTR("on")) == 0) {
    pin = atoi_safe(args);
    if (pin < 0 || pin > 19) { Serial.println(F("Error: Pin 0-19")); return; }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    kprint(F("Pin ")); kprint(pin); kprintln(F(" is now HIGH (ON)"));
    return;
  }
  else if (strcmp_P(cmd, PSTR("off")) == 0) {
    pin = atoi_safe(args);
    if (pin < 0 || pin > 19) { Serial.println(F("Error: Pin 0-19")); return; }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    kprint(F("Pin ")); kprint(pin); kprintln(F(" is now LOW (OFF)"));
    return;
  }

  int memBefore = freeMemory();

  if (strcmp_P(cmd, PSTR("pinmode")) == 0) {
    sp = indexOf(args, " ");
    if (sp == -1) {
      Serial.println(F("Usage: pinmode [pin] [in/out]"));
      return;
    }
    pin = atoi_safe(args);

    if (pin < 0 || pin > 19) {
      Serial.println(F("Error: Pin must be 0-19"));
      return;
    }
    char mode[8] = "";
    char *argPtr = args + sp + 1;
    while (*argPtr == ' ')
      argPtr++; 
    strncpy(mode, argPtr, 7);
    mode[7] = '\0';
    for (int k = 0; k < 7; k++) {
      if (mode[k] <= 32)
        mode[k] = '\0';
    } 
    toLowercase(mode);
    if (strcmp_P(mode, PSTR("out")) == 0) {
      pinMode(pin, OUTPUT);
      Serial.print(F("Pin "));
      Serial.print(pin);
      Serial.println(F(" set to OUTPUT"));
    } else if (strcmp_P(mode, PSTR("in")) == 0) {
      pinMode(pin, INPUT_PULLUP);
      Serial.print(F("Pin "));
      Serial.print(pin);
      Serial.println(F(" set to INPUT"));
    } else {
      Serial.println(F("Error: Mode must be 'in' or 'out'"));
    }
  } else if (strcmp_P(cmd, PSTR("write")) == 0) {
    sp = indexOf(args, " ");
    if (sp == -1) {
      Serial.println(F("Usage: write [pin] [high/low]"));
      return;
    }
    pin = atoi_safe(args);

    if (pin < 0 || pin > 19) {
      Serial.println(F("Error: Pin must be 0-19"));
      return;
    }
    char val[8] = "";
    char *argPtr = args + sp + 1;
    while (*argPtr == ' ')
      argPtr++;
    strncpy(val, argPtr, 7);
    val[7] = '\0';
    for (int k = 0; k < 7; k++) {
      if (val[k] <= 32)
        val[k] = '\0';
    }
    toLowercase(val);

    if (strcmp_P(val, PSTR("high")) != 0 && strcmp_P(val, PSTR("low")) != 0) {
      Serial.println(F("Error: Value must be 'high' or 'low'"));
      return;
    }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, (strcmp_P(val, PSTR("high")) == 0 ? HIGH : LOW));
    Serial.print(F("Pin "));
    Serial.print(pin);
    Serial.print(F(" wrote "));
    Serial.println(strcmp_P(val, PSTR("high")) == 0 ? F("HIGH") : F("LOW"));
  } else if (strcmp_P(cmd, PSTR("read")) == 0) {
    pin = atoi_safe(args);

    if (pin < 0 || pin > 19) {
      Serial.println(F("Error: Pin must be 0-19"));
      return;
    }
    int value = digitalRead(pin);
    Serial.print(F("Pin "));
    Serial.print(pin);
    Serial.print(F(" value: "));
    Serial.println(value);
  } else if (strcmp_P(cmd, PSTR("gpio")) == 0) {
    sp = indexOf(args, " ");
    if (sp == -1) {
      Serial.println(
          F("Usage: gpio [pin] [on/off/toggle] OR gpio vixa [count]"));
      return;
    }
    char pinStr[8] = "";
    strncpy(pinStr, args, sp);
    pinStr[sp] = '\0';

    char action[8] = "";
    char *argPtr = args + sp + 1;
    while (*argPtr == ' ')
      argPtr++;
    strncpy(action, argPtr, 7);
    action[7] = '\0';
    for (int k = 0; k < 7; k++) {
      if (action[k] <= 32)
        action[k] = '\0';
    } 
    toLowercase(action);

    if (strcmp_P(pinStr, PSTR("vixa")) == 0) {
      count = atoi_safe(action);
      if (count <= 0)
        count = 10;
      if (count > 50)
        count = 50;
      addDmesg(F("LED disco mode activated"));
      Serial.println(F("LED DISCO MODE!"));
      
    
      int safePins[] = {5, 4, 0, 2, 14, 12, 13, 15};
      int numSafePins = 8;

      for (int cycle = 0; cycle < count; cycle++) {
        for (int i = 0; i < numSafePins; i++) {
          int p = safePins[i];
          pinMode(p, OUTPUT);
          digitalWrite(p, HIGH);
          delay(50);
          digitalWrite(p, LOW);
          yield(); 
        }
      }
      Serial.println(F("Disco finished!"));
      addDmesg(F("Disco complete"));
    } else {
      pin = atoi_safe(pinStr);

      if (pin < 0 || pin > 19) {
        kprintln(F("Error: Pin must be 0-19"));
        return;
      }

      if (strcmp_P(action, PSTR("on")) == 0) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
        kprint(F("GPIO "));
        kprint(pin);
        kprintln(F(" ON"));
      } else if (strcmp_P(action, PSTR("off")) == 0) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
        kprint(F("GPIO "));
        kprint(pin);
        kprintln(F(" OFF"));
      } else if (strcmp_P(action, PSTR("toggle")) == 0) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, !digitalRead(pin));
        kprint(F("GPIO "));
        kprint(pin);
        kprintln(F(" toggled"));
      } else {
        kprint(F("Error: Action '"));
        kprint(action);
        kprintln(F("' not recognized. Use: on, off, toggle"));
      }
    }
  } else if (strcmp_P(cmd, PSTR("ls")) == 0) {
    int empty = 1, j;
    bool isLong = (strcmp_P(args, PSTR("-l")) == 0);

    for (j = 0; j < MAX_FILES; j++) {
      if ((vfs[j].flags & FLAG_ACTIVE) &&
          strcmp(vfs[j].parentDir, currentPath) == 0) {
        if (isLong) {
          printPermissions(vfs[j].mode, (vfs[j].flags & FLAG_ISDIR));
          kprint(F(" "));
          kprint(vfs[j].ownerId == 0 ? F("root ") : F("guest "));
          kprint(vfs[j].name);
          if (vfs[j].flags & FLAG_ISDIR)
            kprint(F("/"));
          kprintln();
        } else {
          kprint(vfs[j].name);
          if (vfs[j].flags & FLAG_ISDIR)
            kprint(F("/"));
          kprint(F("  "));
        }
        empty = 0;
      }
    }

    if (strcmp(currentPath, "/dev/") == 0) {
      if (isLong)
        kprintln(F("crw-rw-rw- root null\ncrw-rw-rw- root led\ncrw-rw-rw- root "
                   "a0\ncrw-rw-rw- root a1\ncrw-rw-rw- root a2\ncrw-rw-rw- "
                   "root a3\ncrw-rw-rw- root a4\ncrw-rw-rw- root a5"));
      else
        kprint(F("null  led  a0  a1  a2  a3  a4  a5  "));
      empty = 0;
    }
    if (empty && !isLong)
      kprint(F("(empty)"));
    if (!isLong)
      kprintln();
  } else if (strcmp_P(cmd, PSTR("chown")) == 0) {
    char *user = args;
    char *filename = strchr(args, ' ');
    if (filename) {
      *filename = '\0'; filename++;
      uint8_t newOwner = 255;
      if (strcmp_P(user, PSTR("root")) == 0) newOwner = 0;
      else if (strcmp_P(user, PSTR("guest")) == 0) newOwner = 1;
      
      if (newOwner == 255) { kprintln(F("User not found. Use: root, guest")); return; }
      
      int j, found = 0;
      for (j = 0; j < MAX_FILES; j++) {
        if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(filename, vfs[j].name) == 0 && strcmp(vfs[j].parentDir, currentPath) == 0) {
          uint8_t currentUser = (fromSerial ? serialAuthenticated : telnetAuthenticated) ? 0 : 1;
          if (currentUser != 0) {
            kprintln(F("Only root can change ownership."));
          } else {
            vfs[j].ownerId = newOwner;
            kprintln(F("Ownership updated."));
          }
          found = 1; break;
        }
      }
      if (!found) kprintln(F("File not found."));
    } else {
      kprintln(F("Usage: chown [root/guest] [file]"));
    }
  } else if (strcmp_P(cmd, PSTR("chmod")) == 0) {
    char *modeStr = args;
    char *filename = strchr(args, ' ');
    if (filename) {
      *filename = '\0';
      filename++;
      int m = strtol(modeStr, NULL, 8);
      int j, found = 0;
      for (j = 0; j < MAX_FILES; j++) {
        if ((vfs[j].flags & FLAG_ACTIVE) &&
            strcmp(filename, vfs[j].name) == 0 &&
            strcmp(vfs[j].parentDir, currentPath) == 0) {
          uint8_t currentUser =
              (fromSerial ? serialAuthenticated : telnetAuthenticated) ? 0 : 1;
          if (currentUser != 0 && vfs[j].ownerId != currentUser) {
            kprintln(F("Permission denied."));
          } else {
            vfs[j].mode = m;
            kprintln(F("Mode updated."));
          }
          found = 1;
          break;
        }
      }
      if (!found)
        kprintln(F("File not found."));
    } else {
      kprintln(F("Usage: chmod [mode] [file]"));
    }
  } else if (strcmp_P(cmd, PSTR("mkdir")) == 0 ||
             strcmp_P(cmd, PSTR("touch")) == 0) {
    if (!isValidFsName(args)) {
      kprintln(
          F("Invalid name. Use 1-9 printable chars without / or spaces."));
      return;
    }
    int foundSlot = -1, j;
    for (j = 0; j < MAX_FILES; j++) {
      if (!(vfs[j].flags & FLAG_ACTIVE)) {
        foundSlot = j;
        break;
      }
    }
    if (foundSlot == -1) {
      kprintln(F("No space."));
      return;
    }
    strncpy(vfs[foundSlot].name, args, NAME_LEN - 1);
    vfs[foundSlot].name[NAME_LEN - 1] = '\0';
    strncpy(vfs[foundSlot].parentDir, currentPath, PATH_LEN - 1);
    vfs[foundSlot].parentDir[PATH_LEN - 1] = '\0';
    vfs[foundSlot].flags = FLAG_ACTIVE;
    if (strcmp_P(cmd, PSTR("mkdir")) == 0) {
      vfs[foundSlot].flags |= FLAG_ISDIR;
      vfs[foundSlot].mode = 0755;
    } else {
      vfs[foundSlot].mode = 0644;
    }
    vfs[foundSlot].ownerId =
        (fromSerial ? serialAuthenticated : telnetAuthenticated) ? 0 : 1;
    vfs[foundSlot].content[0] = '\0';
    kprintln(F("OK."));
  } else if (strcmp_P(cmd, PSTR("cd")) == 0) {
    if (strcmp_P(args, PSTR("..")) == 0 || strcmp_P(args, PSTR("/")) == 0) {
      strncpy(currentPath, "/", PATH_LEN - 1);
      currentPath[PATH_LEN - 1] = '\0';
    } else {

      char *target = (args[0] == '/') ? (args + 1) : args;
      const char *searchPath = (args[0] == '/') ? "/" : currentPath;

      int j, found = 0;
      for (j = 0; j < MAX_FILES; j++) {
        if ((vfs[j].flags & FLAG_ACTIVE) && (vfs[j].flags & FLAG_ISDIR) &&
            strcmp(target, vfs[j].name) == 0 &&
            strcmp(vfs[j].parentDir, searchPath) == 0) {
          strncpy(currentPath, searchPath, PATH_LEN - 1);
          safeConcatPath(currentPath, vfs[j].name);
          found = 1;
          break;
        }
      }
      if (!found)
        kprintln(F("No dir."));
    }
  } else if (strcmp_P(cmd, PSTR("pwd")) == 0) {
    kprintln(currentPath);
  } else if (strcmp_P(cmd, PSTR("echo")) == 0) {
    int arrow = indexOf(args, " > ");
    if (arrow != -1) {
      args[arrow] = '\0';
      char *text = args;
      char *filename = args + arrow + 3;

      if (strcmp(currentPath, "/dev/") == 0) {
        if (strcmp_P(filename, PSTR("led")) == 0) {
          pinMode(LED_BUILTIN, OUTPUT);
          digitalWrite(LED_BUILTIN, (text[0] == '1') ? HIGH : LOW);
          kprintln(F("LED updated."));
          return;
        } else if (strcmp_P(filename, PSTR("null")) == 0) {
          return;
        }
      }

      int j, found = 0;
      for (j = 0; j < MAX_FILES; j++) {
        if ((vfs[j].flags & FLAG_ACTIVE) && !(vfs[j].flags & FLAG_ISDIR) &&
            strcmp(filename, vfs[j].name) == 0 &&
            strcmp(vfs[j].parentDir, currentPath) == 0) {
          if (!checkPermission(j, 2, fromSerial)) {
            kprintln(F("Permission denied."));
          } else {
            strncpy(vfs[j].content, text, CONTENT_LEN - 1);
            vfs[j].content[CONTENT_LEN - 1] = '\0';
            kprintln(F("Saved."));
          }
          found = 1;
          break;
        }
      }
      if (!found)
        kprintln(F("File not found."));
    } else {
      kprintln(args);
    }
  } else if (strcmp_P(cmd, PSTR("cat")) == 0) {

    if (strcmp(currentPath, "/dev/") == 0) {
      if (strcmp_P(args, PSTR("null")) == 0) {
        return;
      } else if (strcmp_P(args, PSTR("led")) == 0) {
        kprintln(digitalRead(LED_BUILTIN) ? "1" : "0");
        return;
      } else if (args[0] == 'a' && args[1] >= '0' && args[1] <= '5') {
        int aPin = args[1] - '0';
        kprintln(analogRead(aPin));
        return;
      }
    }

    int j, found = 0;
    for (j = 0; j < MAX_FILES; j++) {
      if ((vfs[j].flags & FLAG_ACTIVE) && !(vfs[j].flags & FLAG_ISDIR) &&
          strcmp(args, vfs[j].name) == 0 &&
          strcmp(vfs[j].parentDir, currentPath) == 0) {
        if (!checkPermission(j, 4, fromSerial)) {
          kprintln(F("Permission denied."));
        } else {
          kprintln(vfs[j].content);
        }
        found = 1;
        break;
      }
    }
    if (!found)
      kprintln(F("File not found."));
  } else if (strcmp_P(cmd, PSTR("info")) == 0) {
    if (strcmp(currentPath, "/dev/") == 0 &&
        (strcmp_P(args, PSTR("null")) == 0 ||
         strcmp_P(args, PSTR("led")) == 0 ||
         (args[0] == 'a' && args[1] >= '0'))) {
      kprint(F("Name: "));
      kprintln(args);
      kprintln(F("Type: Virtual Device"));
      kprintln(F("Size: 0 (Stream)"));
      return;
    }
    int j, found = 0;
    for (j = 0; j < MAX_FILES; j++) {
      if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(args, vfs[j].name) == 0 &&
          strcmp(vfs[j].parentDir, currentPath) == 0) {
        kprint(F("Name: "));
        kprintln(vfs[j].name);
        kprint(F("Type: "));
        kprintln((vfs[j].flags & FLAG_ISDIR) ? F("Directory")
                                                   : F("File"));
        kprint(F("Size: "));
        kprint(strlen(vfs[j].content));
        kprintln(F(" bytes"));
        found = 1;
        break;
      }
    }
    if (!found)
      kprintln(F("Not found."));
  } else if (strcmp_P(cmd, PSTR("cp")) == 0 || strcmp_P(cmd, PSTR("mv")) == 0) {
    char *src = args;
    char *dst = strchr(args, ' ');
    if (dst) {
      *dst = '\0'; dst++;
      int sIdx = -1, dIdx = -1, empty = -1;
      for (int j = 0; j < MAX_FILES; j++) {
        if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(src, vfs[j].name) == 0 && strcmp(vfs[j].parentDir, currentPath) == 0) sIdx = j;
        if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(dst, vfs[j].name) == 0 && strcmp(vfs[j].parentDir, currentPath) == 0) dIdx = j;
        if (!(vfs[j].flags & FLAG_ACTIVE) && empty == -1) empty = j;
      }
      if (sIdx != -1) {
        if (strcmp_P(cmd, PSTR("cp")) == 0) {
          if (empty != -1) {
            vfs[empty] = vfs[sIdx];
            strncpy(vfs[empty].name, dst, NAME_LEN - 1);
            kprintln(F("Copied."));
          } else kprintln(F("FS full."));
        } else { 
          strncpy(vfs[sIdx].name, dst, NAME_LEN - 1);
          kprintln(F("Moved."));
        }
      } else kprintln(F("Source not found."));
    } else kprintln(F("Usage: cp/mv [src] [dst]"));
  } else if (strcmp_P(cmd, PSTR("append")) == 0) {
    char *file = args;
    char *text = strchr(args, ' ');
    if (text) {
      *text = '\0'; text++;
      int j, found = 0;
      for (j = 0; j < MAX_FILES; j++) {
        if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(file, vfs[j].name) == 0 && strcmp(vfs[j].parentDir, currentPath) == 0) {
          int curLen = strlen(vfs[j].content);
          if (curLen + strlen(text) < CONTENT_LEN - 1) {
            strcat(vfs[j].content, text);
            kprintln(F("Appended."));
          } else kprintln(F("File full."));
          found = 1; break;
        }
      }
      if (!found) kprintln(F("File not found."));
    } else kprintln(F("Usage: append [file] [text]"));
  } else if (strcmp_P(cmd, PSTR("rm")) == 0) {
    int j, found = 0;
    for (j = 0; j < MAX_FILES; j++) {
      if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(args, vfs[j].name) == 0 &&
          strcmp(vfs[j].parentDir, currentPath) == 0) {
        if (!checkPermission(j, 2, fromSerial)) {
          kprintln(F("Permission denied."));
          found = 1;
          break;
        }
        if (vfs[j].flags & FLAG_ISDIR) {
          char dirPath[PATH_LEN];
          snprintf_P(dirPath, PATH_LEN, PSTR("%s%s/"), currentPath, args);
          int k;
          for (k = 0; k < MAX_FILES; k++) {
            if ((vfs[k].flags & FLAG_ACTIVE) &&
                strncmp(vfs[k].parentDir, dirPath, strlen(dirPath)) == 0) {
              vfs[k].flags &= ~FLAG_ACTIVE;
            }
          }
        }
        vfs[j].flags &= ~FLAG_ACTIVE;
        kprintln(F("Removed."));
        found = 1;
        break;
      }
    }
    if (!found)
      kprintln(F("Not found."));
  } else if (strcmp_P(cmd, PSTR("dmesg")) == 0) {
    kprintln(F("=== KERNEL MESSAGES ==="));
    int j;
    for (j = 0; j < DMESG_LINES; j++) {
      if (dmesg[j].message[0] != '\0') {
        kprint(F("["));
        kprint(dmesg[j].timestamp);
        kprint(F("] "));
        kprintln(dmesg[j].message);
      }
    }
  } else if (strcmp_P(cmd, PSTR("uptime")) == 0) {
    unsigned long s = millis() / 1000;
    unsigned long h = s / 3600;
    unsigned long m = (s % 3600) / 60;
    unsigned long sec = s % 60;
    kprint(F("up "));
    kprint(h);
    kprint(F("h "));
    kprint(m);
    kprint(F("m "));
    kprint(sec);
    kprintln(F("s"));
    addDmesg(F("uptime command"));
  } else if (strcmp_P(cmd, PSTR("df")) == 0 ||
             strcmp_P(cmd, PSTR("free")) == 0) {
    kprint(F("Free RAM: "));
    kprint(freeMemory());
    kprintln(F(" bytes"));
  } else if (strcmp_P(cmd, PSTR("whoami")) == 0) {
    kprintln(F("root"));
  } else if (strcmp_P(cmd, PSTR("uname")) == 0) {
    kprintln(F("UniKernel v1.5"));
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
    kprintln(F("Kernel: ESP8266 RTOS/NonOS"));
    kprintln(F("Hardware: NodeMCU/ESP8266"));
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
    kprintln(F("Kernel: FreeRTOS"));
    kprintln(F("Hardware: ESP32"));
#else
    kprintln(F("Kernel: Arduino AVR"));
    kprintln(F("Hardware: Arduino UNO"));
#endif
    kprint(F("RAM: "));
    kprint(freeMemory());
    kprintln(F(" bytes free"));
  } else if (strcmp_P(cmd, PSTR("reboot")) == 0) {
    kprintln(F("Rebooting..."));
    addDmesg(F("System reboot"));
    delay(500);
#if defined(ARDUINO_ARCH_AVR)
    resetFunc();
#elif defined(ESP8266) || defined(ESP32)
    ESP.restart();
#endif
  } else if (strcmp_P(cmd, PSTR("clear")) == 0) {
    int j;
    for (j = 0; j < 30; j++)
      kprintln();
  } else if (strcmp_P(cmd, PSTR("sh")) == 0) {
    if (args[0] == '\0') {
      kprintln(F("Usage: sh [script]"));
      return;
    }
    int j, found = 0;
    for (j = 0; j < MAX_FILES; j++) {
      if ((vfs[j].flags & FLAG_ACTIVE) && !(vfs[j].flags & FLAG_ISDIR) &&
          strcmp(args, vfs[j].name) == 0 &&
          strcmp(vfs[j].parentDir, currentPath) == 0) {
        found = 1;
        if (!checkPermission(j, 1, fromSerial)) {
          kprintln(F("Permission denied."));
        } else {
          addDmesg(F("sh: running script"));
          runScript(vfs[j].content);
        }
        break;
      }
    }
    if (!found)
      kprintln(F("Script not found."));
  } else if (strcmp_P(cmd, PSTR("pwm")) == 0) {
    sp = indexOf(args, " ");
    if (sp == -1) {
      Serial.println(F("Usage: pwm [pin] [0-255]"));
      return;
    }
    pin = atoi_safe(args);
    char valStr[8] = "";
    char *argPtr = args + sp + 1;
    while (*argPtr == ' ')
      argPtr++;
    strncpy(valStr, argPtr, 7);
    valStr[7] = '\0';
    for (int k = 0; k < 7; k++) {
      if (valStr[k] <= 32)
        valStr[k] = '\0';
    }
    int pwmVal = atoi_safe(valStr);
    if (pwmVal < 0)
      pwmVal = 0;
    if (pwmVal > 255)
      pwmVal = 255;
    pinMode(pin, OUTPUT);
    analogWrite(pin, pwmVal);
    kprint(F("PWM pin "));
    kprint(pin);
    kprint(F(" set to "));
    kprintln(pwmVal);
  } else if (strcmp_P(cmd, PSTR("help")) == 0) {
    kprintln(F("Files: ls, cd, pwd, mkdir, touch, cat, echo, append, cp, mv, rm, chmod, chown, info, save, load"));
    kprintln(F("Hardw: on, off, gpio, pinmode, write, read, pwm, i2c, sh"));
    kprintln(F("Net  : wifi [scan/off/status/auto], wifi connect [ssid] [pass], ifconfig, ping, wget"));
    kprintln(F("Sys  : login, logout, passwd, ps, top, date, uptime, uname, dmesg, df, free, clear, reboot"));
  } else if (strcmp_P(cmd, PSTR("top")) == 0) {
    kprintln(F("UniKernel Top:"));
    kprint(F("Free RAM: ")); kprintln(freeMemory());
    kprint(F("Tasks: ")); 
    int count = 0;
    for(int i=0; i<MAX_TASKS; i++) if(taskTable[i].active) count++;
    kprintln(count);
    executeCommand((char*)"ps", fromSerial);
  } else if (strcmp_P(cmd, PSTR("neofetch")) == 0) {
    kprintln(F("       .---.          root@unikernel"));
    kprintln(F("      /     \\         --------------"));
    kprint(F("     |  (O)  |        OS: UniKernel v1.6 x86_esp\n"));
    kprint(F("      \\     /         Host: ")); kprintln(BOARD_NAME);
    kprintln(F("       '---'          Kernel: 6.14.0-unikernel"));
    kprint(F("     /|     |\\        Uptime: ")); kprint(millis()/1000); kprintln(F("s"));
    kprintln(F("    / |     | \\       Shell: UniShell 2.0"));
    kprint(F("   /  |     |  \\      Memory: ")); kprint(freeMemory()); kprintln(F(" free"));
    kprint(F("  '---'-----'---'     VFS: ")); kprint(MAX_FILES); kprintln(F(" slots"));
    kprintln(F(""));
    kprintln(F("  [RED] [GRN] [YLW] [BLU] [MAG] [CYN] [WHT]"));
  } else if (strcmp_P(cmd, PSTR("export")) == 0) {
    char *key = args;
    char *val = strchr(args, '=');
    if (val) {
      *val = '\0'; val++;
      int found = -1;
      for (int i = 0; i < MAX_ENV; i++) {
        if (envTable[i].active && strcmp(envTable[i].key, key) == 0) { found = i; break; }
        if (!envTable[i].active && found == -1) found = i;
      }
      if (found != -1) {
        strncpy(envTable[found].key, key, ENV_KEY_LEN-1);
        strncpy(envTable[found].val, val, ENV_VAL_LEN-1);
        envTable[found].active = true;
        kprintln(F("Var set."));
      } else kprintln(F("Env full."));
    } else kprintln(F("Usage: export key=val"));
  } else if (strcmp_P(cmd, PSTR("env")) == 0) {
    for (int i = 0; i < MAX_ENV; i++) {
      if (envTable[i].active) {
        kprint(envTable[i].key); kprint(F("=")); kprintln(envTable[i].val);
      }
    }
  } else if (strcmp_P(cmd, PSTR("ntp")) == 0) {
    if (!checkWiFi()) return;
    kprintln(F("Syncing time..."));
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  } else if (strcmp_P(cmd, PSTR("cron")) == 0) {
    if (strncmp_P(args, PSTR("add "), 4) == 0) {
      int h = atoi_safe(args + 4);
      char *mPtr = strchr(args + 4, ':');
      if (mPtr) {
        int m = atoi_safe(mPtr + 1);
        char *cPtr = strchr(mPtr + 1, ' ');
        if (cPtr) {
          while (*cPtr == ' ') cPtr++;
          int found = -1;
          for (int i = 0; i < MAX_CRON; i++) if (!cronTable[i].active) { found = i; break; }
          if (found != -1) {
            cronTable[found].h = h; cronTable[found].m = m;
            strncpy(cronTable[found].cmd, cPtr, 31);
            cronTable[found].active = true;
            kprintln(F("Cron added."));
          } else kprintln(F("Cron full."));
        }
      }
    } else if (strcmp_P(args, PSTR("list")) == 0) {
      for (int i = 0; i < MAX_CRON; i++) {
        if (cronTable[i].active) {
          kprint(i); kprint(F("> ")); kprint(cronTable[i].h); kprint(F(":"));
          if (cronTable[i].m < 10) kprint(F("0"));
          kprint(cronTable[i].m); kprint(F(" -> ")); kprintln(cronTable[i].cmd);
        }
      }
    } else if (strncmp_P(args, PSTR("rm "), 3) == 0) {
      int id = atoi_safe(args + 3);
      if (id >= 0 && id < MAX_CRON) { cronTable[id].active = false; kprintln(F("Removed.")); }
    } else kprintln(F("Usage: cron [add HH:MM cmd / list / rm ID]"));
  } else if (strcmp_P(cmd, PSTR("login")) == 0) {
    if (args[0] == '\0') {
      kprintln(F("Usage: login [pass]"));
      return;
    }
    if (needsSetup) {
      kprintln(
          F("SECURITY ERROR: Device uninitialized. Use 'passwd' via Serial."));
      return;
    }

    char savedPass[10];
    char hashedInput[10];
    EEPROM.get(EEPROM_PASS_ADDR, savedPass);
    hashPass(args, hashedInput);

    if (secureEquals(hashedInput, savedPass, 9)) {
      if (fromSerial) {
        serialAuthenticated = true;
        lastSerialActivity = millis();
      } else {
        telnetAuthenticated = true;
        lastTelnetActivity = millis();
      }
      loginFailCount = 0;
      kprintln(F("Login Successful."));
      addDmesg(F("User logged in"));
    } else {
      loginFailCount++;
      if (loginFailCount >= MAX_FAIL_COUNT) {
        isLockedOut = true;
        unsigned long lockoutTime = millis();
        EEPROM.put(EEPROM_LOCKOUT_ADDR, lockoutTime);
#if defined(ESP8266) || defined(ESP32)
        EEPROM.commit();
#endif
        kprintln(F("CRITICAL: System Locked due to Brute-Force."));
        addDmesg(F("System hard locked!"));
      } else {
        addDmesg(F("Login failed!"));
        lastLoginAttempt = millis();
        loginCooldown = 1000UL << loginFailCount;
        if (loginCooldown > 30000)
          loginCooldown = 30000;

        kprint(F("Access Denied. Attempts: "));
        kprint(loginFailCount);
        kprint(F(" Cooldown: "));
        kprint(loginCooldown / 1000);
        kprintln(F("s"));
      }
    }
  } else if (strcmp_P(cmd, PSTR("logout")) == 0) {
    if (fromSerial) {
      serialAuthenticated = false;
    } else {
      telnetAuthenticated = false;
    }
    kprintln(F("Logged out."));
  } else if (strcmp_P(cmd, PSTR("passwd")) == 0) {
    if (args[0] == '\0' || strlen(args) < 4) {
      kprintln(F("Usage: passwd [min 4 chars]"));
      return;
    }
    char hashedPass[10];
    char salt[PASS_SALT_LEN + 1];

    unsigned long entropy = micros() ^ analogRead(A0);
    for (int i = 0; i < PASS_SALT_LEN; i++) {
      salt[i] = 'a' + ((entropy >> (i * 4)) % 26);
    }
    salt[PASS_SALT_LEN] = '\0';

    EEPROM.put(EEPROM_SALT_ADDR, salt);
    hashPass(args, hashedPass);
    EEPROM.put(EEPROM_PASS_ADDR, hashedPass);
#if defined(ESP8266) || defined(ESP32)
    EEPROM.commit();
#endif
    needsSetup = false;
    kprintln(F("Password secured with high-entropy salt and XOR-Hash."));
    addDmesg(F("Root password changed"));
  } else if (strcmp_P(cmd, PSTR("telnet")) == 0) {
    if (strcmp_P(args, PSTR("on")) == 0) {
      telnetEnabled = true;
      kprintln(F("NetShell (Telnet) Enabled."));
      kprint(F("Listening on: ")); 
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
      kprintln(WiFi.localIP());
#else
      kprintln(F("Serial only (No WiFi)"));
#endif
    } else {
      telnetEnabled = false;
      kprintln(F("NetShell (Telnet) Disabled."));
    }
  } else if (strcmp_P(cmd, PSTR("ps")) == 0) {
    kprintln(F("PID  NAME      INTERVAL  STATUS"));
    kprintln(F("0    kernel    0         RUNNING"));
    for (int t = 0; t < MAX_TASKS; t++) {
      if (taskTable[t].active) {
        kprint(t + 1);
        kprint(F("    "));
        kprint(taskTable[t].name);
        kprint(F("    "));
        kprint(taskTable[t].interval);
        kprint(F("      "));
        kprintln(F("ACTIVE"));
      }
    }
    kprint(F("Free Heap: "));
    kprintln(freeMemory());
  } else if (strcmp_P(cmd, PSTR("kill")) == 0) {
    int pid = atoi_safe(args);
    if (pid > 0 && pid <= MAX_TASKS) {
      taskTable[pid - 1].active = false;
      kprint(F("Process "));
      kprint(pid);
      kprintln(F(" killed."));
    } else {
      kprintln(F("Usage: kill [pid 1-4]"));
    }
  } else if (strcmp_P(cmd, PSTR("bg")) == 0) {
    if (strcmp_P(args, PSTR("blink")) == 0) {
      int found = -1;
      for (int t = 0; t < MAX_TASKS; t++) {
        if (!taskTable[t].active) {
          found = t;
          break;
        }
      }
      if (found != -1) {
        extern void taskBlink(void);
        taskTable[found].func = taskBlink;
        taskTable[found].interval = 500;
        taskTable[found].lastRun = millis();
        taskTable[found].active = true;
        strncpy(taskTable[found].name, "blinker", NAME_LEN - 1);
        kprintln(F("Blinker started in background."));
      } else {
        kprintln(F("Task table full."));
      }
    } else {
      kprintln(F("Usage: bg [blink]"));
    }
  }
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (strcmp_P(cmd, PSTR("ping")) == 0) {
    if (!checkWiFi()) return;
    const char *host = (args[0] == '\0') ? "8.8.8.8" : args;
    kprint(F("Pinging "));
    kprintln(host);
    WiFiClient client;
    if (client.connect(host, 80) || client.connect(host, 53)) {
      kprintln(F("Success: Internet reachable."));
      client.stop();
    } else {
      kprintln(F("Failed: Host unreachable."));
    }
  } else if (strcmp_P(cmd, PSTR("wget")) == 0) {
    if (!checkWiFi()) return;
    char *url = args;
    char *file = NULL;
    for (int i = 0; url[i] != '\0'; i++) {
      if (url[i] == ' ') {
        url[i] = '\0';
        file = url + i + 1;
        break;
      }
    }
    if (!file) {
      kprintln(F("Usage: wget [url] [file]"));
      return;
    }
    kprint(F("Fetching... "));
    WiFiClient client;
    HTTPClient http;
    if (http.begin(client, url)) {
      int httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        WiFiClient *stream = http.getStreamPtr();
        int found = -1;

        for (int j = 0; j < MAX_FILES; j++) {
          if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(vfs[j].name, file) == 0 &&
              strcmp(vfs[j].parentDir, currentPath) == 0) {
            found = j;
            break;
          }
          if (!(vfs[j].flags & FLAG_ACTIVE) && found == -1)
            found = j;
        }
        if (found != -1) {
          if ((vfs[found].flags & FLAG_ACTIVE) &&
              !checkPermission(found, 2, fromSerial)) {
            kprintln(F("Permission denied."));
          } else {
            strncpy(vfs[found].name, file, NAME_LEN - 1);
            vfs[found].name[NAME_LEN - 1] = '\0';
            int bytesRead = 0;
            while (stream->available() && bytesRead < CONTENT_LEN - 1) {
              vfs[found].content[bytesRead] = stream->read();
              bytesRead++;
            }
            vfs[found].content[bytesRead] = '\0';
            if (!(vfs[found].flags & FLAG_ACTIVE)) {
              vfs[found].flags = FLAG_ACTIVE;
              vfs[found].mode = 0644;
              vfs[found].ownerId =
                  (fromSerial ? serialAuthenticated : telnetAuthenticated) ? 0
                                                                           : 1;
              strncpy(vfs[found].parentDir, currentPath, PATH_LEN - 1);
            }
            kprintln(F("Saved."));
          }
        }
      } else {
        kprint(F("Error: "));
        kprintln(httpCode);
      }
      http.end();
    }
  } else if (strcmp_P(cmd, PSTR("wifi")) == 0) {
    if (strncmp_P(args, PSTR("connect "), 8) == 0) {
      char *ssid = args + 8;
      char *pass = NULL;
      for (int i = 0; ssid[i] != '\0'; i++) {
        if (ssid[i] == ' ') {
          ssid[i] = '\0';
          pass = ssid + i + 1;
          break;
        }
      }
      if (pass) {
        if (strlen(pass) < 8) {
          Serial.println(F("Error: WPA2 Password must be at least 8 chars!"));
          return;
        }
        kprint(F("Connecting to "));
        kprintln(ssid);
        WiFi.disconnect();
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, pass);
        kprintln(F("Connecting... check 'wifi status' in 10s"));
      } else {
        kprintln(F("Usage: wifi connect [ssid] [pass]"));
      }
    } else if (strcmp_P(args, PSTR("reset")) == 0) {
      WiFi.disconnect(true);
      kprintln(F("WiFi settings cleared."));
    } else if (strcmp_P(args, PSTR("scan")) == 0) {

      kprintln(F("Scanning WiFi..."));
      WiFi.mode(WIFI_STA);
      int n = WiFi.scanNetworks();
      for (int i = 0; i < n; i++) {
        kprint(i + 1);
        kprint(F(": "));
        kprint(WiFi.SSID(i));
        kprint(F(" ("));
        kprint(WiFi.RSSI(i));
        kprintln(F(")"));
      }
    } else if (strcmp_P(args, PSTR("auto")) == 0) {
      kprintln(F("Attempting auto-connect..."));
      WiFi.mode(WIFI_STA);
      WiFi.begin();
    } else if (strcmp_P(args, PSTR("status")) == 0) {
      wl_status_t s = WiFi.status();
      kprint(F("Status: "));
      if (s == WL_CONNECTED) {
        kprint(F("Connected (OK) IP: "));
        kprintln(WiFi.localIP());
      } else if (s == WL_IDLE_STATUS)
        kprintln(F("Idle..."));
      else if (s == WL_NO_SSID_AVAIL)
        kprintln(F("SSID Not Found"));
      else if (s == WL_CONNECT_FAILED)
        kprintln(F("Connection Failed"));
      else if (s == WL_WRONG_PASSWORD)
        kprintln(F("Wrong Password"));
      else if (s == WL_DISCONNECTED)
        kprintln(F("Disconnected/Waiting..."));
      else
        kprintln(s);
    } else {
      kprintln(F("Usage: wifi [connect/scan/status/reset/off]"));
    }
  } else if (strcmp_P(cmd, PSTR("ifconfig")) == 0) {
    kprint(F("IP: "));
    kprintln(WiFi.localIP());
    kprint(F("GW: "));
    kprintln(WiFi.gatewayIP());
    kprint(F("MAC: "));
    kprintln(WiFi.macAddress());
  } else if (strcmp_P(cmd, PSTR("web")) == 0) {
    if (strcmp_P(args, PSTR("on")) == 0) {
      if (!checkWiFi()) return;
      webEnabled = true;
      kprintln(F("Web Dashboard Enabled."));
      kprint(F("URL: http://")); kprintln(WiFi.localIP());
    } else {
      webEnabled = false;
      kprintln(F("Web Dashboard Disabled."));
    }
  }
#endif
  else if (strcmp_P(cmd, PSTR("save")) == 0) {
    int addr = 0;
    EEPROM.put(addr, vfs);
#if defined(ESP8266) || defined(ESP32)
    EEPROM.commit();
#endif
    kprintln(F("FS saved to EEPROM."));
  } else if (strcmp_P(cmd, PSTR("load")) == 0) {
    int addr = 0;
    EEPROM.get(addr, vfs);
    kprintln(F("FS loaded from EEPROM."));
  } else if (strcmp_P(cmd, PSTR("i2c")) == 0) {
    if (strcmp_P(args, PSTR("scan")) == 0) {
      kprintln(F("Scanning I2C..."));
      byte error, address;
      int nDevices = 0;
      for (address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        if (error == 0) {
          kprint(F("Device at 0x"));
          char hexBuf[4];
          itoa(address, hexBuf, 16);
          kprintln(hexBuf);
          nDevices++;
        }
      }
      if (nDevices == 0)
        kprintln(F("No devices."));
    } else {
      kprintln(F("Usage: i2c scan"));
    }
  } else if (strcmp_P(cmd, PSTR("date")) == 0) {
    time_t now = time(nullptr);
    if (now < 100000) {
      unsigned long s = millis() / 1000;
      int h = (s / 3600) % 24;
      int m = (s / 60) % 60;
      int sec = s % 60;
      kprint(F("Uptime Clock: "));
      if (h < 10) kprint("0"); kprint(h); kprint(":");
      if (m < 10) kprint("0"); kprint(m); kprint(":");
      if (sec < 10) kprint("0"); kprintln(sec);
    } else {
      kprint(F("Local Date: ")); kprintln(ctime(&now));
    }
  } else {
    kprintln(F("Unknown command."));
  }

  if (savedRedirIdx != -1 || redir != NULL) { 
    redirectionFileIdx = savedRedirIdx;
  }
}

ICACHE_FLASH_ATTR void runScript(const char *content) {
  if (shellDepth >= MAX_SHELL_DEPTH) {
    kprintln(F("sh: max recursion depth reached"));
    return;
  }
  shellDepth++;

  char line[MAX_INPUT_LEN];
  int ci = 0, li = 0, lineNum = 0;
  bool truncated = false;
  int len = strlen(content);

  while (ci <= len) {
    char c = (ci < len) ? content[ci] : ';';
    ci++;
    if (c == ';' || c == '\n' || c == '\r') {
      if (li > 0) {
        line[li] = '\0';
        lineNum++;
        kprint(F("[sh:"));
        kprint(lineNum);
        kprint(F("] "));
        kprintln(line);
        executeCommand(line, true);
        li = 0;
      }
    } else {
      if (li < MAX_INPUT_LEN - 1) {
        line[li++] = c;
      } else {
        truncated = true;
      }
    }
  }
  if (truncated)
    addDmesg(F("sh: warning lines truncated"));
  shellDepth--;
  addDmesg(F("sh: script done"));
  kprintln(F("[sh] done."));
}
ICACHE_FLASH_ATTR void taskBlink() {
  static bool state = false;
  state = !state;
  digitalWrite(LED_BUILTIN, state ? HIGH : LOW);
}
