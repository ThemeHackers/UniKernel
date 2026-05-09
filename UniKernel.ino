#include <Arduino.h>
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266WiFi.h>
  #include <WiFiServerSecure.h>
  #include <ESP8266WebServer.h>
  #include <ESP8266mDNS.h>
  #include <ESP8266HTTPClient.h>
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  #include <ESPmDNS.h>
  #include <HTTPClient.h>
#endif
ADC_MODE(ADC_VCC);
#include <EEPROM.h>
#include <Wire.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include "UniAccel.h"

WebSocketsClient webSocket;
bool accelConnected = false;
bool accelStopRequested = true;
char accelHost[16] = "192.168.1.50";
int accelPort = 81;
int accelRetryCount = 0;
unsigned long accelStartTime = 0;

ICACHE_FLASH_ATTR void discoverAccelHost();

#define XOR_KEY 0x5A
template <size_t N>
struct Obfuscator {
    char data[N];
    constexpr Obfuscator(const char* str, char key) : data{} {
        for (size_t i = 0; i < N; ++i) {
            data[i] = str[i] ^ key;
        }
    }
};

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
#define MAX_FILES 16
#define CONTENT_LEN 128
#define DMESG_LINES 10
#define MAX_INPUT_LEN 192
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
#define MAX_ENV 8
#define ENV_KEY_LEN 10

char global_obf_buf[MAX_INPUT_LEN];
#define _OSTR(str) \
    ([]() -> const char* { \
        constexpr Obfuscator<sizeof(str)> obf(str, XOR_KEY); \
        for (size_t i = 0; i < sizeof(str) - 1; ++i) { \
            global_obf_buf[i] = (char)(obf.data[i] ^ XOR_KEY); \
        } \
        global_obf_buf[sizeof(str) - 1] = '\0'; \
        return global_obf_buf; \
    }())
#define ENV_VAL_LEN 32
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
#define BOARD_NAME "esp8266"
#include <time.h>
extern "C" {
#include "user_interface.h"
}

void stripQuotes(char *s) {
  if (!s)
    return;
  char *start = s;
  while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
    start++;
  int len = strlen(start);
  while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t' ||
                     start[len - 1] == '\r' || start[len - 1] == '\n')) {
    start[len - 1] = '\0';
    len--;
  }
  if (len > 0 && start[len - 1] == '\"') {
    start[len - 1] = '\0';
    len--;
  }
  if (len > 0 && start[0] == '\"') {
    memmove(s, start + 1, len);
  } else if (start != s) {
    memmove(s, start, len + 1);
  }
}
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#define BOARD_NAME "esp32"
#elif defined(ARDUINO_ARCH_AVR)
#define BOARD_NAME "uno"
#else
#define BOARD_NAME "arduino"
#endif

#define FLAG_ACTIVE 0x01
#define FLAG_ISDIR 0x02

const char* CLR_RST = "\033[0m";
const char* CLR_RED = "\033[1;31m";
const char* CLR_GRN = "\033[1;32m";
const char* CLR_YLW = "\033[1;33m";
const char* CLR_BLU = "\033[1;34m";
const char* CLR_MAG = "\033[1;35m";
const char* CLR_CYN = "\033[1;36m";
const char* CLR_WHT = "\033[1;37m";

bool useColor = false;

void kprintColor(const char *c) {
  if (useColor)
    kprint(c);
}

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
  unsigned long executionCount;
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

#define MAX_ALIAS 6
typedef struct {
  char name[NAME_LEN];
  char cmd[32];
  bool active;
} Alias;
Alias aliasTable[MAX_ALIAS];

#define MAX_TRIGS 4
typedef struct {
  char cond[16];
  int val;
  char op; 
  char action[32];
  bool active;
} Trigger;
Trigger triggerTable[MAX_TRIGS];

RAMFile vfs[MAX_FILES];
char currentPath[PATH_LEN] = "/";
char inputBuffer[MAX_INPUT_LEN] = "";
int inputLen = 0;
DmesgEntry dmesg[DMESG_LINES];
int dmesgIndex = 0;
int shellDepth = 0;
int escState = 0; 
bool serialAuthenticated = false;
bool telnetAuthenticated = false;
bool sshAuthenticated = false;
bool isSSHInput = false;
uint8_t loginFailCount = 0;
bool isLockedOut = false;
bool needsSetup = false;
bool telnetEnabled = false;
bool webEnabled = false;
bool otaEnabled = false;
unsigned long otaEndTime = 0;
#define OTA_WINDOW 300000 
int redirectionFileIdx = -1;

#define MAX_HISTORY 8
char cmdHistory[MAX_HISTORY][MAX_INPUT_LEN];
int historyWriteIdx = 0;
int historyViewIdx = -1;
int historyCount = 0;

unsigned long lastSerialActivity = 0;
unsigned long lastTelnetActivity = 0;
unsigned long lastSSHActivity = 0;
unsigned long lastLoginAttempt = 0;
unsigned long loginCooldown = 0;
char whitelistIP[16] = "";
#define MAX_SHELL_DEPTH 2
#define SESSION_TIMEOUT 300000
#define MAX_FAIL_COUNT 5
#define LOCKOUT_DURATION 300000

#define EEPROM_PASS_ADDR 512
#define EEPROM_LOCKOUT_ADDR 530
#define EEPROM_SALT_ADDR 538
#define EEPROM_OTA_PASS_ADDR 550
#define EEPROM_FAIL_COUNT_ADDR 566
#define EEPROM_BOOT_FILE_ADDR 580

#define EEPROM_VFS_ADDR 1024
#define VFS_MAGIC 0x55AA

#define PASS_SALT_LEN 4
#if !defined(ICACHE_FLASH_ATTR)
#define ICACHE_FLASH_ATTR
#endif

#define KERNEL_KEY 0x5A

#if defined(ARDUINO_ARCH_AVR)

void fastPinMode(uint8_t pin, uint8_t mode) {
  if (pin <= 7) {
    if (mode == OUTPUT)
      DDRD |= (1 << pin);
    else {
      DDRD &= ~(1 << pin);
      if (mode == INPUT_PULLUP)
        PORTD |= (1 << pin);
    }
  } else if (pin <= 13) {
    uint8_t bPin = pin - 8;
    if (mode == OUTPUT)
      DDRB |= (1 << bPin);
    else {
      DDRB &= ~(1 << bPin);
      if (mode == INPUT_PULLUP)
        PORTB |= (1 << bPin);
    }
  } else if (pin <= 19) {
    uint8_t cPin = pin - 14;
    if (mode == OUTPUT)
      DDRC |= (1 << cPin);
    else {
      DDRC &= ~(1 << cPin);
      if (mode == INPUT_PULLUP)
        PORTC |= (1 << cPin);
    }
  }
}

void fastDigitalWrite(uint8_t pin, uint8_t val) {
  if (pin <= 7) {
    if (val)
      PORTD |= (1 << pin);
    else
      PORTD &= ~(1 << pin);
  } else if (pin <= 13) {
    uint8_t bPin = pin - 8;
    if (val)
      PORTB |= (1 << bPin);
    else
      PORTB &= ~(1 << bPin);
  } else if (pin <= 19) {
    uint8_t cPin = pin - 14;
    if (val)
      PORTC |= (1 << cPin);
    else
      PORTC &= ~(1 << cPin);
  }
}

uint8_t fastDigitalRead(uint8_t pin) {
  if (pin <= 7)
    return (PIND & (1 << pin)) ? HIGH : LOW;
  if (pin <= 13)
    return (PINB & (1 << (pin - 8))) ? HIGH : LOW;
  if (pin <= 19)
    return (PINC & (1 << (pin - 14))) ? HIGH : LOW;
  return LOW;
}

volatile uint32_t system_ticks = 0;
ISR(TIMER1_COMPA_vect) { system_ticks++; }

void initHeartbeat() {
  cli();
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  OCR1A = 15624;
  TCCR1B |= (1 << WGM12);
  TCCR1B |= (1 << CS12) | (1 << CS10);
  TIMSK1 |= (1 << OCIE1A);
  sei();
}
#else

#if defined(ESP8266)

void fastPinMode(uint8_t pin, uint8_t mode) {

  pinMode(pin, mode);

  if (pin > 16)
    return;
  if (mode == OUTPUT)
    GPE |= (1 << pin);
  else
    GPE &= ~(1 << pin);
}
void fastDigitalWrite(uint8_t pin, uint8_t val) {
  if (pin > 16) {
    digitalWrite(pin, val);
    return;
  }
  if (val)
    GPOS = (1 << pin);
  else
    GPOC = (1 << pin);
}
uint8_t fastDigitalRead(uint8_t pin) {
  if (pin > 16)
    return digitalRead(pin);
  return (GPI & (1 << pin)) ? HIGH : LOW;
}
#elif defined(ESP32)

#define fastPinMode(p, m) pinMode(p, m)
#define fastDigitalWrite(p, v) digitalWrite(p, v)
#define fastDigitalRead(p) digitalRead(p)
#endif
#endif

ICACHE_FLASH_ATTR void executeCommand(char *line, bool fromSerial);
ICACHE_FLASH_ATTR void parseAndExecute(char *line, size_t maxLen, bool fromSerial);
ICACHE_FLASH_ATTR void kPulse();
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
  char salt[PASS_SALT_LEN + 1];
  EEPROM.get(EEPROM_SALT_ADDR, salt);
  salt[PASS_SALT_LEN] = '\0';

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  br_sha256_context ctx;
  br_sha256_init(&ctx);
  br_sha256_update(&ctx, salt, strlen(salt));
  br_sha256_update(&ctx, input, strlen(input));
  uint8_t hash[32];
  br_sha256_out(&ctx, hash);
  for (int i = 0; i < 16; i++) {
    output[i] = hash[i];
  }
  output[16] = '\0';
#else

  #error "Strong cryptographic hash required. Alternate targets must implement SHA-256."
#endif
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

ICACHE_FLASH_ATTR bool isIpAllowed(IPAddress ip) {
  if (strlen(whitelistIP) == 0) return true;
  return ip.toString() == String(whitelistIP);
}

ICACHE_FLASH_ATTR bool checkWebAuth(String pass, IPAddress remoteIp) {
  if (!isIpAllowed(remoteIp)) {
    logError("Firewall block: IP not whitelisted");
    return false;
  }
  if (isLockedOut) return false;
  if (millis() - lastLoginAttempt < loginCooldown) return false;

  char hashedInput[17];
  char savedPass[17];
  EEPROM.get(EEPROM_PASS_ADDR, savedPass);
  hashPass(pass.c_str(), hashedInput);

  if (secureEquals(hashedInput, savedPass, 16)) {
    loginFailCount = 0;
    return true;
  } else {
    loginFailCount++;
    lastLoginAttempt = millis();
    loginCooldown = 1000UL << loginFailCount;
    if (loginCooldown > 30000) loginCooldown = 30000;
    EEPROM.put(EEPROM_FAIL_COUNT_ADDR, (uint8_t)loginFailCount);
    EEPROM.commit();
    addDmesgRam("Web auth failed");
    return false;
  }
}

ICACHE_FLASH_ATTR bool isTimeout(unsigned long lastActivity,
                                 unsigned long timeout) {
  unsigned long currentTime = millis();
  return (currentTime - lastActivity) >= timeout;
}

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
#include <WiFiServerSecure.h>
WiFiServer telnetServer(23);
WiFiServerSecure sshServer(22);
WiFiClient telnetClient;
WiFiClientSecure sshClient;
bool sshEnabled = false;
int authFailures = 0;
unsigned long lockoutEnd = 0;
unsigned long lastActivity = 0;
ESP8266WebServer webServer(80);
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
WiFiServer telnetServer(23);
WiFiClient telnetClient;
bool sshEnabled = false;
int authFailures = 0;
unsigned long lockoutEnd = 0;
unsigned long lastActivity = 0;
WebServer webServer(80);
#endif

const char DASHBOARD_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'>
<title>UniKernel | Advanced Dashboard</title>
<link href='https://fonts.googleapis.com/css2?family=Outfit:wght@300;500;700&family=JetBrains+Mono&display=swap' rel='stylesheet'>
<style>
:root { --bg: #050b1a; --card: rgba(255,255,255,0.05); --primary: #00f2ff; --accent: #7000ff; }
body { background: var(--bg); color: #fff; font-family: 'Outfit', sans-serif; margin: 0; overflow-x: hidden; }
.glass { background: var(--card); backdrop-filter: blur(10px); border: 1px solid rgba(255,255,255,0.1); border-radius: 20px; }
.container { max-width: 1000px; margin: 50px auto; padding: 20px; }
header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 40px; }
h1 { font-weight: 700; font-size: 2.2rem; background: linear-gradient(45deg, var(--primary), var(--accent)); -webkit-background-clip: text; -webkit-text-fill-color: transparent; margin: 0; }
.grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 20px; }
.card { padding: 25px; position: relative; }
.gauge-container { position: relative; width: 150px; height: 150px; margin: 10px auto; }
.gauge-svg { transform: rotate(-90deg); width: 100%; height: 100%; }
.gauge-bg { fill: none; stroke: rgba(255,255,255,0.1); stroke-width: 12; }
.gauge-fill { fill: none; stroke: var(--primary); stroke-width: 12; stroke-dasharray: 440; stroke-dashoffset: 440; transition: 1s; stroke-linecap: round; }
.gauge-text { position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); font-family: 'JetBrains Mono'; font-size: 1.5rem; color: var(--primary); }
.btn { padding: 12px 20px; border-radius: 10px; border: none; background: linear-gradient(45deg, var(--primary), var(--accent)); color: #fff; font-weight: 600; cursor: pointer; transition: 0.3s; width: 100%; }
.btn:hover { transform: translateY(-3px); box-shadow: 0 5px 15px rgba(0,242,255,0.3); }
input { width: 100%; padding: 10px; border-radius: 8px; border: 1px solid #333; background: #111; color: #fff; margin-bottom: 10px; box-sizing: border-box; }
</style></head><body>
<div class='container'><header><h1>UniKernel Core Pro</h1><div><span style='color:#00ff88'>●</span> ONLINE</div></header>
<div class='grid'><div class='card glass'><h3>Memory Usage</h3><div class='gauge-container'><svg class='gauge-svg' viewBox='0 0 160 160'><circle class='gauge-bg' cx='80' cy='80' r='70'/><circle id='g-mem' class='gauge-fill' cx='80' cy='80' r='70'/></svg><div class='gauge-text' id='t-mem'>0%</div></div><p style='text-align:center'>Heap Saturation</p></div>
<div class='card glass'><h3>System Control</h3><input type='password' id='p' placeholder='Auth Token'><button class='btn' onclick='toggle(2,0)'>LED ON</button><br><button class='btn' style='background:#ff4757; margin-top:8px;' onclick='toggle(2,1)'>LED OFF</button></div>
<div class='card glass'><h3>Power Manager</h3><input type='number' id='sl' placeholder='Sleep (sec)'><button class='btn' style='background:#7000ff' onclick='doSleep()'>ENTER DEEP SLEEP</button></div></div></div>
<script>
function update(){
  const pw = document.getElementById('p').value;
  fetch('/api/stats?pass='+pw).then(r=>r.json()).then(d=>{
  if (d.error) return;
  const p = Math.round((1 - d.free/81920)*100);
  document.getElementById('g-mem').style.strokeDashoffset = 440 - (440 * p / 100);
  document.getElementById('t-mem').innerText = p + '%';
});}
async function toggle(p,v){ const pw=document.getElementById('p').value; await fetch('/api/gpio', { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({pin: p, val: v, pass: pw}) }); }
async function doSleep(){ const s = document.getElementById('sl').value; await fetch('/api/sleep?s='+s); }
setInterval(update, 2000); update();
</script></body></html>
)rawhtml";

ICACHE_FLASH_ATTR void kprint(const __FlashStringHelper *s) {
  if (redirectionFileIdx != -1) {
    strncat_P(vfs[redirectionFileIdx].content, (PGM_P)s,
              CONTENT_LEN - strlen(vfs[redirectionFileIdx].content) - 1);
    return;
  }
  Serial.print(s);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
  if (telnetClient && telnetClient.connected())
    telnetClient.print(s);
#endif
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (sshClient && sshClient.connected())
    sshClient.print(s);
#endif
#if defined(ESP32)
  if (btEnabled)
    SerialBT.print(s);
#endif
}
ICACHE_FLASH_ATTR void kprint(const char *s) {
  if (redirectionFileIdx != -1) {
    strncat(vfs[redirectionFileIdx].content, s,
            CONTENT_LEN - strlen(vfs[redirectionFileIdx].content) - 1);
    return;
  }
  Serial.print(s);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
  if (telnetClient && telnetClient.connected())
    telnetClient.print(s);
#endif
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (sshClient && sshClient.connected())
    sshClient.print(s);
#endif
#if defined(ESP32)
  if (btEnabled)
    SerialBT.print(s);
#endif
}
ICACHE_FLASH_ATTR void kprint(int n) {
  if (redirectionFileIdx != -1) {
    char buf[12];
    itoa(n, buf, 10);
    strncat(vfs[redirectionFileIdx].content, buf,
            CONTENT_LEN - strlen(vfs[redirectionFileIdx].content) - 1);
    return;
  }
  Serial.print(n);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
  if (telnetClient && telnetClient.connected())
    telnetClient.print(n);
#endif
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (sshClient && sshClient.connected())
    sshClient.print(n);
#endif
#if defined(ESP32)
  if (btEnabled)
    SerialBT.print(n);
#endif
}
ICACHE_FLASH_ATTR void kprint(int n, int base) {
  if (redirectionFileIdx != -1) {
    char buf[12];
    itoa(n, buf, base);
    strncat(vfs[redirectionFileIdx].content, buf,
            CONTENT_LEN - strlen(vfs[redirectionFileIdx].content) - 1);
    return;
  }
  Serial.print(n, base);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
  if (telnetClient && telnetClient.connected())
    telnetClient.print(n, base);
#endif
}
ICACHE_FLASH_ATTR void kprintln(const __FlashStringHelper *s) {
  kprint(s);
  kprintln();
}
ICACHE_FLASH_ATTR
void kprint_sys(const char *s) {
  Serial.print(s);
  if (telnetClient && telnetClient.connected())
    telnetClient.print(s);
}
void kprint_sys(const String &s) { kprint_sys(s.c_str()); }
void kprintln_sys(const char *s) {
  kprint_sys(s);
  kprint_sys("\n");
}
void kprintln_sys(const String &s) { kprintln_sys(s.c_str()); }
void kprintln_sys(const __FlashStringHelper *ifsh) {
  Serial.println(ifsh);
  if (telnetClient && telnetClient.connected())
    telnetClient.println(ifsh);
}
void kprint_sys(const __FlashStringHelper *ifsh) {
  Serial.print(ifsh);
  if (telnetClient && telnetClient.connected())
    telnetClient.print(ifsh);
}

char *kTrim(char *s) {
  if (!s)
    return s;
  while (isspace((unsigned char)*s))
    s++;
  if (*s == 0)
    return s;
  char *end = s + strlen(s) - 1;
  while (end > s && isspace((unsigned char)*end))
    end--;
  end[1] = '\0';
  return s;
}

void redrawPrompt() {
  kprint("\r");
  kprint(CLR_CYN);
  kprint("root@esp8266:");
  kprint(currentPath);
  kprint("# ");
  kprint(CLR_RST);
  kprint(inputBuffer);
}

void kprintLog(const String &msg) {
  if (inputLen > 0) {
    kprint("\r\033[2K"); 
    kprint(msg.c_str());
    redrawPrompt();
  } else {
    kprint(msg.c_str());
  }
}

void kprintlnLog(const String &msg) {
  if (inputLen > 0) {
    kprint("\r\033[2K");
    kprintln(msg.c_str());
    redrawPrompt();
  } else {
    kprintln(msg.c_str());
  }
}

void kprintln(const char *s) {
  kprint(s);
  kprintln();
}
ICACHE_FLASH_ATTR void kprintln() { kprint(F("\r\n")); }
ICACHE_FLASH_ATTR void kprintln(int n) {
  kprint(n);
  kprintln();
}
ICACHE_FLASH_ATTR void kprintln(unsigned long n) {
  if (redirectionFileIdx != -1) {
    char buf[16];
    ltoa(n, buf, 10);
    strncat(vfs[redirectionFileIdx].content, buf,
            CONTENT_LEN - strlen(vfs[redirectionFileIdx].content) - 1);
    kprint(F("\r\n"));
    return;
  }
  Serial.println(n);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (telnetClient && telnetClient.connected())
    telnetClient.println(n);
  if (sshClient && sshClient.connected())
    sshClient.println(n);
#endif
}
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
ICACHE_FLASH_ATTR void kprintln(IPAddress ip) {
  Serial.println(ip);
  if (telnetClient && telnetClient.connected())
    telnetClient.println(ip);
  if (sshClient && sshClient.connected())
    sshClient.println(ip);
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
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
  if (telnetClient && telnetClient.connected())
    telnetClient.println(s);
#endif
#if defined(ESP32)
  if (btEnabled)
    SerialBT.println(s);
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

void logError(const char *msg) {
  int found = -1;
  for (int j = 0; j < MAX_FILES; j++) {
    if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(vfs[j].name, "error.log") == 0 && strcmp(vfs[j].parentDir, "/sys/") == 0) {
      found = j;
      break;
    }
  }
  if (found != -1) {
    char entry[CONTENT_LEN];
    snprintf(entry, sizeof(entry), "[%lu] %s\n", millis()/1000, msg);
    size_t currentLen = strlen(vfs[found].content);
    strncat(vfs[found].content, entry, CONTENT_LEN - currentLen - 1);
  }
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

  for (i = 0; i < MAX_FILES; i++) {
    if (!(vfs[i].flags & FLAG_ACTIVE)) {
      strcpy(vfs[i].name, "error.log");
      strcpy(vfs[i].parentDir, "/sys");
      vfs[i].flags = FLAG_ACTIVE;
      vfs[i].mode = 0644;
      vfs[i].ownerId = 0;
      strcpy(vfs[i].content, "--- UniKernel Error Log ---\n");
      break;
    }
  }

  addDmesg(F("Kernel initialized"));
  addDmesg(F("Filesystem mounted"));
  addDmesg(F("Ready for commands"));
}

ICACHE_FLASH_ATTR void printPrompt() {
  bool currentAuth = serialAuthenticated || telnetAuthenticated;
  kprint(currentAuth ? F("root@") : F("guest@"));
  kprint(F(BOARD_NAME));
  kprintColor(CLR_WHT);
  kprint(F(":"));
  kprintColor(CLR_BLU);
  kprint(currentPath);
  kprintColor(CLR_RST);
  kprint(F("# "));
}

ICACHE_FLASH_ATTR void checkMemorySafeguard() {
  int free = freeMemory();
  if (free < 3000) {
    addDmesg(F("CRITICAL: OOM Killer Active!"));
    if (webEnabled) { webEnabled = false; webServer.stop(); addDmesg(F("Service Killed: Web")); }
    if (telnetEnabled) { telnetEnabled = false; telnetServer.stop(); addDmesg(F("Service Killed: Telnet")); }
    if (sshEnabled) { sshEnabled = false; sshServer.stop(); addDmesg(F("Service Killed: SSH")); }

    historyCount = 0;
    historyWriteIdx = 0;
  }
}

ICACHE_FLASH_ATTR void logResetReason() {
  String reason = ESP.getResetReason();
  File f = LittleFS.open("/crash.log", "a");
  if (f) {
    f.print("["); f.print(millis()/1000); f.print("] Reset: ");
    f.println(reason);
    f.close();
  }
}

ICACHE_FLASH_ATTR void setup() {
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  system_update_cpu_freq(160);
#endif
  Serial.begin(115200);
  delay(1500);
  while (Serial.available())
    Serial.read();
  Serial.println(F("\n\n[System] Booting UniKernel (160MHz Mode)..."));
  yield();

  pinMode(LED_BUILTIN, OUTPUT);
  int bootBtn = 0; 
  pinMode(bootBtn, INPUT_PULLUP);

  int presses = 0;
  unsigned long windowStart = millis();
  Serial.println(F("[Hardware] Checking for reset button presses (2s window)..."));

  while (millis() - windowStart < 2000) {
    if (digitalRead(bootBtn) == LOW) {
      delay(50); 
      if (digitalRead(bootBtn) == LOW) {
        presses++;
        digitalWrite(LED_BUILTIN, LOW); 
        delay(200);
        digitalWrite(LED_BUILTIN, HIGH); 
        while(digitalRead(bootBtn) == LOW) yield();
        windowStart = millis(); 
        Serial.print(F("Press detected: ")); Serial.println(presses);
      }
    }
    yield();
  }

  if (presses == 2) {
    loginFailCount = 0;
    isLockedOut = false;
    EEPROM.begin(4096);
    EEPROM.put(EEPROM_FAIL_COUNT_ADDR, (uint8_t)0);
    EEPROM.commit();
    Serial.println(F("[Hardware] Manual Unlock: FAIL_COUNT Reset."));
  } else if (presses >= 3) {
    EEPROM.begin(4096);
    EEPROM.put(EEPROM_PASS_ADDR, (char)0xFF); 
    EEPROM.put(EEPROM_FAIL_COUNT_ADDR, (uint8_t)0);
    EEPROM.commit();
    needsSetup = true;
    Serial.println(F("[Hardware] FACTORY RESET: Password Cleared."));
  }

  fastDigitalWrite(LED_BUILTIN, LOW);
  delay(100);
  fastDigitalWrite(LED_BUILTIN, HIGH);
  delay(100);
  fastDigitalWrite(LED_BUILTIN, LOW);
  delay(100);
  fastDigitalWrite(LED_BUILTIN, HIGH);

  initFS();
#if defined(ESP8266) || defined(ESP32)
  EEPROM.begin(4096);
#endif

  uint16_t magic;
  int addr = EEPROM_VFS_ADDR;
  EEPROM.get(addr, magic);
  if (magic == VFS_MAGIC) {
    EEPROM.get(addr + 2, vfs);
    addDmesg(F("Filesystem restored from EEPROM"));
  }

  Wire.begin();
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin();

  if (WiFi.SSID().length() > 0) {
    int wifiRetry = 40; 
    Serial.print(F("[Network] Connecting to stored WiFi ("));
    Serial.print(WiFi.SSID());
    Serial.print(F(")..."));
    while (WiFi.status() != WL_CONNECTED && wifiRetry > 0) {
      delay(500);
      Serial.print(F("."));
      wifiRetry--;
      yield();
    }
    Serial.println();
  } else {
    Serial.println(F("[Network] No stored WiFi credentials found."));
  }

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  ESP.wdtEnable(5000);
#endif

  char check;
  EEPROM.get(EEPROM_PASS_ADDR, check);

  if (check == 0xFF || check == 0x00) {
    needsSetup = true;
    addDmesg(F("Security Setup Required"));
  }

  uint8_t storedFails;
  EEPROM.get(EEPROM_FAIL_COUNT_ADDR, storedFails);
  if (storedFails != 0xFF)
    loginFailCount = storedFails;

  if (loginFailCount >= MAX_FAIL_COUNT) {
    isLockedOut = true;
    addDmesg(F("CRITICAL: Boot Lockout Active"));
  } else {
    EEPROM.put(EEPROM_LOCKOUT_ADDR, (unsigned long)0);
  }
#if defined(ESP8266) || defined(ESP32)
  EEPROM.commit();
#endif

  if (WiFi.status() == WL_CONNECTED) {
    addDmesg(F("WiFi Connected Successfully"));
    Serial.print(F("IP: "));
    Serial.println(WiFi.localIP());
  } else {
    addDmesg(F("WiFi Not Connected (Auto)"));
  }

  setupWebServer();
  LittleFS.begin();
  logResetReason(); 
  char otaHash[33];
  EEPROM.get(EEPROM_OTA_PASS_ADDR, otaHash);
  otaHash[32] = '\0';

  ArduinoOTA.setHostname("UniKernel-Node");
  if (otaHash[0] != 0xFF && otaHash[0] != 0x00) {
    ArduinoOTA.setPasswordHash(otaHash);
  } else {
    ArduinoOTA.setPassword("admin");
  }
  ArduinoOTA.onStart([]() { addDmesg(F("OTA: Starting Update")); });
  ArduinoOTA.onEnd([]() { addDmesg(F("OTA: Update Finished")); });
  ArduinoOTA.onError(
      [](ota_error_t error) { addDmesg(F("OTA: Error occurred")); });
  MDNS.begin("unikernel");

  static const uint8_t eckey[] PROGMEM = {
      0x30, 0x77, 0x02, 0x01, 0x01, 0x04, 0x20, 0x25, 0xe8, 0xec, 0x1e, 0x7e,
      0x5e, 0xd4, 0x54, 0x53, 0x6a, 0x80, 0xd0, 0xf3, 0xf8, 0x30, 0xe5, 0x36,
      0x1a, 0xb2, 0x35, 0xfb, 0x82, 0xd7, 0x4a, 0x82, 0x73, 0x73, 0x15, 0x4c,
      0x02, 0x49, 0xa2, 0xa0, 0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d,
      0x03, 0x01, 0x07, 0xa1, 0x44, 0x03, 0x42, 0x00, 0x04, 0x4d, 0x8f, 0x0c,
      0xd2, 0x99, 0xe6, 0x8d, 0xe6, 0xfb, 0xac, 0x8c, 0x5e, 0xfe, 0xa3, 0xe3,
      0x99, 0x4b, 0xc8, 0x0c, 0x16, 0x26, 0x5f, 0xa1, 0xa4, 0x12, 0xdd, 0x71,
      0x5c, 0x36, 0x8b, 0x3f, 0xe1, 0x9a, 0xe8, 0x4f, 0xfb, 0x2b, 0xbc, 0xd3,
      0x6d, 0xa7, 0x07, 0x36, 0xf3, 0xd5, 0xba, 0x0a, 0x7e, 0xba, 0x7d, 0xec,
      0xc3, 0x38, 0xd6, 0xca, 0xfb, 0x1c, 0xbf, 0x37, 0x44, 0x4a, 0x02, 0xcb,
      0xf1};
  static const uint8_t eccert[] PROGMEM = {
      0x30, 0x82, 0x01, 0x7e, 0x30, 0x82, 0x01, 0x23, 0xa0, 0x03, 0x02, 0x01,
      0x02, 0x02, 0x14, 0x32, 0x16, 0x17, 0x2b, 0xcb, 0x19, 0xd7, 0xd4, 0x80,
      0x34, 0xc2, 0x5c, 0x7d, 0x19, 0x72, 0x06, 0x73, 0xa0, 0x5d, 0xea, 0x30,
      0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02, 0x30,
      0x14, 0x31, 0x12, 0x30, 0x10, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x09,
      0x55, 0x6e, 0x69, 0x4b, 0x65, 0x72, 0x6e, 0x65, 0x6c, 0x30, 0x1e, 0x17,
      0x0d, 0x32, 0x36, 0x30, 0x35, 0x30, 0x37, 0x31, 0x32, 0x35, 0x38, 0x33,
      0x34, 0x5a, 0x17, 0x0d, 0x33, 0x36, 0x30, 0x35, 0x30, 0x34, 0x31, 0x32,
      0x35, 0x38, 0x33, 0x34, 0x5a, 0x30, 0x14, 0x31, 0x12, 0x30, 0x10, 0x06,
      0x03, 0x55, 0x04, 0x03, 0x0c, 0x09, 0x55, 0x6e, 0x69, 0x4b, 0x65, 0x72,
      0x6e, 0x65, 0x6c, 0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48,
      0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03,
      0x01, 0x07, 0x03, 0x42, 0x00, 0x04, 0x4d, 0x8f, 0x0c, 0xd2, 0x99, 0xe6,
      0x8d, 0xe6, 0xfb, 0xac, 0x8c, 0x5e, 0xfe, 0xa3, 0xe3, 0x99, 0x4b, 0xc8,
      0x0c, 0x16, 0x26, 0x5f, 0xa1, 0xa4, 0x12, 0xdd, 0x71, 0x5c, 0x36, 0x8b,
      0x3f, 0xe1, 0x9a, 0xe8, 0x4f, 0xfb, 0x2b, 0xbc, 0xd3, 0x6d, 0xa7, 0x07,
      0x36, 0xf3, 0xd5, 0xba, 0x0a, 0x7e, 0xba, 0x7d, 0xec, 0xc3, 0x38, 0xd6,
      0xca, 0xfb, 0x1c, 0xbf, 0x37, 0x44, 0x4a, 0x02, 0xcb, 0xf1, 0xa3, 0x53,
      0x30, 0x51, 0x30, 0x1d, 0x06, 0x03, 0x55, 0x1d, 0x0e, 0x04, 0x16, 0x04,
      0x14, 0xe8, 0x2b, 0x36, 0xf6, 0x7b, 0x6a, 0x0f, 0xd3, 0xf9, 0xd8, 0xfa,
      0xaa, 0x06, 0x6d, 0x2a, 0xe3, 0x50, 0xc7, 0x8b, 0xc8, 0x30, 0x1f, 0x06,
      0x03, 0x55, 0x1d, 0x23, 0x04, 0x18, 0x30, 0x16, 0x80, 0x14, 0xe8, 0x2b,
      0x36, 0xf6, 0x7b, 0x6a, 0x0f, 0xd3, 0xf9, 0xd8, 0xfa, 0xaa, 0x06, 0x6d,
      0x2a, 0xe3, 0x50, 0xc7, 0x8b, 0xc8, 0x30, 0x0f, 0x06, 0x03, 0x55, 0x1d,
      0x13, 0x01, 0x01, 0xff, 0x04, 0x05, 0x30, 0x03, 0x01, 0x01, 0xff, 0x30,
      0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02, 0x03,
      0x49, 0x00, 0x30, 0x46, 0x02, 0x21, 0x00, 0xa7, 0xbf, 0x32, 0xdb, 0x9e,
      0x7e, 0x10, 0x67, 0x1e, 0xbd, 0x58, 0x66, 0x28, 0x8d, 0xbc, 0x49, 0xc7,
      0x4a, 0x2f, 0x65, 0x7c, 0x06, 0xda, 0x25, 0xf2, 0x71, 0xca, 0x17, 0xb0,
      0x45, 0xae, 0x08, 0x02, 0x21, 0x00, 0xd6, 0x61, 0x36, 0x49, 0x0c, 0x65,
      0x0b, 0x70, 0xc3, 0x03, 0x6b, 0x5e, 0x25, 0x75, 0x70, 0x04, 0x4c, 0x12,
      0xe9, 0xfd, 0xdc, 0xe0, 0x4c, 0x8f, 0x36, 0x0f, 0xcd, 0x5d, 0x2b, 0x7f,
      0xf6, 0x59};

  sshServer.setECCert(new BearSSL::X509List(eccert, sizeof(eccert)),
                       BR_KEYTYPE_EC,
                       new BearSSL::PrivateKey(eckey, sizeof(eckey)));
  addDmesg(F("Secure Boot Complete (Optimized Mode)"));
#endif

  for (int t = 0; t < MAX_TASKS; t++) {
    taskTable[t].active = false;
    taskTable[t].executionCount = 0;
  }

  delay(500);
  char bootCmd[MAX_INPUT_LEN];
  strcpy_P(bootCmd, PSTR("neofetch"));
  parseAndExecute(bootCmd, MAX_INPUT_LEN, true);

  char bootFile[NAME_LEN];
  EEPROM.get(EEPROM_BOOT_FILE_ADDR, bootFile);
  bootFile[NAME_LEN - 1] = '\0'; 

  bool isValid = (bootFile[0] != 0xFF && bootFile[0] != '\0');
  if (isValid) {
    for (int i = 0; i < (int)strlen(bootFile); i++) {
      if (bootFile[i] < 32 || bootFile[i] > 126) { isValid = false; break; }
    }
  }

  if (!isValid) {
    strcpy(bootFile, "0rc.sh");
  }

  serialAuthenticated = false;
  addDmesg(F("System: Automated Boot Sequence Started"));

  Serial.println(F("\n[System] UniKernel Ready."));
}

ICACHE_FLASH_ATTR void setupWebServer() {
  webServer.collectHeaders("Host");
  webServer.on("/", HTTP_GET, []() {
    webServer.send_P(200, "text/html", DASHBOARD_HTML);
  });

  webServer.on("/api/gpio", HTTP_POST, []() {
    if (!webServer.hasArg("plain")) {
        webServer.send(400, "application/json", _OSTR("{\"error\":\"Bad Request\"}"));
        return;
    }
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, webServer.arg("plain"));
    if (error) {
        webServer.send(400, "application/json", _OSTR("{\"error\":\"Invalid JSON\"}"));
        return;
    }
    String pass = doc["pass"] | "";
    if (!checkWebAuth(pass, webServer.client().remoteIP())) {
        webServer.send(401, "application/json", _OSTR("{\"error\":\"Unauthorized or Rate-limited\"}"));
        return;
    }
    int pin = doc["pin"] | 2;
    int val = doc["val"] | 1;
    fastPinMode(pin, OUTPUT);
    fastDigitalWrite(pin, val);
    webServer.send(200, "application/json", _OSTR("{\"status\":\"success\"}"));
  });

  webServer.on("/api/stats", []() {
    if (webServer.hasArg("pass")) {
        if (!checkWebAuth(webServer.arg("pass"), webServer.client().remoteIP())) {
            webServer.send(401, "application/json", _OSTR("{\"error\":\"Unauthorized or Rate-limited\"}"));
            return;
        }
    } else {
        webServer.send(401, "application/json", _OSTR("{\"error\":\"Auth Required\"}"));
        return;
    }

    String json = "{\"free\":" + String(ESP.getFreeHeap()) +
                  ",\"up\":" + String(millis() / 1000);
#if defined(ESP32)
    json += ",\"bt\":" + String(btEnabled ? 1 : 0);
#endif
    json += "}";
    webServer.send(200, "application/json", json);
  });

  webServer.on("/api/bt", []() {
    if (!checkWebAuth(webServer.arg("pass"), webServer.client().remoteIP())) {
      webServer.send(401, "text/plain", "Unauthorized or Rate-limited");
      return;
    }

    webServer.send(403, "text/plain", "API Disabled: CSRF Hardening Active");
    return;
#if defined(ESP32)
    int val = webServer.arg("val").toInt();
    if (val == 1) {
      SerialBT.begin("UniKernel-Web");
      btEnabled = true;
    } else {
      SerialBT.end();
      btEnabled = false;
    }
    webServer.send(200, "text/plain", "OK");
#else
    webServer.send(400, "text/plain", "Not Supported");
#endif
  });

  webServer.begin();
}

ICACHE_FLASH_ATTR void processTriggers() {
  static unsigned long lastTrig = 0;
  if (millis() - lastTrig < 5000) return; 
  lastTrig = millis();

  for (int i = 0; i < MAX_TRIGS; i++) {
    if (!triggerTable[i].active) continue;
    int current = 0;
    if (strcmp(triggerTable[i].cond, "vcc") == 0) current = ESP.getVcc();
    else if (strcmp(triggerTable[i].cond, "temp") == 0) current = 25 + (millis() % 5);
    else if (strcmp(triggerTable[i].cond, "ram") == 0) current = freeMemory();

    bool fire = false;
    if (triggerTable[i].op == '<' && current < triggerTable[i].val) fire = true;
    if (triggerTable[i].op == '>' && current > triggerTable[i].val) fire = true;

    if (fire) {
      char buf[MAX_INPUT_LEN]; 
      strncpy(buf, triggerTable[i].action, MAX_INPUT_LEN - 1);
      buf[MAX_INPUT_LEN - 1] = '\0';
      addDmesg(F("Trigger Fired!"));

      executeCommand(buf, false);
    }
  }
}

void doTabCompletion() {
  if (inputLen == 0) return;
  inputBuffer[inputLen] = '\0';
  
  const char* cmds[] = {
    "ls", "cd", "pwd", "cat", "echo", "rm", "mkdir", "touch", "wifi", "accel", 
    "sys", "help", "clear", "reboot", "uptime", "free", "neofetch", "ping", 
    "ifconfig", "hwinfo", "top", "ps", "login", "logout", "df", "dmesg"
  };
  int numCmds = 26;
  
  int matches = 0;
  const char* match = NULL;
  for (int i = 0; i < numCmds; i++) {
    if (strncmp(inputBuffer, cmds[i], inputLen) == 0) {
      matches++;
      match = cmds[i];
    }
  }
  
  if (matches == 1) {
    while (inputLen > 0) { kprint(F("\b \b")); inputLen--; }
    strcpy(inputBuffer, match);
    inputLen = strlen(inputBuffer);
    kprint(inputBuffer);
    kprint(F(" ")); 
    inputBuffer[inputLen++] = ' ';
    inputBuffer[inputLen] = '\0';
  } else if (matches > 1) {
    kprintln();
    for (int i = 0; i < numCmds; i++) {
      if (strncmp(inputBuffer, cmds[i], strlen(inputBuffer)) == 0) {
        kprint(cmds[i]); kprint(F("  "));
      }
    }
    kprintln();
    printPrompt();
    kprint(inputBuffer);
  }
}

ICACHE_FLASH_ATTR void loop() {
  checkMemorySafeguard();
  processTriggers();

  if (webEnabled)
    webServer.handleClient();

  if (!accelStopRequested)
    webSocket.loop();

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (otaEnabled) {
    if (millis() > otaEndTime) {
      otaEnabled = false;
      addDmesg(F("OTA: Window Closed"));
    } else {
      ArduinoOTA.handle();
      MDNS.update();
    }
  }
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (sshEnabled && sshServer.hasClient()) {
    WiFiClientSecure c = sshServer.available();
    if (sshClient && sshClient.connected()) {
      c.println(F("Busy: Another SSH session active."));
      c.stop();
    } else {
      sshClient = c;
      sshAuthenticated = false; 
      ESP.wdtFeed(); 
      printPrompt();
      lastSSHActivity = millis();
    }
  }
#endif
#endif

  unsigned long now = millis();
  static uint8_t lastM = 99;
  time_t tNow = time(nullptr);
  struct tm *ti = localtime(&tNow);
  if (ti && ti->tm_year > 100 && ti->tm_min != lastM) {
    lastM = ti->tm_min;
    for (int i = 0; i < MAX_CRON; i++) {
      if (cronTable[i].active && cronTable[i].h == ti->tm_hour && cronTable[i].m == ti->tm_min) {
        char buf[MAX_INPUT_LEN];
        strncpy(buf, cronTable[i].cmd, MAX_INPUT_LEN - 1);
        executeCommand(buf, true);
      }
    }
  }
  for (int t = 0; t < MAX_TASKS; t++) {
    if (taskTable[t].active && (now - taskTable[t].lastRun >= taskTable[t].interval)) {
      taskTable[t].lastRun = now;
      taskTable[t].executionCount++;
      taskTable[t].func();
    }
  }

  static unsigned long lastMemCheck = 0;
  if (millis() - lastMemCheck > 10000) {
    lastMemCheck = millis();
    int freeRam = ESP.getFreeHeap();
    if (freeRam < 5000) {
      Serial.print(F("\n[!] WARNING: Low Memory (")); Serial.print(freeRam); Serial.println(F(" bytes)."));

      if (freeRam < 4000) {
        bool killed = false;
        for (int i = MAX_TASKS - 1; i >= 0; i--) {
          if (taskTable[i].active) {
            taskTable[i].active = false;
            Serial.print(F("[OOM] Killer: Process '")); Serial.print(taskTable[i].name); Serial.println(F("' terminated to free RAM."));
            addDmesg(F("OOM Killer activated"));
            killed = true;
            break; 
          }
        }
        if (!killed) {
           Serial.println(F("[OOM] Warning: No non-critical tasks to kill. System unstable."));
        }
      }
    }
  }

  static bool firstBoot = true;
  if (firstBoot) {
    firstBoot = false;
    char bootFile[NAME_LEN];
    EEPROM.get(EEPROM_BOOT_FILE_ADDR, bootFile);
    bootFile[NAME_LEN - 1] = '\0';
    if (bootFile[0] == 0xFF || bootFile[0] == '\0') strcpy(bootFile, "0rc.sh");

    char bootAction[NAME_LEN + 8];
    snprintf(bootAction, sizeof(bootAction), "sh %s", bootFile);
    kprintln(F("[System] Executing Boot Script..."));
    executeCommand(bootAction, true);
    printPrompt();
  }

  if (serialAuthenticated && isTimeout(lastSerialActivity, SESSION_TIMEOUT)) {
    serialAuthenticated = false;
    Serial.println(F("\nSerial session timeout. Logged out."));
    printPrompt();
  }

  if (sshAuthenticated && isTimeout(lastSSHActivity, SESSION_TIMEOUT)) {
    sshAuthenticated = false;
    if (sshClient && sshClient.connected()) {
        sshClient.println(F("\nSSH session timeout. Closing connection."));
        sshClient.stop();
    }
  }

  static char lastChar = 0;
  static bool inEscSeq = false;
  static int escState = 0;
  char c = 0;
  bool hasInput = false;
  bool fromSerial = false;

  redirectionFileIdx = -1; 

  if (Serial.available() > 0) {
    c = Serial.read();
    hasInput = true;
    fromSerial = true;
    isSSHInput = false;
  } 

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
  if (!hasInput && telnetEnabled && telnetServer.hasClient()) {
     WiFiClient tc = telnetServer.available();
     if (!telnetClient || !telnetClient.connected()) {
        telnetClient = tc;
        telnetAuthenticated = false;
        printPrompt();
     } else tc.stop();
  } 

  if (!hasInput && telnetEnabled && telnetClient && telnetClient.available() > 0) {
    c = telnetClient.read();
    hasInput = true;
    fromSerial = false;
    isSSHInput = false;
  }

  if (!hasInput && sshEnabled && sshClient && sshClient.available() > 0) {
    c = sshClient.read();
    hasInput = true;
    fromSerial = false;
    isSSHInput = true;
  }
#endif

  if (hasInput) {
    if (!fromSerial && isLockedOut) {
       kprintln(F("\n[!] Access Denied: Brute-force Lockout Active."));
       return;
    }

    if (!fromSerial && millis() - lastLoginAttempt < loginCooldown) {
      if (c >= 32 && c <= 126) Serial.print((char)c); 
      if (c == '\r' || c == '\n') {
        kprint(F("\rCooldown Active: "));
        kprint((loginCooldown - (millis() - lastLoginAttempt)) / 1000);
        kprintln(F("s left."));
      }
      return;
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
        bool currentAuth = fromSerial ? serialAuthenticated : (telnetAuthenticated || sshAuthenticated);

        char firstCmd[16] = {0};
        sscanf(inputBuffer, "%15s", firstCmd);
        bool isLogin = (strcmp(firstCmd, "login") == 0);
        bool isHelp = (strcmp(firstCmd, "help") == 0);
        bool isPasswd = (strcmp(firstCmd, "passwd") == 0);

        if (!currentAuth && !isLogin && !isHelp && !isPasswd) {
          kprintln(F("--- ACCESS DENIED ---"));
        } else {
          parseAndExecute(inputBuffer, MAX_INPUT_LEN, fromSerial);
        }

        if (inputLen > 0 && (historyCount == 0 || strcmp(inputBuffer, cmdHistory[(historyWriteIdx + MAX_HISTORY - 1) % MAX_HISTORY]) != 0)) {
          strncpy(cmdHistory[historyWriteIdx], inputBuffer, MAX_INPUT_LEN - 1);
          historyWriteIdx = (historyWriteIdx + 1) % MAX_HISTORY;
          if (historyCount < MAX_HISTORY) historyCount++;
        }
        historyViewIdx = -1;
        inputLen = 0;
        memset(inputBuffer, 0, sizeof(inputBuffer));
        printPrompt();
      } else {
        kprintln();
        printPrompt();
      }
    } else if (c == '\t') {
      doTabCompletion();
    } else {
      if (c == 0x1b) { inEscSeq = true; escState = 0; return; }
      if (inEscSeq) {
        if (escState == 0) {
          if (c == '[') { escState = 1; return; }
          else { inEscSeq = false; escState = 0; }
        } else if (escState == 1) {
          if (c == 'A' || c == 'B') {
             if (historyCount > 0) {
                if (c == 'A') {
                   if (historyViewIdx == -1) historyViewIdx = (historyWriteIdx + MAX_HISTORY - 1) % MAX_HISTORY;
                   else historyViewIdx = (historyViewIdx + MAX_HISTORY - 1) % MAX_HISTORY;
                } else {
                   if (historyViewIdx != -1) historyViewIdx = (historyViewIdx + 1) % MAX_HISTORY;
                }
                while (inputLen > 0) { kprint(F("\b \b")); inputLen--; }
                strncpy(inputBuffer, cmdHistory[historyViewIdx], MAX_INPUT_LEN - 1);
                inputLen = strlen(inputBuffer);
                kprint(inputBuffer);
             }
          }
          escState = 0; inEscSeq = false;
          return;
        }
      }

      lastChar = c;
      if (c == 8 || c == 127) {
        if (inputLen > 0) { inputLen--; inputBuffer[inputLen] = '\0'; kprint(F("\b \b")); }
      } else if (c >= 32 && c <= 126 && inputLen < MAX_INPUT_LEN - 1) {
        inputBuffer[inputLen] = c;
        inputLen++;
        kprint((char)c);
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

void safeConcatPath(char *base, const char *extra) {
  size_t len = strlen(base);
  if (len >= PATH_LEN - 1) return;

  if (len > 0 && base[len - 1] != '/' && extra[0] != '/') {
    if (len < PATH_LEN - 1) {
        strcat(base, "/");
        len++;
    }
  }

  size_t remaining = PATH_LEN - len - 1;
  if (remaining > 0) {
    strncat(base, extra, remaining);
  }
  base[PATH_LEN - 1] = '\0';
}

ICACHE_FLASH_ATTR void toLowercase(char *str) {
  int i;
  for (i = 0; str[i] != '\0'; i++) {
    if (str[i] >= 'A' && str[i] <= 'Z')
      str[i] = str[i] - 'A' + 'a';
  }
}

ICACHE_FLASH_ATTR bool checkPermission(int fileIdx, uint8_t action,
                                       bool fromSerial) {
  bool currentAuth = fromSerial ? serialAuthenticated : (telnetAuthenticated || sshAuthenticated);
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
  if (strcmp_P(cmd, PSTR("ifconfig")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("login")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("logout")) == 0)
    return true;
  return false;
}

ICACHE_FLASH_ATTR void expandVars(char *line, size_t maxLen) {
  char temp[MAX_INPUT_LEN];
  memset(temp, 0, sizeof(temp));
  char *p = line;
  char *t = temp;
  size_t remaining = MAX_INPUT_LEN - 1;

  while (*p && remaining > 0) {
    if (*p == '$') {
      p++;
      char key[ENV_KEY_LEN] = {0};
      int ki = 0;
      while (isalnum(*p) && ki < ENV_KEY_LEN - 1)
        key[ki++] = *p++;

      const char *valStr = nullptr;
      char autoBuf[16];
      bool isAuto = false;

      if (strcmp(key, "VCC") == 0) {
        snprintf(autoBuf, sizeof(autoBuf), "%d", ESP.getVcc());
        valStr = autoBuf;
        isAuto = true;
      } else if (strcmp(key, "TEMP") == 0) {
        snprintf(autoBuf, sizeof(autoBuf), "%d", 25 + (int)(millis() % 5));
        valStr = autoBuf;
        isAuto = true;
      } else if (strcmp(key, "RAM") == 0) {
        snprintf(autoBuf, sizeof(autoBuf), "%d", freeMemory());
        valStr = autoBuf;
        isAuto = true;
      } else {
        for (int i = 0; i < MAX_ENV; i++) {
          if (envTable[i].active && strcmp(envTable[i].key, key) == 0) {
            valStr = envTable[i].val;
            break;
          }
        }
      }

      if (valStr) {
        while (*valStr && remaining > 0) {
          *t++ = *valStr++;
          remaining--;
        }
      }
    } else {
      *t++ = *p++;
      remaining--;
    }
  }
  *t = '\0';
  strncpy(line, temp, maxLen - 1);
  line[maxLen - 1] = '\0';
}

ICACHE_FLASH_ATTR void parseAndExecute(char *line, size_t maxLen, bool fromSerial) {
  expandVars(line, maxLen);
  char *p = line;
  char *cmd_start = p;
  bool inQuotes = false;

  while (*p) {
    if (*p == '\"') {
      inQuotes = !inQuotes;
    } else if (!inQuotes) {
      if (*p == ';') {
        *p = '\0';
        executeCommand(cmd_start, fromSerial);
        cmd_start = p + 1;
        while (*cmd_start == ' ')
          cmd_start++;
      } else if (p[0] == '&' && p[1] == '&') {
        *p = '\0';
        executeCommand(cmd_start, fromSerial);
        p++;
        cmd_start = p + 1;
        while (*cmd_start == ' ')
          cmd_start++;
      }
    }
    p++;
  }
  if (*cmd_start && *cmd_start != ' ')
    executeCommand(cmd_start, fromSerial);
}

ICACHE_FLASH_ATTR void kPulse() {
  bool state = fastDigitalRead(LED_BUILTIN);
  fastDigitalWrite(LED_BUILTIN, !state);
  delay(30);
  fastDigitalWrite(LED_BUILTIN, state);
}

ICACHE_FLASH_ATTR void executeCommandInternal(char *line, bool fromSerial);

ICACHE_FLASH_ATTR void executeCommand(char *line, bool fromSerial) {
  int savedRedir = redirectionFileIdx;
  executeCommandInternal(line, fromSerial);
  redirectionFileIdx = savedRedir;
}

ICACHE_FLASH_ATTR void executeCommandInternal(char *line, bool fromSerial) {
  static int recursionDepth = 0;
  kPulse();

  char *cmd = line;
  char *args = NULL;
  int i, sp, pin, count;

  for (int i = 0; i < MAX_ALIAS; i++) {
    if (aliasTable[i].active && strncmp(line, aliasTable[i].name, strlen(aliasTable[i].name)) == 0) {

      if (recursionDepth >= 3) {
          kprintln(F("Error: Maximum alias recursion depth exceeded."));
          return;
      }
      char resolved[MAX_INPUT_LEN];
      char *space = strchr(line, ' ');
      if (space) {
        snprintf(resolved, MAX_INPUT_LEN, "%s %s", aliasTable[i].cmd, space + 1);
      } else {
        strncpy(resolved, aliasTable[i].cmd, MAX_INPUT_LEN - 1);
      }
      recursionDepth++;
      executeCommandInternal(resolved, fromSerial);
      recursionDepth--;
      return;
    }
  }

  char *redir = strchr(line, '>');
  if (redir)
    *redir = '\0';

  char *firstSpace = strchr(cmd, ' ');
  if (firstSpace) {
    *firstSpace = '\0';
    args = firstSpace + 1;
  } else {
    static char emptyArgs[] = "";
    args = emptyArgs;
  }

  cmd = kTrim(cmd);
  args = kTrim(args);
  stripQuotes(args);
  toLowercase(cmd);

  bool currentAuth = fromSerial ? serialAuthenticated : telnetAuthenticated;
  bool isLogin = (strcmp_P(cmd, PSTR("login")) == 0);
  bool isHelp = (strcmp_P(cmd, PSTR("help")) == 0);
  bool isPasswd = (strcmp_P(cmd, PSTR("passwd")) == 0);

  if (!currentAuth && !isLogin && !isHelp && shellDepth == 0 &&
      !isTelnetSafeCommand(cmd)) {
    if (!fromSerial) { 
      kprintln(F("--- ACCESS DENIED ---"));
      kprintln(F("System is protected. Please type: login [your_password]"));
      return;
    }
  }

  if (!currentAuth && shellDepth == 0) {
    if (strchr(args, ';') || strchr(args, '&') || strchr(args, '|') ||
        strchr(args, '`')) {
      kprintln(
          F("Error: Command injection characters detected. Access Denied."));
      return;
    }
  }

  if (redir) {
    if (!currentAuth && shellDepth == 0) {
      kprintln(F("Error: Redirection requires authentication."));
      return;
    }
    char *filename = redir + 1;
    while (*filename == ' ')
      filename++;

    int found = -1, empty = -1;
    for (int j = 0; j < MAX_FILES; j++) {
      if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(vfs[j].name, filename) == 0 &&
          strcmp(vfs[j].parentDir, currentPath) == 0) {
        found = j;
        break;
      }
      if (!(vfs[j].flags & FLAG_ACTIVE) && empty == -1)
        empty = j;
    }
    int target = (found != -1) ? found : empty;
    if (target != -1) {
      if (found != -1 && !checkPermission(found, 2, fromSerial)) {
        kprintln(F("Error: Permission denied for target file."));
        return;
      }
      if (found == -1) {
        if (!isValidFsName(filename)) {
          kprintln(F("Error: Invalid filename."));
          return;
        }
        strncpy(vfs[target].name, filename, NAME_LEN - 1);
        vfs[target].flags = FLAG_ACTIVE;
        vfs[target].mode = 0644;
        vfs[target].ownerId = currentAuth ? 0 : 1;
        strncpy(vfs[target].parentDir, currentPath, PATH_LEN - 1);
      }
      memset(vfs[target].content, 0, CONTENT_LEN);
      redirectionFileIdx = target;
    }
  }

  if (strcmp_P(cmd, PSTR("on")) == 0) {
    pin = atoi_safe(args);
    if (pin < 0 || pin > 19) {
      Serial.println(F("Error: Pin 0-19"));
      return;
    }
    fastPinMode(pin, OUTPUT);
    fastDigitalWrite(pin, HIGH);
    kprint(F("Pin "));
    kprint(pin);
    kprintln(F(" is now HIGH (ON)"));
    return;
  } else if (strcmp_P(cmd, PSTR("off")) == 0) {
    pin = atoi_safe(args);
    if (pin < 0 || pin > 19) {
      Serial.println(F("Error: Pin 0-19"));
      return;
    }
    fastPinMode(pin, OUTPUT);
    fastDigitalWrite(pin, LOW);
    kprint(F("Pin "));
    kprint(pin);
    kprintln(F(" is now LOW (OFF)"));
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
          kprint(vfs[j].ownerId == 0 ? F("root  ") : F("guest "));
          kprint((unsigned long)strlen(vfs[j].content));
          kprint(F(" "));
          if (vfs[j].flags & FLAG_ISDIR)
            kprintColor(CLR_BLU);
          kprint(vfs[j].name);
          if (vfs[j].flags & FLAG_ISDIR)
            kprint(F("/"));
          kprintColor(CLR_RST);
          kprintln();
        } else {
          if (vfs[j].flags & FLAG_ISDIR)
            kprintColor(CLR_BLU);
          kprint(vfs[j].name);
          if (vfs[j].flags & FLAG_ISDIR)
            kprint(F("/"));
          kprintColor(CLR_RST);
          kprint(F("  "));
        }
        empty = 0;
      }
    }

    if (strcmp(currentPath, "/dev/") == 0) {
      if (isLong)
        kprintln(F("crw-rw-rw- root null\ncrw-rw-rw- root led\ncrw-rw-rw- root "
                   "a0\ncrw-rw-rw- root a1\ncrw-rw-rw- root a2\ncrw-rw-rw- root "
                   "a3\ncrw-rw-rw- root a4\ncrw-rw-rw- root a5"));
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
      *filename = '\0';
      filename++;
      uint8_t newOwner = 255;
      if (strcmp_P(user, PSTR("root")) == 0)
        newOwner = 0;
      else if (strcmp_P(user, PSTR("guest")) == 0)
        newOwner = 1;

      if (newOwner == 255) {
        kprintln(F("User not found. Use: root, guest"));
        return;
      }

      int j, found = 0;
      for (j = 0; j < MAX_FILES; j++) {
        if ((vfs[j].flags & FLAG_ACTIVE) &&
            strcmp(filename, vfs[j].name) == 0 &&
            strcmp(vfs[j].parentDir, currentPath) == 0) {
          uint8_t currentUser =
              (fromSerial ? serialAuthenticated : telnetAuthenticated) ? 0 : 1;
          if (currentUser != 0) {
            kprintln(F("Only root can change ownership."));
          } else {
            vfs[j].ownerId = newOwner;
            kprintln(F("Ownership updated."));
          }
          found = 1;
          break;
        }
      }
      if (!found)
        kprintln(F("File not found."));
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
      kprintln(F("Invalid name. Use 1-9 printable chars without / or spaces."));
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
    if (!currentAuth) { kprintln(F("Auth required.")); return; }
    vfs[foundSlot].parentDir[PATH_LEN - 1] = '\0';
    vfs[foundSlot].flags = FLAG_ACTIVE;
    if (strcmp_P(cmd, PSTR("mkdir")) == 0) {
      vfs[foundSlot].flags |= FLAG_ISDIR;
      vfs[foundSlot].mode = 0755;
    } else {
      vfs[foundSlot].mode = 0644;
    }
    vfs[foundSlot].ownerId = 0; 
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
          char newPath[PATH_LEN];
          strncpy(newPath, searchPath, PATH_LEN - 1);
          newPath[PATH_LEN - 1] = '\0';
          safeConcatPath(newPath, vfs[j].name);
          strncpy(currentPath, newPath, PATH_LEN - 1);
          currentPath[PATH_LEN - 1] = '\0';
          found = 1;
          break;
        }
      }
      if (!found)
        kprintln(F("No dir."));
    }
  } else if (strcmp_P(cmd, PSTR("pwd")) == 0) {
    kprintln(currentPath);
  } else if (strcmp_P(cmd, PSTR("sh")) == 0) {
    int j, found = 0;
    for (j = 0; j < MAX_FILES; j++) {
      if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(args, vfs[j].name) == 0 &&
          strcmp(vfs[j].parentDir, currentPath) == 0) {
        runScript(vfs[j].content);
        found = 1;
        break;
      }
    }
    if (!found)
      kprintln(F("Script not found."));
  } else if (strcmp_P(cmd, PSTR("echo")) == 0) {
    int arrow = indexOf(args, " > ");
    if (arrow != -1) {
      args[arrow] = '\0';
      char *text = args;
      char *filename = args + arrow + 3;
      while (*filename == ' ')
        filename++;

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
      } else if (strcmp_P(args, PSTR("temp")) == 0) {

        kprintln(25 + (millis() % 5)); 
        return;
      } else if (strcmp_P(args, PSTR("vcc")) == 0) {
        kprintln(ESP.getVcc());
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
    if (args[0] == '\0') {
      kprintln(F("UniKernel OS v6.14.0"));
      kprint(F("RAM Free: "));
      kprintln(freeMemory());
      kprintln(F("Automation: Use 'rc.sh' for auto-start scripts."));
      kprintln(F("Example: echo \"telnet on\" > rc.sh"));
      return;
    }
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
        kprintln((vfs[j].flags & FLAG_ISDIR) ? F("Directory") : F("File"));
        kprint(F("Size: "));
        kprint((unsigned long)strlen(vfs[j].content));
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
      *dst = '\0';
      dst++;
      int sIdx = -1, dIdx = -1, empty = -1;
      for (int j = 0; j < MAX_FILES; j++) {
        if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(src, vfs[j].name) == 0 &&
            strcmp(vfs[j].parentDir, currentPath) == 0)
          sIdx = j;
        if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(dst, vfs[j].name) == 0 &&
            strcmp(vfs[j].parentDir, currentPath) == 0)
          dIdx = j;
        if (!(vfs[j].flags & FLAG_ACTIVE) && empty == -1)
          empty = j;
      }
      if (sIdx != -1) {
        if (!currentAuth) { kprintln(F("Auth required.")); return; }
        if (strcmp_P(cmd, PSTR("cp")) == 0) {
          if (empty != -1) {
            vfs[empty] = vfs[sIdx];
            strncpy(vfs[empty].name, dst, NAME_LEN - 1);
            vfs[empty].name[NAME_LEN - 1] = '\0';
            kprintln(F("Copied."));
          } else
            kprintln(F("FS full."));
        } else {
          strncpy(vfs[sIdx].name, dst, NAME_LEN - 1);
          vfs[sIdx].name[NAME_LEN - 1] = '\0';
          kprintln(F("Moved."));
        }
      } else
        kprintln(F("Source not found."));
    } else
      kprintln(F("Usage: cp/mv [src] [dst]"));
  } else if (strcmp_P(cmd, PSTR("append")) == 0) {
    char *file = args;
    char *text = strchr(args, ' ');
    if (text) {
      *text = '\0';
      text++;
      while (*text == ' ')
        text++;

      int j, found = 0;
      for (j = 0; j < MAX_FILES; j++) {
        if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(file, vfs[j].name) == 0 &&
            strcmp(vfs[j].parentDir, currentPath) == 0) {
          int curLen = strlen(vfs[j].content);
          int addLen = strlen(text);
          if (curLen + addLen < CONTENT_LEN - 1) {
            strncat(vfs[j].content, text, CONTENT_LEN - curLen - 1);
            kprintln(F("Appended."));
          } else
            kprintln(F("File full."));
          found = 1;
          break;
        }
      }
      if (!found)
        kprintln(F("File not found."));
    } else
      kprintln(F("Usage: append [file] [text]"));
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
#if defined(ARDUINO_ARCH_AVR)
    kprint(F("Bare-Metal Ticks: "));
    kprintln((unsigned long)system_ticks);
#endif
    addDmesg(F("uptime command"));
  } else if (strcmp_P(cmd, PSTR("df")) == 0 ||
             strcmp_P(cmd, PSTR("free")) == 0) {
    kprint(F("RAM  - Free: "));
    kprint(freeMemory());
    kprintln(F(" bytes"));
    int usedFiles = 0;
    for (int j = 0; j < MAX_FILES; j++)
      if (vfs[j].flags & FLAG_ACTIVE)
        usedFiles++;
    kprint(F("VFS  - Used: "));
    kprint(usedFiles);
    kprint(F("/"));
    kprintln(MAX_FILES);
    kprint(F("LFS  - Free: "));
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
    FSInfo fs_info;
    LittleFS.info(fs_info);
    kprint(fs_info.totalBytes - fs_info.usedBytes);
    kprintln(F(" bytes"));
#else
    kprintln(F("N/A"));
#endif
  } else if (strcmp_P(cmd, PSTR("whoami")) == 0) {
    kprintln(currentAuth ? F("root") : F("guest"));
  } else if (strcmp_P(cmd, PSTR("uname")) == 0) {
    kprint(F("UniKernel ("));
    kprint(BOARD_NAME);
    kprintln(F(")"));
    kprint(F("Kernel: "));
    kprintln(ESP.getSdkVersion());
    kprint(F("Core: "));
    kprintln(ESP.getCoreVersion());
    kprint(F("CPU: "));
    kprint(ESP.getCpuFreqMHz());
    kprintln(F(" MHz"));
  } else if (strcmp_P(cmd, PSTR("hwinfo")) == 0) {
    kprintColor(CLR_CYN);
    kprintln(F("--- HARDWARE INFORMATION ---"));
    kprintColor(CLR_WHT);
    kprint(F("Flash Chip ID: "));
    kprintln((unsigned long)ESP.getFlashChipId());
    kprint(F("Flash Real Size: "));
    kprint((unsigned long)ESP.getFlashChipRealSize() / 1024);
    kprintln(F(" KB"));
    kprint(F("Flash Speed: "));
    kprint((unsigned long)ESP.getFlashChipSpeed() / 1000000);
    kprintln(F(" MHz"));
    kprint(F("Free Heap: "));
    kprint((unsigned long)ESP.getFreeHeap());
    kprintln(F(" bytes"));
    kprint(F("Sketch Size: "));
    kprint((unsigned long)ESP.getSketchSize());
    kprintln(F(" bytes"));
    kprint(F("Free Sketch: "));
    kprint((unsigned long)ESP.getFreeSketchSpace());
    kprintln(F(" bytes"));
    kprint(F("Chip VCC: "));
    kprint(ESP.getVcc());
    kprintln(F(" mV"));
    kprint(F("Reset: "));
    kprintln(ESP.getResetReason());
    kprint(F("Flash Mode: "));
    FlashMode_t mode = ESP.getFlashChipMode();
    kprintln(mode == FM_QIO ? "QIO" : (mode == FM_DIO ? "DIO" : "Other"));
#if defined(ESP32)
    kprint(F("Bluetooth: "));
    kprintln(btEnabled ? F("Online") : F("Offline"));
#endif
  } else if (strcmp_P(cmd, PSTR("accel")) == 0) {
    handleAccelCommand(args);
  } else if (strcmp_P(cmd, PSTR("sleep")) == 0) {
    int sec = atoi_safe(args);
    if (sec <= 0)
      sec = 5;
    kprint(F("Sleep "));
    kprint(sec);
    kprintln(F("s..."));
    delay(100);
    WiFi.mode(WIFI_OFF);
    delay(sec * 1000);
    WiFi.mode(WIFI_STA);
    WiFi.begin();
    kprintln(F("Woke up."));
  } else if (strcmp_P(cmd, PSTR("delay")) == 0) {
    int ms = atoi_safe(args);
    if (ms > 0)
      delay(ms);
  } else if (strcmp_P(cmd, PSTR("cpu")) == 0) {
    int freq = atoi_safe(args);
    if (freq == 80 || freq == 160) {
      system_update_cpu_freq(freq);
      kprint(CLR_YLW);
      kprint(F("CPU Frequency set to "));
      kprint(freq);
      kprintln(F(" MHz"));
      kprint(CLR_RST);
    } else {
      kprintln(F("Usage: cpu [80/160]"));
    }
  } else if (strcmp_P(cmd, PSTR("deepsleep")) == 0) {
    int sec = atoi_safe(args);
    if (sec <= 0) sec = 10;
    kprint(F("Deep Sleep for ")); kprint(sec); kprintln(F(" seconds..."));
    delay(500);
    ESP.deepSleep(sec * 1000000);
  } else if (strcmp_P(cmd, PSTR("reboot")) == 0) {
    kprintln(F("Rebooting system..."));
    delay(500);
    ESP.restart();
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
    kprintln(F("Files: ls, cd, pwd, mkdir, touch, cat, echo, append, cp, mv, "
               "rm, info, save, load, lfs, chown, chmod"));
    kprintln(F("Hardw: on, off, gpio, pinmode, write, read, pwm, i2c"));
    kprintln(F("Net  : wifi, bt, ifconfig, ping, wget, ntp, telnet, web, ssh, "
               "netstat, accel"));
    kprintln(F("Sys  : ps, top, sys [diag/audit/backup], date, uptime, uname, hwinfo, neofetch, cpu, sleep, dmesg, free, clear, reboot, boot, df, whoami"));
    kprintln(F("Secur: login, logout, passwd, firewall, ota, color, export, "
               "env, sh, cron, delay, kill, bg"));
  } else if (strcmp_P(cmd, PSTR("top")) == 0) {
    for (int i = 0; i < 5; i++) {
      kprint(F("\x1b[2J\x1b[H")); 
      kprintColor(CLR_CYN);
      kprintln(F("--- UniKernel System Monitor ---"));
      kprintColor(CLR_WHT);
      kprint(F("Uptime: ")); kprint(millis() / 1000); kprint(F("s  "));
      kprint(F("Load: ")); kprint(ESP.getHeapFragmentation()); kprintln(F("%"));
      kprint(F("Memory: ")); kprint(ESP.getFreeHeap()); kprintln(F(" bytes free"));
      kprintln(F("--------------------------------"));
      kprintln(F("PID  SERVICE/TASK   STATUS"));

      if (webEnabled) kprintln(F("S1   web_server     RUNNING"));
      if (telnetEnabled) kprintln(F("S2   telnet_daemon  RUNNING"));
      if (sshEnabled) kprintln(F("S3   ssh_daemon     RUNNING"));
      if (otaEnabled) kprintln(F("S4   ota_service    READY"));

      for (int t = 0; t < MAX_TASKS; t++) {
        if (taskTable[t].active) {
          kprint(t + 1);
          kprint(F("    "));
          kprint(taskTable[t].name);
          kprintln(F("          ACTIVE"));
        }
      }
      kprintln(F("--------------------------------"));
      kprintln(F("Press Enter on Serial to exit monitor."));
      delay(1000);
      ESP.wdtFeed();
      yield();
    }
  } else if (strcmp_P(cmd, PSTR("ota")) == 0) {
    if (strcmp_P(args, PSTR("on")) == 0) {
      otaEnabled = true;
      otaEndTime = millis() + OTA_WINDOW;
      ArduinoOTA.begin();
      kprintln(F("OTA Enabled for 5 minutes. Port: 8266"));
      addDmesg(F("OTA Enabled via Shell"));
    } else if (strncmp_P(args, PSTR("setpass "), 8) == 0) {
      char newPass[16];
      strncpy(newPass, args + 8, 15);
      newPass[15] = '\0';

      MD5Builder md5;
      md5.begin();
      md5.add(newPass);
      md5.calculate();
      String hash = md5.toString();

      char otaHash[33];
      strncpy(otaHash, hash.c_str(), 32);
      otaHash[32] = '\0';

      EEPROM.put(EEPROM_OTA_PASS_ADDR, otaHash);
      EEPROM.commit();
      ArduinoOTA.setPasswordHash(otaHash);
      kprintln(F("OTA Password updated and hashed in EEPROM."));
    } else {
      kprintln(F("Usage: ota [on/setpass new_password]"));
    }
  } else if (strcmp_P(cmd, PSTR("firewall")) == 0) {
    if (strncmp_P(args, PSTR("allow "), 6) == 0) {
      strncpy(whitelistIP, args + 6, 15);
      kprint(F("Firewall: Only allowing "));
      kprintln(whitelistIP);
    } else if (strcmp_P(args, PSTR("reset")) == 0) {
      whitelistIP[0] = '\0';
      kprintln(F("Firewall disabled (All IPs allowed)."));
    } else {
      kprint(F("Current Whitelist: "));
      kprintln(strlen(whitelistIP) > 0 ? whitelistIP : "None");
      kprintln(F("Usage: firewall [allow IP / reset]"));
    }
  } else if (strcmp_P(cmd, PSTR("ssh")) == 0) {
    if (strcmp_P(args, PSTR("on")) == 0) {
      system_update_cpu_freq(160);
      sshEnabled = true;
      sshServer.setBufferSizes(4096, 4096);
      sshServer.begin();
      kprintln(F("SSH Boosted to 160MHz. (Wait 5-10s)"));
    } else if (strcmp_P(args, PSTR("off")) == 0) {
      sshEnabled = false;
      sshServer.stop();
      kprintln(F("SSH Server Disabled."));
    }
  } else if (strcmp_P(cmd, PSTR("lfs")) == 0) {
    if (strncmp_P(args, PSTR("ls"), 2) == 0) {
      Dir dir = LittleFS.openDir("/");
      while (dir.next()) {
        kprint(dir.fileName());
        kprint(F("  "));
        kprintln((unsigned long)dir.fileSize());
      }
    } else if (strncmp_P(args, PSTR("format"), 6) == 0) {
      LittleFS.format();
      kprintln(F("LFS Formatted."));
    } else if (strncmp_P(args, PSTR("write "), 6) == 0) {
      File f = LittleFS.open("/data.txt", "a");
      if (f) {
        f.println(args + 6);
        f.close();
        kprintln(F("Written to LFS."));
      }
    } else {
      kprintln(F("Usage: lfs [ls/format/write text]"));
    }
  } else if (strcmp_P(cmd, PSTR("neofetch")) == 0) {
    kprintColor(CLR_YLW);
    kprintln(F("       .---.          root@unikernel"));
    kprintColor(CLR_YLW);
    kprintln(F("      /     \\         --------------"));
    kprintColor(CLR_YLW);
    kprint(F("     |  (O)  |        "));
    kprintColor(CLR_WHT);
    kprintln(F("OS: UniKernel x86_esp"));
    kprintColor(CLR_YLW);
    kprint(F("      \\     /         "));
    kprintColor(CLR_WHT);
    kprint(F("Host: "));
    kprintln(BOARD_NAME);
    kprintColor(CLR_YLW);
    kprint(F("       '---'          "));
    kprintColor(CLR_WHT);
    kprintln(F("Kernel: 6.14.0-unikernel"));
    kprintColor(CLR_YLW);
    kprint(F("     /|     |\\        "));
    kprintColor(CLR_WHT);
    kprint(F("Uptime: "));
    kprint(millis() / 1000);
    kprintln(F("s"));
    kprintColor(CLR_YLW);
    kprint(F("    / |     | \\       "));
    kprintColor(CLR_WHT);
    kprintln(F("Shell: UniShell 2.0"));
    kprintColor(CLR_YLW);
    kprint(F("   /  |     |  \\      "));
    kprintColor(CLR_WHT);
    kprint(F("Memory: "));
    kprint(freeMemory());
    kprintln(F(" free"));
    kprintColor(CLR_YLW);
    kprint(F("  '---'-----'---'     "));
    kprintColor(CLR_WHT);
    kprint(F("VFS: "));
    kprint(MAX_FILES);
    kprintln(F(" slots"));
    kprintln(F(""));
    kprint(F("  "));
    kprintColor(CLR_RED);
    kprint(F("### "));
    kprintColor(CLR_GRN);
    kprint(F("### "));
    kprintColor(CLR_YLW);
    kprint(F("### "));
    kprintColor(CLR_BLU);
    kprint(F("### "));
    kprintColor(CLR_MAG);
    kprint(F("### "));
    kprintColor(CLR_CYN);
    kprint(F("### "));
    kprintColor(CLR_WHT);
    kprintln(F("###"));
    kprintColor(CLR_RST);
  } else if (strcmp_P(cmd, PSTR("export")) == 0) {
    char *key = args;
    char *val = strchr(args, '=');
    if (val) {
      *val = '\0';
      val++;
      int found = -1;
      for (int i = 0; i < MAX_ENV; i++) {
        if (envTable[i].active && strcmp(envTable[i].key, key) == 0) {
          found = i;
          break;
        }
        if (!envTable[i].active && found == -1)
          found = i;
      }
      if (found != -1) {
        strncpy(envTable[found].key, key, ENV_KEY_LEN - 1);
        strncpy(envTable[found].val, val, ENV_VAL_LEN - 1);
        envTable[found].active = true;
        kprintln(F("Var set."));
      } else
        kprintln(F("Env full."));
    } else
      kprintln(F("Usage: export key=val"));
  } else if (strcmp_P(cmd, PSTR("env")) == 0) {
    for (int i = 0; i < MAX_ENV; i++) {
      if (envTable[i].active) {
        kprint(envTable[i].key);
        kprint(F("="));
        kprintln(envTable[i].val);
      }
    }
  } else if (strcmp_P(cmd, PSTR("ntp")) == 0) {
    if (!checkWiFi())
      return;
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
          while (*cPtr == ' ')
            cPtr++;
          int found = -1;
          for (int i = 0; i < MAX_CRON; i++)
            if (!cronTable[i].active) {
              found = i;
              break;
            }
          if (found != -1) {
            cronTable[found].h = h;
            cronTable[found].m = m;
            strncpy(cronTable[found].cmd, cPtr, 31);
            cronTable[found].active = true;
            kprintln(F("Cron added."));
          } else
            kprintln(F("Cron full."));
        }
      }
    } else if (strcmp_P(args, PSTR("list")) == 0) {
      for (int i = 0; i < MAX_CRON; i++) {
        if (cronTable[i].active) {
          kprint(i);
          kprint(F("> "));
          kprint(cronTable[i].h);
          kprint(F(":"));
          if (cronTable[i].m < 10)
            kprint(F("0"));
          kprint(cronTable[i].m);
          kprint(F(" -> "));
          kprintln(cronTable[i].cmd);
        }
      }
    } else if (strncmp_P(args, PSTR("rm "), 3) == 0) {
      int id = atoi_safe(args + 3);
      if (id >= 0 && id < MAX_CRON) {
        cronTable[id].active = false;
        kprintln(F("Removed."));
      }
    } else {
      kprintln(F("Usage: cron [add HH:MM cmd / list / rm ID]"));
    }
  } else if (strcmp_P(cmd, PSTR("alias")) == 0) {
    char *name = args;
    char *val = strchr(args, '=');
    if (val) {
      *val = '\0';
      val++;
      int found = -1;
      for (int i = 0; i < MAX_ALIAS; i++) {
        if (aliasTable[i].active && strcmp(aliasTable[i].name, name) == 0) { found = i; break; }
        if (!aliasTable[i].active && found == -1) found = i;
      }
      if (found != -1) {
        strncpy(aliasTable[found].name, name, NAME_LEN - 1);
        strncpy(aliasTable[found].cmd, val, 31);
        aliasTable[found].active = true;
        kprintln(F("Alias set."));
      } else kprintln(F("Alias table full."));
    } else {
      for (int i = 0; i < MAX_ALIAS; i++) {
        if (aliasTable[i].active) {
          kprint(aliasTable[i].name); kprint(F("='")); kprint(aliasTable[i].cmd); kprintln(F("'"));
        }
      }
    }
  } else if (strcmp_P(cmd, PSTR("trigger")) == 0) {

    char cond[16], opStr[2], act[32]; int val;
    if (sscanf(args, "%15s %1s %d %31s", cond, opStr, &val, act) == 4) {
      int found = -1;
      for (int i=0; i<MAX_TRIGS; i++) if (!triggerTable[i].active) { found = i; break; }
      if (found != -1) {

        strncpy(triggerTable[found].cond, cond, sizeof(triggerTable[found].cond) - 1);
        triggerTable[found].cond[sizeof(triggerTable[found].cond) - 1] = '\0';

        triggerTable[found].op = opStr[0];
        triggerTable[found].val = val;

        strncpy(triggerTable[found].action, act, sizeof(triggerTable[found].action) - 1);
        triggerTable[found].action[sizeof(triggerTable[found].action) - 1] = '\0';

        triggerTable[found].active = true;
        kprintln(F("Trigger registered safely."));
      } else kprintln(F("Table full."));
    } else {
      for (int i=0; i<MAX_TRIGS; i++) if (triggerTable[i].active) {
        kprint(triggerTable[i].cond); kprint(triggerTable[i].op); 
        kprint(triggerTable[i].val); kprint(F(" -> ")); kprintln(triggerTable[i].action);
      }
    }
  } else if (strcmp_P(cmd, PSTR("color")) == 0) {
    if (strcmp_P(args, PSTR("on")) == 0) {
      useColor = true;
      kprintln(F("Color UI Enabled."));
    } else {
      useColor = false;
      kprintln(F("Color UI Disabled."));
    }
  } else if (strcmp_P(cmd, PSTR("login")) == 0) {
    if (args[0] == '\0') {
      kprintln(F("Usage: login [pass]"));
      return;
    }
    if (fromSerial && isLockedOut) {
       isLockedOut = false;
       loginFailCount = 0;
    }

    if (isLockedOut) {
      kprintln(F("CRITICAL: System Locked due to Brute-Force."));
      return;
    }

    if (needsSetup) {
      kprintln(
          F("SECURITY ERROR: Device uninitialized. Use 'passwd' via Serial."));
      return;
    }

    char savedPass[17];
    char hashedInput[17];
    EEPROM.get(EEPROM_PASS_ADDR, savedPass);
    hashPass(args, hashedInput);

    if (secureEquals(hashedInput, savedPass, 16)) {
      if (fromSerial) {
        serialAuthenticated = true;
        lastSerialActivity = millis();
      } else if (isSSHInput) {
        sshAuthenticated = true;
        lastSSHActivity = millis();
      } else {
        telnetAuthenticated = true;
        lastTelnetActivity = millis();
      }
      loginFailCount = 0;
      EEPROM.put(EEPROM_FAIL_COUNT_ADDR, (uint8_t)0);
      EEPROM.commit();
      kprintln(_OSTR("Login Successful."));
      addDmesgRam(_OSTR("User logged in"));
    } else {
      loginFailCount++;
      EEPROM.put(EEPROM_FAIL_COUNT_ADDR, loginFailCount);
      EEPROM.commit();
      if (loginFailCount >= MAX_FAIL_COUNT) {
        isLockedOut = true;
        kprintln(_OSTR("CRITICAL: System Locked due to Brute-Force."));
        addDmesgRam(_OSTR("System hard locked!"));
      } else {
        addDmesgRam(_OSTR("Login failed!"));
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
  } else if (strcmp_P(cmd, PSTR("logout")) == 0 || strcmp_P(cmd, PSTR("exit")) == 0) {
    if (fromSerial) {
      serialAuthenticated = false;
      kprintln(F("Logged out."));
    } else if (isSSHInput) {
      sshAuthenticated = false;
      sshClient.println(F("Logged out. Closing SSH session."));
      sshClient.stop();
    } else {
      telnetAuthenticated = false;
      telnetClient.println(F("Logged out. Closing Telnet session."));
      telnetClient.stop();
    }
  } else if (strcmp_P(cmd, PSTR("passwd")) == 0) {
    if (args[0] == '\0' || strlen(args) < 4) {
      kprintln(F("Usage: passwd [min 4 chars]"));
      return;
    }
    char hashedPass[17];
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
      int retry = 5;
      while (WiFi.status() != WL_CONNECTED && retry > 0) {
        delay(1000);
        retry--;
        yield();
      }
      telnetEnabled = true;
      telnetServer.begin();
      kprintln(F("NetShell (Telnet) Enabled."));
      kprint(F("Listening on: "));
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
      kprintln(WiFi.localIP());
#else
      kprintln(F("Serial only (No WiFi)"));
#endif
    } else if (strcmp_P(args, PSTR("off")) == 0) {
      telnetEnabled = false;
      if (telnetClient)
        telnetClient.stop();
      telnetServer.stop();
      kprintln(F("NetShell (Telnet) Disabled."));
    }
  } else if (strcmp_P(cmd, PSTR("ps")) == 0) {
    if (!currentAuth) { kprintln(F("Auth required.")); return; }
    kprintln(F("PID  NAME      INTERVAL  COUNT     STATUS"));
    kprintln(F("0    kernel    0         -         RUNNING"));
    for (int t = 0; t < MAX_TASKS; t++) {
      if (taskTable[t].active) {
        kprint(t + 1);
        kprint(F("    "));
        kprint(taskTable[t].name);
        kprint(F("    "));
        kprint(taskTable[t].interval);
        kprint(F("       "));
        kprint(taskTable[t].executionCount);
        kprint(F("         "));
        kprintln(F("ACTIVE"));
      }
    }
    kprintln(F("Free Heap: "));
    kprintln(freeMemory());
  } else if (strcmp_P(cmd, PSTR("netstat")) == 0) {
    if (!currentAuth) { kprintln(F("Auth required.")); return; }
    kprintln(F("Active Services:"));
    kprint(F("Telnet : "));
    kprintln(telnetEnabled ? F("ON (Port 23)") : F("OFF"));
    kprint(F("Web    : "));
    kprintln(webEnabled ? F("ON (Port 80)") : F("OFF"));
    kprint(F("SSH    : "));
    kprintln(sshEnabled ? F("ON (Port 22)") : F("OFF"));
  } else if (strcmp_P(cmd, PSTR("sys")) == 0) {
    if (strcmp_P(args, PSTR("diagnosis")) == 0) {
      kprintln(F("--- UniKernel Performance Diagnosis ---"));
      uint32_t start = micros();
      if (!accelStopRequested) webSocket.loop();
      uint32_t wsTime = micros() - start;
      start = micros();
      MDNS.update();
      uint32_t mdnsTime = micros() - start;
      uint32_t webTime = 0;
      if (webEnabled) {
        start = micros();
        webServer.handleClient();
        webTime = micros() - start;
      }
      kprint(F("WebSocket Loop : ")); kprint(wsTime); kprintln(F(" us"));
      kprint(F("mDNS Update    : ")); kprint(mdnsTime); kprintln(F(" us"));
      kprint(F("Web Server     : ")); kprint(webTime); kprintln(F(" us"));
      kprint(F("Free Memory    : ")); kprint(ESP.getFreeHeap()); kprintln(F(" bytes"));
      kprint(F("Fragmentation  : ")); kprint(ESP.getHeapFragmentation()); kprintln(F("%"));
      if (wsTime > 5000 || mdnsTime > 5000 || webTime > 10000) {
        kprintColor(CLR_RED);
        kprintln(F("[!] Bottleneck detected in network services."));
        kprintColor(CLR_RST);
      } else {
        kprintColor(CLR_GRN);
        kprintln(F("[+] Core loop performance is optimal."));
        kprintColor(CLR_RST);
      }
    } else if (strcmp_P(args, PSTR("audit")) == 0) {
      kprintln(F("--- UniKernel Security & Integrity Audit ---"));
      int score = 100;
      char check; EEPROM.get(EEPROM_PASS_ADDR, check);
      if (check == 0xFF || check == 0x00) { kprintln(F("[!] Security: Root password NOT set! (-40)")); score -= 40; }
      else kprintln(F("[+] Security: Root password is active."));
      if (strlen(whitelistIP) == 0) { kprintln(F("[!] Security: Firewall is OPEN (No whitelist). (-20)")); score -= 20; }
      else kprintln(F("[+] Security: Firewall active (Whitelist enabled)."));
      if (telnetEnabled) { kprintln(F("[!] Privacy: Telnet is enabled (Insecure). (-10)")); score -= 10; }
      if (freeMemory() < 10000) { kprintln(F("[!] Resources: Memory is low (<10KB). (-15)")); score -= 15; }
      if (otaEnabled) kprintln(F("[!] System: OTA update window is active."));
      kprint(F("System Security Score: "));
      if (score > 80) kprintColor(CLR_GRN);
      else if (score > 50) kprintColor(CLR_YLW);
      else kprintColor(CLR_RED);
      kprint(score); kprintln(F("/100")); kprintColor(CLR_RST);
    } else if (strcmp_P(args, PSTR("backup")) == 0) {
      kprintln(F("--- UniKernel VFS Backup Script ---"));
      for (int i = 0; i < MAX_FILES; i++) {
        if ((vfs[i].flags & FLAG_ACTIVE) && !(vfs[i].flags & FLAG_ISDIR)) {
          kprint(F("echo \"")); kprint(vfs[i].content);
          kprint(F("\" > ")); kprintln(vfs[i].name);
        }
      }
      kprintln(F("--- End Backup ---"));
    } else if (strcmp_P(args, PSTR("speed")) == 0) {
      kprintln(F("Measuring system loop frequency..."));
      unsigned long start = millis(); unsigned long cycles = 0;
      while (millis() - start < 1000) { yield(); cycles++; }
      kprint(F("Loop frequency: ")); kprint(cycles); kprintln(F(" Hz"));
    } else {
      kprintln(F("UniKernel System Utility"));
      kprintln(F("Usage: sys [diagnosis | audit | backup | speed]"));
    }
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
  } else if (strcmp_P(cmd, PSTR("boot")) == 0) {
    if (args[0] == '\0' || strcmp_P(args, PSTR("list")) == 0) {
      char currentBoot[NAME_LEN];
      EEPROM.get(EEPROM_BOOT_FILE_ADDR, currentBoot);
      if (currentBoot[0] == 0xFF || currentBoot[0] == '\0') strcpy(currentBoot, "0rc.sh");
      kprint(F("Active boot file: ")); kprintln(currentBoot);
      kprintln(F("Available profiles:"));
      for (int i = 0; i < MAX_FILES; i++) {
        if ((vfs[i].flags & FLAG_ACTIVE) && strstr(vfs[i].name, "rc.sh")) {
          kprint(F("  - ")); kprintln(vfs[i].name);
        }
      }
      kprintln(F("Usage: boot [filename | list | save <0-9> | reset]"));
      return;
    }
    if (strcmp_P(args, PSTR("reset")) == 0) {
      char empty[NAME_LEN]; memset(empty, 0, NAME_LEN);
      EEPROM.put(EEPROM_BOOT_FILE_ADDR, empty);
#if defined(ESP8266) || defined(ESP32)
      EEPROM.commit();
#endif
      kprintln(F("Boot reset to 0rc.sh"));
    } else if (strncmp_P(args, PSTR("save "), 5) == 0) {
      char profileName[NAME_LEN];
      snprintf(profileName, NAME_LEN, "%src.sh", args + 5);
      char wifiCmd[CONTENT_LEN];
      snprintf(wifiCmd, CONTENT_LEN, "wifi connect %s ********; waitwifi; telnet on; web on", WiFi.SSID().c_str());

      int target = -1;
      for (int i = 0; i < MAX_FILES; i++) {
        if ((vfs[i].flags & FLAG_ACTIVE) && strcmp(vfs[i].name, profileName) == 0) { target = i; break; }
        if (!(vfs[i].flags & FLAG_ACTIVE) && target == -1) target = i;
      }
      if (target != -1) {
        strncpy(vfs[target].name, profileName, NAME_LEN-1);
        strncpy(vfs[target].content, wifiCmd, CONTENT_LEN-1);
        vfs[target].flags = FLAG_ACTIVE;
        vfs[target].mode = 0644;
        strncpy(vfs[target].parentDir, "/", PATH_LEN-1);
        kprint(F("Saved current WiFi to ")); kprintln(profileName);
      }
    } else {
      if (strlen(args) >= NAME_LEN) { kprintln(F("Error: Name too long.")); return; }
      char buf[NAME_LEN];
      memset(buf, 0, NAME_LEN);
      strncpy(buf, args, NAME_LEN - 1);
      EEPROM.put(EEPROM_BOOT_FILE_ADDR, buf);
#if defined(ESP8266) || defined(ESP32)
      EEPROM.commit();
#endif
      kprint(F("Boot profile set to: ")); kprintln(buf);
    }
  }
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
  else if (strcmp_P(cmd, PSTR("ping")) == 0) {
    if (!checkWiFi())
      return;
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
    if (!checkWiFi())
      return;
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
    if (!currentAuth) { kprintln(F("Auth required.")); return; }
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
        WiFi.persistent(true);
        WiFi.mode(WIFI_STA);
        WiFi.setSleepMode(WIFI_NONE_SLEEP);
        WiFi.setAutoReconnect(true);
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
      int t = 0;
      while (WiFi.status() != WL_CONNECTED && t < 10) { delay(500); ESP.wdtFeed(); t++; }
      if (WiFi.status() != WL_CONNECTED) {
        kprintln(F("Auto-connect failed. Trying boot script..."));
        executeCommand((char*)"sh 0rc.sh", true);
      }
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
    if (!currentAuth) { kprintln(F("Auth required.")); return; }
    wl_status_t s = WiFi.status();
    kprint(F("SSID: "));
    kprintln(WiFi.SSID());
    kprint(F("RSSI: "));
    kprint(WiFi.RSSI());
    kprintln(F(" dBm"));
    kprint(F("IP:   "));
    kprintln(WiFi.localIP());
    kprint(F("GW:   "));
    kprintln(WiFi.gatewayIP());
    kprint(F("MAC:  "));
    kprintln(WiFi.macAddress());
  } else if (strcmp_P(cmd, PSTR("web")) == 0) {
    if (strcmp_P(args, PSTR("on")) == 0) {
      int retry = 5;
      while (WiFi.status() != WL_CONNECTED && retry > 0) {
        delay(1000);
        retry--;
        yield();
      }
      if (WiFi.status() != WL_CONNECTED) {
        kprintln(F("Web: Waiting for WiFi..."));
      }
      webEnabled = true;
      setupWebServer();
      kprintln(F("Web Dashboard Enabled."));
      kprint(F("URL: http://"));
      kprintln(WiFi.localIP());
    } else if (strcmp_P(args, PSTR("off")) == 0) {
      webEnabled = false;
      webServer.stop();
      kprintln(F("Web Dashboard Disabled."));
    }
  } else if (strcmp_P(cmd, PSTR("mqtt")) == 0) {
    if (!checkWiFi()) return;
    char *host = args;
    char *msg = strchr(args, ' ');
    if (msg) {
      *msg = '\0'; msg++;
      kprint(F("MQTT Sim: Sending [")); kprint(msg);
      kprint(F("] to ")); kprintln(host);

      WiFiClient client;
      if (client.connect(host, 1883)) {
        kprintln(F("Connected to Broker."));
        client.stop();
      } else kprintln(F("Broker Unreachable."));
    } else kprintln(F("Usage: mqtt [host] [message]"));
  } else if (strcmp_P(cmd, PSTR("bt")) == 0) {
#if defined(ESP32)
    if (strncmp_P(args, PSTR("on"), 2) == 0) {
      char name[32] = "UniKernel-BT";
      if (strlen(args) > 3) {
        strncpy(name, args + 3, 31);
        name[31] = '\0';
      }
      if (SerialBT.begin(name)) {
        btEnabled = true;
        addDmesg(F("Bluetooth enabled"));
        kprint(F("Bluetooth ON. Name: "));
        kprintln(name);
      } else
        kprintln(F("Error: BT Init failed."));
    } else if (strcmp_P(args, PSTR("off")) == 0) {
      SerialBT.end();
      btEnabled = false;
      addDmesg(F("Bluetooth disabled"));
      kprintln(F("Bluetooth OFF."));
    } else if (strncmp_P(args, PSTR("write "), 6) == 0) {
      if (btEnabled) {
        SerialBT.println(args + 6);
        kprintln(F("Sent via BT."));
      } else
        kprintln(F("Error: BT not enabled."));
    } else if (strcmp_P(args, PSTR("status")) == 0) {
      kprint(F("Bluetooth: "));
      kprintln(btEnabled ? F("ENABLED") : F("DISABLED"));
      if (btEnabled) {
        kprint(F("Device: "));
        kprintln(BOARD_NAME);
        kprint(F("Connected: "));
        kprintln(SerialBT.hasClient() ? F("YES") : F("NO"));
      }
    } else {
      kprintln(F("Usage: bt [on (name)/off/write text/status]"));
    }
#else
    kprintln(F("Error: Bluetooth only supported on ESP32."));
#endif
  }
#endif
  else if (strcmp_P(cmd, PSTR("save")) == 0) {
    int addr = EEPROM_VFS_ADDR;
    uint16_t magic = VFS_MAGIC;
    EEPROM.put(addr, magic);
    EEPROM.put(addr + 2, vfs);
#if defined(ESP8266) || defined(ESP32)
    EEPROM.commit();
#endif
    kprintln(F("FS saved to EEPROM."));
  } else if (strcmp_P(cmd, PSTR("load")) == 0) {
    int addr = EEPROM_VFS_ADDR;
    uint16_t magic;
    EEPROM.get(addr, magic);
    if (magic == VFS_MAGIC) {
      EEPROM.get(addr + 2, vfs);
      kprintln(F("FS loaded from EEPROM."));
    } else {
      kprintln(F("Error: No valid FS."));
    }
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
        yield();
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
      if (h < 10)
        kprint("0");
      kprint(h);
      kprint(":");
      if (m < 10)
        kprint("0");
      kprint(m);
      kprint(":");
      if (sec < 10)
        kprint("0");
      kprintln(sec);
    } else {
      kprint(F("Local Date: "));
      kprintln(ctime(&now));
    }
  } else if (strcmp_P(cmd, PSTR("sleep")) == 0 ||
             strcmp_P(cmd, PSTR("delay")) == 0) {
    int ms = atoi_safe(args);
    if (ms <= 0) return; 
    if (ms > 60000) ms = 60000; 
    delay(ms);
  } else if (strcmp_P(cmd, PSTR("waitwifi")) == 0) {
    kprintln(F("System: Waiting for IP address..."));
    int timeout = 30;
    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
      delay(500);
      ESP.wdtFeed();
      delay(500);
      kprint(F("."));
      timeout--;
      yield();
    }
    if (WiFi.status() == WL_CONNECTED) {
      kprint(F("\n[OK] IP Obtained: "));
      kprintln(WiFi.localIP());
    } else {
      kprintln(F("\n[ERROR] WiFi Connection Failed."));
      char err[64];
      wl_status_t s = WiFi.status();
      const char* reason = "Unknown";
      if (s == WL_NO_SSID_AVAIL) reason = "SSID Not Found";
      else if (s == WL_CONNECT_FAILED) reason = "Connection Failed";
      else if (s == WL_WRONG_PASSWORD) reason = "Wrong Password";
      else if (s == WL_DISCONNECTED) reason = "Disconnected";

      snprintf(err, sizeof(err), "WiFi Failed: %s", reason);
      logError(err);
    }
  } else {
    kprintln(F("Unknown command."));
  }
}

ICACHE_FLASH_ATTR void runScript(const char *content) {
  if (shellDepth >= MAX_SHELL_DEPTH) {
    kprintln(F("sh: max recursion depth reached"));
    return;
  }
  static char scriptBuffers[MAX_SHELL_DEPTH][MAX_INPUT_LEN];
  char *scriptLine = scriptBuffers[shellDepth];
  shellDepth++;

  int ci = 0, li = 0, lineNum = 0;
  int len = strlen(content);

  while (ci <= len) {
    char c = (ci < len) ? content[ci] : '\0';
    ci++;
    if (c == ';' || c == '\n' || c == '\r' || c == '\0') {
      if (li > 0) {
        scriptLine[li] = '\0';
        char *cleanedLine = kTrim(scriptLine);
        if (strlen(cleanedLine) > 0) {
          lineNum++;
          parseAndExecute(cleanedLine, MAX_INPUT_LEN, true);
        }
        li = 0;
        yield();
      }
    } else if (li < MAX_INPUT_LEN - 1) {
      scriptLine[li++] = c;
    }
  }
  shellDepth--;
}
ICACHE_FLASH_ATTR void taskBlink() {
  static bool state = false;
  state = !state;
  fastDigitalWrite(LED_BUILTIN, state ? HIGH : LOW);
}