#include <Arduino.h>
#include <string.h>
#include <avr/pgmspace.h>
#include <EEPROM.h>
#include <Wire.h>
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#endif

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
  #define MAX_FILES 16
#else
  #define MAX_FILES 6
#endif

#define NAME_LEN 10         
#define CONTENT_LEN 16      
#define PATH_LEN 12         
#define DMESG_LINES 10
#define DMESG_LEN 32

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  #define BOARD_NAME "esp8266"
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
  #define BOARD_NAME "esp32"
#elif defined(ARDUINO_ARCH_AVR)
  #define BOARD_NAME "uno"
#else
  #define BOARD_NAME "arduino"
#endif

#define FLAG_ACTIVE 0x01
#define FLAG_ISDIR  0x02

typedef struct {
  char name[NAME_LEN];
  char content[CONTENT_LEN];
  char parentDir[PATH_LEN];
  uint8_t flags;
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

#define MAX_TASKS 4
Task taskTable[MAX_TASKS];

RAMFile vfs[MAX_FILES];
char currentPath[PATH_LEN] = "/";
char inputBuffer[48] = "";
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
unsigned long lastSerialActivity = 0;
unsigned long lastTelnetActivity = 0; 
unsigned long lastLoginAttempt = 0;
unsigned long loginCooldown = 0;
#define MAX_SHELL_DEPTH 3
#define SESSION_TIMEOUT 300000 
#define MAX_FAIL_COUNT 5
#define LOCKOUT_DURATION 300000


#define EEPROM_PASS_ADDR 512
#define EEPROM_LOCKOUT_ADDR 522
#define EEPROM_SALT_ADDR 530


#define PASS_SALT_LEN 4
#define MAX_INPUT_LEN 48  

#if !defined(ICACHE_FLASH_ATTR)
#define ICACHE_FLASH_ATTR
#endif

#define KERNEL_KEY 0x5A

ICACHE_FLASH_ATTR void hashPass(const char* input, char* output) {
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


ICACHE_FLASH_ATTR bool isTimeout(unsigned long lastActivity, unsigned long timeout) {
  unsigned long currentTime = millis();
  return (currentTime - lastActivity) >= timeout;
}

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
WiFiServer telnetServer(23);
WiFiClient telnetClient;
#endif


void kprint(const __FlashStringHelper* s) {
  Serial.print(s);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetClient && telnetClient.connected()) telnetClient.print(s);
#endif
}
void kprint(const char* s) {
  Serial.print(s);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetClient && telnetClient.connected()) telnetClient.print(s);
#endif
}
void kprint(int n) {
  Serial.print(n);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetClient && telnetClient.connected()) telnetClient.print(n);
#endif
}
void kprintln(const __FlashStringHelper* s) {
  Serial.println(s);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetClient && telnetClient.connected()) telnetClient.println(s);
#endif
}
void kprintln(const char* s) {
  Serial.println(s);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetClient && telnetClient.connected()) telnetClient.println(s);
#endif
}
void kprintln() {
  Serial.println();
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetClient && telnetClient.connected()) telnetClient.println();
#endif
}
void kprintln(int n) {
  Serial.println(n);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetClient && telnetClient.connected()) telnetClient.println(n);
#endif
}
void kprintln(unsigned long n) {
  Serial.println(n);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetClient && telnetClient.connected()) telnetClient.println(n);
#endif
}
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
void kprintln(IPAddress ip) {
  Serial.println(ip);
  if (telnetClient && telnetClient.connected()) telnetClient.println(ip);
}
#endif
void kprint(String s) {
  Serial.print(s);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetClient && telnetClient.connected()) telnetClient.print(s);
#endif
}
void kprintln(String s) {
  Serial.println(s);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetClient && telnetClient.connected()) telnetClient.println(s);
#endif
}

ICACHE_FLASH_ATTR int freeMemory() {
#if defined(ARDUINO_ARCH_AVR)
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
#elif defined(ESP8266) || defined(ESP32)
  return ESP.getFreeHeap();
#else
  return 0; 
#endif
}

#if defined(ARDUINO_ARCH_AVR)
void(* resetFunc) (void) = 0;
#endif


void addDmesg(const __FlashStringHelper* msg) {
  if (dmesgIndex >= DMESG_LINES) dmesgIndex = 0;
  dmesg[dmesgIndex].timestamp = millis() / 1000;
  strncpy_P(dmesg[dmesgIndex].message, (PGM_P)msg, DMESG_LEN - 1);
  dmesg[dmesgIndex].message[DMESG_LEN - 1] = '\0';
  dmesgIndex++;
}

ICACHE_FLASH_ATTR void addDmesgRam(const char* msg) {
  if (dmesgIndex >= DMESG_LINES) dmesgIndex = 0;
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
  const char* sysDirs = sysDirs_P;
#else
  const char* sysDirs[] = {"home", "dev", "sys", "bin"};
#endif

  
  for (d = 0; d < 4; d++) {
#if defined(ARDUINO_ARCH_AVR)
    const char* dirName = sysDirs;
    for (int skip = 0; skip < d; skip++) {
      while (pgm_read_byte(dirName) != '\0') dirName++;
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
  Serial.println(F("\n\n[System] Booting KernelUNO..."));
  

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); delay(100);
  digitalWrite(LED_BUILTIN, HIGH); delay(100);
  digitalWrite(LED_BUILTIN, LOW); delay(100);
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
    Serial.print(F("IP: ")); Serial.println(WiFi.localIP());
  } else {
    addDmesg(F("WiFi Not Connected (Auto)"));
  }
  telnetServer.begin(); 
  addDmesg(F("Secure Boot Complete"));
#endif
  

  for (int t = 0; t < MAX_TASKS; t++) {
    taskTable[t].active = false;
  }

  delay(500);
  Serial.println(F("--- KernelUNO v1.5 (WiFi/Advanced) ---"));
  Serial.println(F("Type 'help' for commands"));
  printPrompt();
}

void loop() {

  unsigned long now = millis();
  for (int t = 0; t < MAX_TASKS; t++) {
    if (taskTable[t].active && (now - taskTable[t].lastRun >= taskTable[t].interval)) {
      taskTable[t].lastRun = now;
      taskTable[t].func();
    }
  }

  if (isLockedOut) { delay(1000); return; } 


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
      telnetClient.println(F("\nWelcome to KernelUNO NetShell"));
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
      while (telnetClient.available() < 2 && millis() - startWait < 50) yield(); 
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
        kprint(F("\rSystem Cooldown: ")); kprint((loginCooldown - (millis() - lastLoginAttempt)) / 1000); kprintln(F("s remaining."));
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
        bool isPasswd = (strncmp_P(inputBuffer, PSTR("passwd"), 6) == 0);
        bool currentAuth = fromSerial ? serialAuthenticated : telnetAuthenticated;
        
        if (!currentAuth && !isLogin && !(needsSetup && isPasswd && fromSerial)) {
          kprintln(F("Access Denied. Use: login [pass]"));
        } else {
          executeCommand(inputBuffer, fromSerial);
        }
        
        inputLen = 0;
        memset(inputBuffer, 0, 48);
        printPrompt();
      } else {
        kprintln();
        printPrompt();
      }
    }
    else {
      lastChar = c;
      if (c == 8 || c == 127) {
        if (inputLen > 0) {
          inputLen--;
          inputBuffer[inputLen] = '\0';
          kprint(F("\b \b"));
        }
      }
      else if (inputLen < MAX_INPUT_LEN - 1) {
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
        ESP.wdtFeed(); 
#endif
        
        if (fromSerial) {
          Serial.print(c);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
          if (telnetClient && telnetClient.connected()) telnetClient.print(c);
#endif
        } else {
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
          if (telnetClient && telnetClient.connected()) telnetClient.print(c);
#endif
          Serial.print(c); 
        }
        
        inputBuffer[inputLen] = c;
        inputLen++;
      }
    }
  }
}

ICACHE_FLASH_ATTR int indexOf(const char* str, const char* substr) {
  int i, j, slen = strlen(str), sublen = strlen(substr);
  for (i = 0; i <= slen - sublen; i++) {
    int match = 1;
    for (j = 0; j < sublen; j++) {
      if (str[i + j] != substr[j]) { match = 0; break; }
    }
    if (match) return i;
  }
  return -1;
}

ICACHE_FLASH_ATTR int atoi_safe(const char* str) {
  int num = 0;
  while (*str >= '0' && *str <= '9') {
    num = num * 10 + (*str - '0');
    str++;
  }
  return num;
}

ICACHE_FLASH_ATTR void toLowercase(char* str) {
  int i;
  for (i = 0; str[i] != '\0'; i++) {
    if (str[i] >= 'A' && str[i] <= 'Z') str[i] = str[i] - 'A' + 'a';
  }
}

ICACHE_FLASH_ATTR int safeConcatPath(char* dest, const char* add) {
  int destLen = strlen(dest);
  int addLen = strlen(add);
  if (destLen + addLen + 2 >= PATH_LEN) return 0;
  strncat(dest, add, PATH_LEN - destLen - 1);
  strncat(dest, "/", PATH_LEN - strlen(dest) - 1);
  return 1;
}

void runScript(const char* content);

ICACHE_FLASH_ATTR bool isTelnetSafeCommand(const char* cmd) {
  if (strcmp_P(cmd, PSTR("ls")) == 0) return true;
  if (strcmp_P(cmd, PSTR("cd")) == 0) return true;
  if (strcmp_P(cmd, PSTR("pwd")) == 0) return true;
  if (strcmp_P(cmd, PSTR("cat")) == 0) return true;
  if (strcmp_P(cmd, PSTR("info")) == 0) return true;
  if (strcmp_P(cmd, PSTR("dmesg")) == 0) return true;
  if (strcmp_P(cmd, PSTR("uptime")) == 0) return true;
  if (strcmp_P(cmd, PSTR("df")) == 0) return true;
  if (strcmp_P(cmd, PSTR("free")) == 0) return true;
  if (strcmp_P(cmd, PSTR("whoami")) == 0) return true;
  if (strcmp_P(cmd, PSTR("uname")) == 0) return true;
  if (strcmp_P(cmd, PSTR("ps")) == 0) return true;
  if (strcmp_P(cmd, PSTR("date")) == 0) return true;
  if (strcmp_P(cmd, PSTR("help")) == 0) return true;
  if (strcmp_P(cmd, PSTR("clear")) == 0) return true;
  if (strcmp_P(cmd, PSTR("read")) == 0) return true;
  if (strcmp_P(cmd, PSTR("ping")) == 0) return true;
  if (strcmp_P(cmd, PSTR("ifconfig")) == 0) return true;
  if (strcmp_P(cmd, PSTR("wifi")) == 0) return true;
  return false;
}

ICACHE_FLASH_ATTR void executeCommand(char* line, bool fromSerial) {
  char* cmd = line;
  char* args = NULL;
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

  toLowercase(cmd);

  if (!fromSerial && !isTelnetSafeCommand(cmd)) {
    kprintln(F("Telnet: Command not allowed (read-only mode)"));
    return;
  }

  int memBefore = freeMemory();


  if (strcmp_P(cmd, PSTR("pinmode")) == 0) {
    sp = indexOf(args, " ");
    if (sp == -1) { Serial.println(F("Usage: pinmode [pin] [in/out]")); return; }
    pin = atoi_safe(args);

    if (pin < 0 || pin > 19) { Serial.println(F("Error: Pin must be 0-19")); return; }
    char mode[8] = "";
    strncpy(mode, args + sp + 1, 7);
    mode[7] = '\0';
    toLowercase(mode);
    if (strcmp_P(mode, PSTR("out")) == 0) {
      pinMode(pin, OUTPUT);
      Serial.print(F("Pin ")); Serial.print(pin); Serial.println(F(" set to OUTPUT"));
    }
    else if (strcmp_P(mode, PSTR("in")) == 0) {
      pinMode(pin, INPUT_PULLUP);
      Serial.print(F("Pin ")); Serial.print(pin); Serial.println(F(" set to INPUT"));
    }
    else {
      Serial.println(F("Error: Mode must be 'in' or 'out'"));
    }
  }
  else if (strcmp_P(cmd, PSTR("write")) == 0) {
    sp = indexOf(args, " ");
    if (sp == -1) { Serial.println(F("Usage: write [pin] [high/low]")); return; }
    pin = atoi_safe(args);

    if (pin < 0 || pin > 19) { Serial.println(F("Error: Pin must be 0-19")); return; }
    char val[8] = "";
    strncpy(val, args + sp + 1, 7);
    val[7] = '\0';
    toLowercase(val);
    
    if (strcmp_P(val, PSTR("high")) != 0 && strcmp_P(val, PSTR("low")) != 0) {
      Serial.println(F("Error: Value must be 'high' or 'low'")); return;
    }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, (strcmp_P(val, PSTR("high")) == 0 ? HIGH : LOW));
    Serial.print(F("Pin ")); Serial.print(pin); Serial.print(F(" wrote "));
    Serial.println(strcmp_P(val, PSTR("high")) == 0 ? F("HIGH") : F("LOW"));
  }
  else if (strcmp_P(cmd, PSTR("read")) == 0) {
    pin = atoi_safe(args);
  
    if (pin < 0 || pin > 19) { Serial.println(F("Error: Pin must be 0-19")); return; }
    int value = digitalRead(pin);
    Serial.print(F("Pin ")); Serial.print(pin);
    Serial.print(F(" value: ")); Serial.println(value);
  }
  else if (strcmp_P(cmd, PSTR("gpio")) == 0) {
    sp = indexOf(args, " ");
    if (sp == -1) {
      Serial.println(F("Usage: gpio [pin] [on/off] OR gpio vixa [count]"));
      return;
    }
    char pinStr[8] = "";
    strncpy(pinStr, args, sp);
    pinStr[sp] = '\0';
    char action[8] = "";
    strncpy(action, args + sp + 1, 7);
    action[7] = '\0';
    toLowercase(action);

    if (strcmp_P(pinStr, PSTR("vixa")) == 0) {
      count = atoi_safe(action);
      if (count <= 0) count = 10;
      if (count > 50) count = 50; 
      addDmesg(F("LED disco mode activated"));
      Serial.println(F("LED DISCO MODE!"));
      int cycle, p;
      for (cycle = 0; cycle < count; cycle++) {
        for (p = 2; p <= 13; p++) {
          pinMode(p, OUTPUT);
          digitalWrite(p, HIGH);
          delay(50);
          digitalWrite(p, LOW);
        }
      }
      Serial.println(F("Disco finished!"));
      addDmesg(F("Disco complete"));
    } else {
      pin = atoi_safe(pinStr);

      if (pin < 0 || pin > 19) { Serial.println(F("Error: Pin must be 0-19")); return; }
      if (strcmp_P(action, PSTR("on")) == 0) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
        kprint(F("GPIO ")); kprint(pin); kprintln(F(" ON"));
      }
      else if (strcmp_P(action, PSTR("off")) == 0) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
        kprint(F("GPIO ")); kprint(pin); kprintln(F(" OFF"));
      }
      else if (strcmp_P(action, PSTR("toggle")) == 0) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, !digitalRead(pin));
        kprint(F("GPIO ")); kprint(pin); kprintln(F(" toggled"));
      }
      else {
        Serial.println(F("Error: Action must be 'on', 'off', or 'toggle'"));
      }
    }
  }
  else if (strcmp_P(cmd, PSTR("ls")) == 0) {
    int empty = 1, j;

    for (j = 0; j < MAX_FILES; j++) {
      if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(vfs[j].parentDir, currentPath) == 0) {
        kprint(vfs[j].name);
        if (vfs[j].flags & FLAG_ISDIR) kprint(F("/"));
        kprint(F("  "));
        empty = 0;
      }
    }
  
    if (strcmp(currentPath, "/dev/") == 0) {
      kprint(F("null  led  a0  a1  a2  a3  a4  a5  "));
      empty = 0;
    }
    if (empty) kprint(F("(empty)"));
    kprintln();
  }
  else if (strcmp_P(cmd, PSTR("mkdir")) == 0 || strcmp_P(cmd, PSTR("touch")) == 0) {
    int foundSlot = -1, j;
    for (j = 0; j < MAX_FILES; j++) {
      if (!(vfs[j].flags & FLAG_ACTIVE)) { foundSlot = j; break; }
    }
    if (foundSlot == -1) { Serial.println(F("No space.")); return; }
    strncpy(vfs[foundSlot].name, args, NAME_LEN - 1);
    vfs[foundSlot].name[NAME_LEN - 1] = '\0';
    strncpy(vfs[foundSlot].parentDir, currentPath, PATH_LEN - 1);
    vfs[foundSlot].parentDir[PATH_LEN - 1] = '\0';
    vfs[foundSlot].flags = FLAG_ACTIVE;
    if (strcmp_P(cmd, PSTR("mkdir")) == 0) vfs[foundSlot].flags |= FLAG_ISDIR;
    vfs[foundSlot].content[0] = '\0';
    Serial.println(F("OK."));
  }
  else if (strcmp_P(cmd, PSTR("cd")) == 0) {
    if (strcmp_P(args, PSTR("..")) == 0 || strcmp_P(args, PSTR("/")) == 0) {
      strncpy(currentPath, "/", PATH_LEN - 1);
      currentPath[PATH_LEN - 1] = '\0';
    } else {
   
      char* target = (args[0] == '/') ? (args + 1) : args;
      const char* searchPath = (args[0] == '/') ? "/" : currentPath;
      
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
      if (!found) Serial.println(F("No dir."));
    }
  }
  else if (strcmp_P(cmd, PSTR("pwd")) == 0) {
    kprintln(currentPath);
  }
  else if (strcmp_P(cmd, PSTR("echo")) == 0) {
    int arrow = indexOf(args, " > ");
    if (arrow != -1) {
      args[arrow] = '\0';
      char* text = args;
      char* filename = args + arrow + 3;
      
   
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
          strncpy(vfs[j].content, text, CONTENT_LEN - 1);
          vfs[j].content[CONTENT_LEN - 1] = '\0';
          kprintln(F("Saved."));
          found = 1;
          break;
        }
      }
      if (!found) kprintln(F("File not found."));
    } else {
      kprintln(args);
    }
  }
  else if (strcmp_P(cmd, PSTR("cat")) == 0) {

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
        Serial.println(vfs[j].content);
        found = 1;
        break;
      }
    }
    if (!found) Serial.println(F("File not found."));
  }
  else if (strcmp_P(cmd, PSTR("info")) == 0) {
    if (strcmp(currentPath, "/dev/") == 0 && (strcmp_P(args, PSTR("null")) == 0 || strcmp_P(args, PSTR("led")) == 0 || (args[0] == 'a' && args[1] >= '0'))) {
      kprint(F("Name: ")); kprintln(args);
      kprintln(F("Type: Virtual Device"));
      kprintln(F("Size: 0 (Stream)"));
      return;
    }
    int j, found = 0;
    for (j = 0; j < MAX_FILES; j++) {
      if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(args, vfs[j].name) == 0 && strcmp(vfs[j].parentDir, currentPath) == 0) {
        Serial.print(F("Name: ")); Serial.println(vfs[j].name);
        Serial.print(F("Type: ")); Serial.println((vfs[j].flags & FLAG_ISDIR) ? F("Directory") : F("File"));
        Serial.print(F("Size: ")); Serial.print(strlen(vfs[j].content)); Serial.println(F(" bytes"));
        found = 1;
        break;
      }
    }
    if (!found) Serial.println(F("Not found."));
  }
  else if (strcmp_P(cmd, PSTR("rm")) == 0) {
    int j, found = 0;
    for (j = 0; j < MAX_FILES; j++) {
      if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(args, vfs[j].name) == 0 && strcmp(vfs[j].parentDir, currentPath) == 0) {
        if (vfs[j].flags & FLAG_ISDIR) {
          char dirPath[PATH_LEN];
          snprintf_P(dirPath, PATH_LEN, PSTR("%s%s/"), currentPath, args);
          int k;
          for (k = 0; k < MAX_FILES; k++) {
            if ((vfs[k].flags & FLAG_ACTIVE) && strncmp(vfs[k].parentDir, dirPath, strlen(dirPath)) == 0) {
              vfs[k].flags &= ~FLAG_ACTIVE;
            }
          }
        }
        vfs[j].flags &= ~FLAG_ACTIVE;
        Serial.println(F("Removed."));
        found = 1;
        break;
      }
    }
    if (!found) Serial.println(F("Not found."));
  }
  else if (strcmp_P(cmd, PSTR("dmesg")) == 0) {
    Serial.println(F("=== KERNEL MESSAGES ==="));
    int j;
    for (j = 0; j < DMESG_LINES; j++) {
      if (dmesg[j].message[0] != '\0') {
        Serial.print(F("["));
        Serial.print(dmesg[j].timestamp);
        Serial.print(F("] "));
        Serial.println(dmesg[j].message);
      }
    }
  }
  else if (strcmp_P(cmd, PSTR("uptime")) == 0) {
    unsigned long s = millis() / 1000;
    unsigned long h = s / 3600;
    unsigned long m = (s % 3600) / 60;
    unsigned long sec = s % 60;
    kprint(F("up "));
    kprint(h); kprint(F("h "));
    kprint(m); kprint(F("m "));
    kprint(sec); kprintln(F("s"));
    addDmesg(F("uptime command"));
  }
  else if (strcmp_P(cmd, PSTR("df")) == 0 || strcmp_P(cmd, PSTR("free")) == 0) {
    kprint(F("Free RAM: "));
    kprint(freeMemory());
    kprintln(F(" bytes"));
  }
  else if (strcmp_P(cmd, PSTR("whoami")) == 0) {
    kprintln(F("root"));
  }
  else if (strcmp_P(cmd, PSTR("uname")) == 0) {
    kprintln(F("KernelUNO v1.5"));
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
  }
  else if (strcmp_P(cmd, PSTR("reboot")) == 0) {
    Serial.println(F("Rebooting..."));
    addDmesg(F("System reboot"));
    delay(500);
#if defined(ARDUINO_ARCH_AVR)
    resetFunc();
#elif defined(ESP8266) || defined(ESP32)
    ESP.restart();
#endif
  }
  else if (strcmp_P(cmd, PSTR("clear")) == 0) {
    int j;
    for (j = 0; j < 30; j++) kprintln();
  }
  else if (strcmp_P(cmd, PSTR("sh")) == 0) {
    if (args[0] == '\0') {
      Serial.println(F("Usage: sh [script]"));
      return;
    }
    int j, found = 0;
    for (j = 0; j < MAX_FILES; j++) {
      if ((vfs[j].flags & FLAG_ACTIVE) && !(vfs[j].flags & FLAG_ISDIR) &&
          strcmp(args, vfs[j].name) == 0 &&
          strcmp(vfs[j].parentDir, currentPath) == 0) {
        found = 1;
        addDmesg(F("sh: running script"));
        runScript(vfs[j].content);
        break;
      }
    }
    if (!found) Serial.println(F("Script not found."));
  }
  else if (strcmp_P(cmd, PSTR("pwm")) == 0) {
    sp = indexOf(args, " ");
    if (sp == -1) { Serial.println(F("Usage: pwm [pin] [0-255]")); return; }
    pin = atoi_safe(args);
    char valStr[8] = "";
    strncpy(valStr, args + sp + 1, 7);
    valStr[7] = '\0';
    int pwmVal = atoi_safe(valStr);
    if (pwmVal < 0) pwmVal = 0;
    if (pwmVal > 255) pwmVal = 255;
    pinMode(pin, OUTPUT);
    analogWrite(pin, pwmVal);
    Serial.print(F("PWM pin ")); Serial.print(pin);
    Serial.print(F(" set to ")); Serial.println(pwmVal);
  }
  else if (strcmp_P(cmd, PSTR("help")) == 0) {
    kprintln(F("Files: ls, cd, pwd, mkdir, touch, cat, echo, rm, info, save, load"));
    kprintln(F("Hardw: pinmode, write, read, gpio, pwm, i2c, sh"));
    kprintln(F("Net  : wifi [scan/off/status/auto], wifi connect [ssid] [pass], ifconfig, ping, wget"));
    kprintln(F("Sys  : login, logout, passwd, ps, date, uptime, uname, dmesg, df, free, clear, reboot"));
  }
  else if (strcmp_P(cmd, PSTR("login")) == 0) {
    if (args[0] == '\0') { kprintln(F("Usage: login [pass]")); return; }
    if (needsSetup) { kprintln(F("SECURITY ERROR: Device uninitialized. Use 'passwd' via Serial.")); return; }
    
    char savedPass[10];
    char hashedInput[10];
    EEPROM.get(EEPROM_PASS_ADDR, savedPass);
    hashPass(args, hashedInput);
    
    if (strcmp(hashedInput, savedPass) == 0) {
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
        if (loginCooldown > 30000) loginCooldown = 30000;  
        
        kprint(F("Access Denied. Attempts: ")); kprint(loginFailCount);
        kprint(F(" Cooldown: ")); kprint(loginCooldown / 1000); kprintln(F("s"));
      }
    }
  }
  else if (strcmp_P(cmd, PSTR("logout")) == 0) {
    if (fromSerial) {
      serialAuthenticated = false;
    } else {
      telnetAuthenticated = false;
    }
    kprintln(F("Logged out."));
  }
  else if (strcmp_P(cmd, PSTR("passwd")) == 0) {
    if (args[0] == '\0' || strlen(args) < 4) { kprintln(F("Usage: passwd [min 4 chars]")); return; }
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
  }
  else if (strcmp_P(cmd, PSTR("telnet")) == 0) {
    if (strcmp_P(args, PSTR("on")) == 0) {
      telnetEnabled = true;
      kprintln(F("NetShell (Telnet) Enabled."));
    } else {
      telnetEnabled = false;
      kprintln(F("NetShell (Telnet) Disabled."));
    }
  }
  else if (strcmp_P(cmd, PSTR("ps")) == 0) {
    kprintln(F("PID  NAME      INTERVAL  STATUS"));
    kprintln(F("0    kernel    0         RUNNING"));
    for (int t = 0; t < MAX_TASKS; t++) {
      if (taskTable[t].active) {
        kprint(t + 1); kprint(F("    "));
        kprint(taskTable[t].name); kprint(F("    "));
        kprint(taskTable[t].interval); kprint(F("      "));
        kprintln(F("ACTIVE"));
      }
    }
    kprint(F("Free Heap: ")); kprintln(freeMemory());
  }
  else if (strcmp_P(cmd, PSTR("kill")) == 0) {
    int pid = atoi_safe(args);
    if (pid > 0 && pid <= MAX_TASKS) {
      taskTable[pid - 1].active = false;
      kprint(F("Process ")); kprint(pid); kprintln(F(" killed."));
    } else {
      kprintln(F("Usage: kill [pid 1-4]"));
    }
  }
  else if (strcmp_P(cmd, PSTR("bg")) == 0) {
    if (strcmp_P(args, PSTR("blink")) == 0) {
      int found = -1;
      for (int t = 0; t < MAX_TASKS; t++) {
        if (!taskTable[t].active) { found = t; break; }
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
  else if (strcmp_P(cmd, PSTR("ping")) == 0) {
    const char* host = (args[0] == '\0') ? "8.8.8.8" : args;
    Serial.print(F("Pinging ")); Serial.println(host);
    WiFiClient client;
    if (client.connect(host, 80) || client.connect(host, 53)) {
      Serial.println(F("Success: Internet reachable."));
      client.stop();
    } else {
      Serial.println(F("Failed: Host unreachable."));
    }
  }
  else if (strcmp_P(cmd, PSTR("wget")) == 0) {
    char* url = args;
    char* file = NULL;
    for (int i = 0; url[i] != '\0'; i++) {
      if (url[i] == ' ') { url[i] = '\0'; file = url + i + 1; break; }
    }
    if (!file) { Serial.println(F("Usage: wget [url] [file]")); return; }
    Serial.print(F("Fetching... "));
    WiFiClient client;
    HTTPClient http;
    if (http.begin(client, url)) {
      int httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        WiFiClient* stream = http.getStreamPtr();
        int found = -1;
     
        for (int j = 0; j < MAX_FILES; j++) {
           if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(vfs[j].name, file) == 0 && strcmp(vfs[j].parentDir, currentPath) == 0) { 
             found = j; break; 
           }
           if (!(vfs[j].flags & FLAG_ACTIVE) && found == -1) found = j;
        }
        if (found != -1) {
          strncpy(vfs[found].name, file, NAME_LEN-1);
          vfs[found].name[NAME_LEN-1] = '\0';
          int bytesRead = 0;
          while (stream->available() && bytesRead < CONTENT_LEN - 1) {
            vfs[found].content[bytesRead] = stream->read();
            bytesRead++;
          }
          vfs[found].content[bytesRead] = '\0';
          vfs[found].flags = FLAG_ACTIVE;
          Serial.println(F("Saved."));
        }
      } else { Serial.print(F("Error: ")); Serial.println(httpCode); }
      http.end();
    }
  }
  else if (strcmp_P(cmd, PSTR("wifi")) == 0) {
    if (strncmp_P(args, PSTR("connect "), 8) == 0) {
      char* ssid = args + 8;
      char* pass = NULL;
      for (int i = 0; ssid[i] != '\0'; i++) {
        if (ssid[i] == ' ') { ssid[i] = '\0'; pass = ssid + i + 1; break; }
      }
      if (pass) {
        if (strlen(pass) < 8) {
          Serial.println(F("Error: WPA2 Password must be at least 8 chars!"));
          return;
        }
        Serial.print(F("Connecting to ")); Serial.println(ssid);
        WiFi.disconnect();
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, pass);
        Serial.println(F("Connecting... check 'wifi status' in 10s"));
      } else { Serial.println(F("Usage: wifi connect [ssid] [pass]")); }
    }
    else if (strcmp_P(args, PSTR("reset")) == 0) {
      WiFi.disconnect(true);
      Serial.println(F("WiFi settings cleared."));
    }
    else if (strcmp_P(args, PSTR("scan")) == 0) {

      Serial.println(F("Scanning WiFi..."));
      WiFi.mode(WIFI_STA);
      int n = WiFi.scanNetworks();
      for (int i = 0; i < n; i++) {
        Serial.print(i + 1); Serial.print(F(": "));
        Serial.print(WiFi.SSID(i)); Serial.print(F(" ("));
        Serial.print(WiFi.RSSI(i)); Serial.println(F(")"));
      }
    }
    else if (strcmp_P(args, PSTR("auto")) == 0) {
      kprintln(F("Attempting auto-connect..."));
      WiFi.mode(WIFI_STA);
      WiFi.begin(); 
    }
    else if (strcmp_P(args, PSTR("status")) == 0) {
      wl_status_t s = WiFi.status();
      kprint(F("Status: "));
      if (s == WL_CONNECTED) {
        kprint(F("Connected (OK) IP: "));
        kprintln(WiFi.localIP());
      }
      else if (s == WL_IDLE_STATUS) kprintln(F("Idle..."));
      else if (s == WL_NO_SSID_AVAIL) kprintln(F("SSID Not Found"));
      else if (s == WL_CONNECT_FAILED) kprintln(F("Connection Failed"));
      else if (s == WL_WRONG_PASSWORD) kprintln(F("Wrong Password"));
      else if (s == WL_DISCONNECTED) kprintln(F("Disconnected/Waiting..."));
      else kprintln(s);
    }
    else {
      Serial.println(F("Usage: wifi [connect/scan/status/reset/off]"));
    }
  }
  else if (strcmp_P(cmd, PSTR("ifconfig")) == 0) {
    kprint(F("IP: ")); kprintln(WiFi.localIP());
    kprint(F("GW: ")); kprintln(WiFi.gatewayIP());
    kprint(F("MAC: ")); kprintln(WiFi.macAddress());
  }
#endif
  else if (strcmp_P(cmd, PSTR("save")) == 0) {
    int addr = 0;
    EEPROM.put(addr, vfs);
#if defined(ESP8266) || defined(ESP32)
    EEPROM.commit();
#endif
    Serial.println(F("FS saved to EEPROM."));
  }
  else if (strcmp_P(cmd, PSTR("load")) == 0) {
    int addr = 0;
    EEPROM.get(addr, vfs);
    Serial.println(F("FS loaded from EEPROM."));
  }
  else if (strcmp_P(cmd, PSTR("i2c")) == 0) {
    if (strcmp_P(args, PSTR("scan")) == 0) {
      Serial.println(F("Scanning I2C..."));
      byte error, address;
      int nDevices = 0;
      for(address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        if (error == 0) {
          Serial.print(F("Device at 0x"));
          if (address < 16) Serial.print("0");
          Serial.println(address, HEX);
          nDevices++;
        }
      }
      if (nDevices == 0) Serial.println(F("No devices."));
    } else {
      Serial.println(F("Usage: i2c scan"));
    }
  }
  else if (strcmp_P(cmd, PSTR("date")) == 0) {
    unsigned long s = millis() / 1000;
    int h = (s / 3600) % 24;
    int m = (s / 60) % 60;
    int sec = s % 60;
    Serial.print(F("Sys Date: "));
    if(h<10) Serial.print("0"); Serial.print(h); Serial.print(":");
    if(m<10) Serial.print("0"); Serial.print(m); Serial.print(":");
    if(sec<10) Serial.print("0"); Serial.print(sec);
    Serial.println();
  }
  else {
    Serial.println(F("Unknown command."));
  }

  int memAfter = freeMemory();
  Serial.print(F("[Mem: "));
  Serial.print(memBefore - memAfter); 
  Serial.print(F(" used | "));
  Serial.print(memAfter);
  Serial.println(F(" free]"));
}



ICACHE_FLASH_ATTR void runScript(const char* content) {
  if (shellDepth >= MAX_SHELL_DEPTH) {
    Serial.println(F("sh: max recursion depth reached"));
    return;
  }
  shellDepth++;
  
  char line[80]; 
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
        Serial.print(F("[sh:")); Serial.print(lineNum); Serial.print(F("] "));
        Serial.println(line);
        executeCommand(line, true);
        li = 0;
      }
    } else {
      if (li < 79) {
        line[li++] = c;
      } else {
        truncated = true;
      }
    }
  }
  if (truncated) addDmesg(F("sh: warning lines truncated"));
  shellDepth--;
  addDmesg(F("sh: script done"));
  Serial.println(F("[sh] done."));
}
void taskBlink() {
  static bool state = false;
  state = !state;
  digitalWrite(LED_BUILTIN, state ? HIGH : LOW);
}
