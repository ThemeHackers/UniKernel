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
  #define MAX_FILES 8
#endif

#define NAME_LEN 10         
#define CONTENT_LEN 24      
#define PATH_LEN 12         
#define DMESG_LINES 4
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

RAMFile vfs[MAX_FILES];
char currentPath[PATH_LEN] = "/";
char inputBuffer[64] = "";
int inputLen = 0;
DmesgEntry dmesg[DMESG_LINES];
int dmesgIndex = 0;
int shellDepth = 0;
bool authenticated = false; 
#define MAX_SHELL_DEPTH 3

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
WiFiServer telnetServer(23);
WiFiClient telnetClient;
#endif


void kprint(const __FlashStringHelper* s) {
  Serial.print(s);
  if (telnetClient && telnetClient.connected()) telnetClient.print(s);
}
void kprint(const char* s) {
  Serial.print(s);
  if (telnetClient && telnetClient.connected()) telnetClient.print(s);
}
void kprint(int n) {
  Serial.print(n);
  if (telnetClient && telnetClient.connected()) telnetClient.print(n);
}
void kprintln(const __FlashStringHelper* s) {
  Serial.println(s);
  if (telnetClient && telnetClient.connected()) telnetClient.println(s);
}
void kprintln(const char* s) {
  Serial.println(s);
  if (telnetClient && telnetClient.connected()) telnetClient.println(s);
}
void kprintln() {
  Serial.println();
  if (telnetClient && telnetClient.connected()) telnetClient.println();
}
void kprintln(int n) {
  Serial.println(n);
  if (telnetClient && telnetClient.connected()) telnetClient.println(n);
}
void kprintln(unsigned long n) {
  Serial.println(n);
  if (telnetClient && telnetClient.connected()) telnetClient.println(n);
}
void kprintln(IPAddress ip) {
  Serial.println(ip);
  if (telnetClient && telnetClient.connected()) telnetClient.println(ip);
}
void kprint(String s) {
  Serial.print(s);
  if (telnetClient && telnetClient.connected()) telnetClient.print(s);
}
void kprintln(String s) {
  Serial.println(s);
  if (telnetClient && telnetClient.connected()) telnetClient.println(s);
}

int freeMemory() {
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

void addDmesgRam(const char* msg) {
  if (dmesgIndex >= DMESG_LINES) dmesgIndex = 0;
  dmesg[dmesgIndex].timestamp = millis() / 1000;
  strncpy(dmesg[dmesgIndex].message, msg, DMESG_LEN - 1);
  dmesg[dmesgIndex].message[DMESG_LEN - 1] = '\0';
  dmesgIndex++;
}

void initFS() {
  int d, i;

  const char* dirs[] = {"home", "dev"};
  for (d = 0; d < 2; d++) {
    for (i = 0; i < MAX_FILES; i++) {
      if (!(vfs[i].flags & FLAG_ACTIVE)) {
        strncpy(vfs[i].name, dirs[d], NAME_LEN - 1);
        vfs[i].name[NAME_LEN - 1] = '\0';
        strncpy(vfs[i].parentDir, "/", PATH_LEN - 1);
        vfs[i].parentDir[PATH_LEN - 1] = '\0';
        vfs[i].flags = FLAG_ACTIVE | FLAG_ISDIR;
        break;
      }
    }
  }

  char devPath[PATH_LEN] = "/dev/";
  const char* pins[] = {"pin2", "pin3", "pin4"};
  for (d = 0; d < 3; d++) {
    for (i = 0; i < MAX_FILES; i++) {
      if (!(vfs[i].flags & FLAG_ACTIVE)) {
        strncpy(vfs[i].name, pins[d], NAME_LEN - 1);
        vfs[i].name[NAME_LEN - 1] = '\0';
        strncpy(vfs[i].parentDir, devPath, PATH_LEN - 1);
        vfs[i].parentDir[PATH_LEN - 1] = '\0';
        vfs[i].flags = FLAG_ACTIVE; 
        vfs[i].content[0] = '\0';
        break;
      }
    }
  }


  const char* sysDirs[] = {"sys", "bin"};
  for (d = 0; d < 2; d++) {
    for (i = 0; i < MAX_FILES; i++) {
      if (!(vfs[i].flags & FLAG_ACTIVE)) {
        strncpy(vfs[i].name, sysDirs[d], NAME_LEN - 1);
        vfs[i].flags = FLAG_ACTIVE | FLAG_ISDIR;
        strncpy(vfs[i].parentDir, "/", PATH_LEN - 1);
        break;
      }
    }
  }

  addDmesg(F("Kernel initialized"));
  addDmesg(F("Filesystem mounted"));
  addDmesg(F("Ready for commands"));
}

void printPrompt() {
  Serial.print(F("root@"));
  Serial.print(F(BOARD_NAME));
  Serial.print(F(":"));
  Serial.print(currentPath);
  Serial.print(F("# "));
  if (telnetClient) {
    telnetClient.print(F("root@"));
    telnetClient.print(F(BOARD_NAME));
    telnetClient.print(F(":"));
    telnetClient.print(currentPath);
    telnetClient.print(F("# "));
  }
}

void setup() {
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
  EEPROM.begin(1024);
  telnetServer.begin();
  telnetServer.setNoDelay(true);
  addDmesg(F("WiFi Auto-connect started"));
#endif
  delay(500);
  Serial.println(F("--- KernelUNO v1.5 (WiFi/Advanced) ---"));
  Serial.println(F("Type 'help' for commands"));
  printPrompt();
}

void loop() {
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetServer.hasClient()) {
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

  char c = 0;
  bool hasInput = false;

  if (Serial.available() > 0) {
    c = Serial.read();
    hasInput = true;
  } 
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  else if (telnetClient && telnetClient.available() > 0) {
    c = telnetClient.read();
    hasInput = true;
  }
#endif

  if (hasInput) {
    if (c == '\r' || c == '\n') {
      if (inputLen > 0) {
        inputBuffer[inputLen] = '\0';
        Serial.println();
        if (telnetClient) telnetClient.println();
        
        if (!authenticated && strncmp_P(inputBuffer, PSTR("login "), 6) != 0) {
          Serial.println(F("Access Denied. Use: login [pass]"));
          if (telnetClient) telnetClient.println(F("Access Denied. Use: login [pass]"));
        } else {
          executeCommand(inputBuffer);
        }
        
        inputLen = 0;
        memset(inputBuffer, 0, 32);
        printPrompt();
      } else {
        Serial.println();
        if (telnetClient) telnetClient.println();
        printPrompt();
      }
    }
    else if (c == 8 || c == 127) {
      if (inputLen > 0) {
        inputLen--;
        inputBuffer[inputLen] = '\0';
        Serial.print(F("\b \b"));
        if (telnetClient) telnetClient.print(F("\b \b"));
      }
    }
    else if (inputLen < 63) {
      Serial.print(c);
      if (telnetClient) telnetClient.print(c);
      inputBuffer[inputLen] = c;
      inputLen++;
    }
  }
}

int indexOf(const char* str, const char* substr) {
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

int atoi_safe(const char* str) {
  int num = 0;
  while (*str >= '0' && *str <= '9') {
    num = num * 10 + (*str - '0');
    str++;
  }
  return num;
}

void toLowercase(char* str) {
  int i;
  for (i = 0; str[i] != '\0'; i++) {
    if (str[i] >= 'A' && str[i] <= 'Z') str[i] = str[i] - 'A' + 'a';
  }
}

int safeConcatPath(char* dest, const char* add) {
  int destLen = strlen(dest);
  int addLen = strlen(add);
  if (destLen + addLen + 2 >= PATH_LEN) return 0;
  strncat(dest, add, PATH_LEN - destLen - 1);
  strncat(dest, "/", PATH_LEN - strlen(dest) - 1);
  return 1;
}

void runScript(const char* content);

void executeCommand(char* line) {
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

  int memBefore = freeMemory();


  if (strcmp_P(cmd, PSTR("pinmode")) == 0) {
    sp = indexOf(args, " ");
    if (sp == -1) { Serial.println(F("Usage: pinmode [pin] [in/out]")); return; }
    pin = atoi_safe(args);
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
  }
  else if (strcmp_P(cmd, PSTR("write")) == 0) {
    sp = indexOf(args, " ");
    if (sp == -1) { Serial.println(F("Usage: write [pin] [high/low]")); return; }
    pin = atoi_safe(args);
    char val[8] = "";
    strncpy(val, args + sp + 1, 7);
    val[7] = '\0';
    toLowercase(val);
    pinMode(pin, (strcmp_P(val, PSTR("high")) == 0 ? HIGH : LOW));
    digitalWrite(pin, (strcmp_P(val, PSTR("high")) == 0 ? HIGH : LOW));
    Serial.print(F("Pin ")); Serial.print(pin); Serial.print(F(" wrote "));
    Serial.println(strcmp_P(val, PSTR("high")) == 0 ? F("HIGH") : F("LOW"));
  }
  else if (strcmp_P(cmd, PSTR("read")) == 0) {
    pin = atoi_safe(args);
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
      int j, found = 0;
      for (j = 0; j < MAX_FILES; j++) {
        if ((vfs[j].flags & FLAG_ACTIVE) && (vfs[j].flags & FLAG_ISDIR) &&
            strcmp(args, vfs[j].name) == 0 &&
            strcmp(vfs[j].parentDir, currentPath) == 0) {
          if (!safeConcatPath(currentPath, vfs[j].name)) {
            strncpy(currentPath, "/", PATH_LEN - 1);
            currentPath[PATH_LEN - 1] = '\0';
            Serial.println(F("Path too long."));
            return;
          }
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
      
      int j, found = 0;
      for (j = 0; j < MAX_FILES; j++) {
        if ((vfs[j].flags & FLAG_ACTIVE) && !(vfs[j].flags & FLAG_ISDIR) &&
            strcmp(filename, vfs[j].name) == 0 &&
            strcmp(vfs[j].parentDir, currentPath) == 0) {
          strncpy(vfs[j].content, text, CONTENT_LEN - 1);
          vfs[j].content[CONTENT_LEN - 1] = '\0';
          kprintln(F("Saved."));
          if (strcmp_P(vfs[j].parentDir, PSTR("/dev/")) == 0 && strncmp_P(vfs[j].name, PSTR("pin"), 3) == 0) {
            int devPin = atoi_safe(vfs[j].name + 3);
            if (devPin > 0) {
              pinMode(devPin, OUTPUT);
              digitalWrite(devPin, (text[0] == '1') ? HIGH : LOW);
              addDmesg(F("GPIO toggled via echo"));
            }
          }
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
    kprintln(F("Sys  : login, ps, date, uptime, uname, dmesg, df, free, clear, reboot"));
  }
  else if (strcmp_P(cmd, PSTR("login")) == 0) {
    char savedPass[10];
    EEPROM.get(512, savedPass);
    if (args[0] == '\0') { Serial.println(F("Usage: login [pass]")); return; }
    if (strcmp(args, savedPass) == 0 || strcmp_P(args, PSTR("admin")) == 0) {
      authenticated = true;
      Serial.println(F("Login Successful."));
    } else {
      Serial.println(F("Wrong Password."));
    }
  }
  else if (strcmp_P(cmd, PSTR("ps")) == 0) {
    Serial.print(F("Tasks: 1 (shell)\nCPU: 80MHz\nFree Heap: "));
    Serial.println(freeMemory());
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
        String payload = http.getString(); 
        int found = -1;
        for (int j = 0; j < MAX_FILES; j++) {
           if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(vfs[j].name, file) == 0) { found = j; break; }
           if (!(vfs[j].flags & FLAG_ACTIVE) && found == -1) found = j;
        }
        if (found != -1) {
          strncpy(vfs[found].name, file, NAME_LEN-1);
          strncpy(vfs[found].content, payload.c_str(), CONTENT_LEN-1);
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



void runScript(const char* content) {
  if (shellDepth >= MAX_SHELL_DEPTH) {
    Serial.println(F("sh: max recursion depth reached"));
    return;
  }
  shellDepth++;
  
  char line[64];
  int ci = 0, li = 0, lineNum = 0;
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
        executeCommand(line);
        li = 0;
      }
    } else {
      if (li < 63) line[li++] = c;
    }
  }
  shellDepth--;
  addDmesg(F("sh: script done"));
  Serial.println(F("[sh] done."));
}
