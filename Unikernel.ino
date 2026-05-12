#include <Arduino.h>
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiServerSecure.h>

#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>

#endif
ADC_MODE(ADC_VCC);
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <WebSocketsClient.h>
#include <Wire.h>

#include "include/common.h"
#include "include/vfs.h"
#include "include/auth.h"
#include "include/shell.h"
#include "include/commands.h"
#include "UniAccel.h"

#define PRODUCTION_BUILD

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
ICACHE_FLASH_ATTR void kprintln(IPAddress ip);
#endif

WebSocketsClient webSocket;
bool accelConnected = false;
bool accelStopRequested = true;
char accelHost[16] = "192.168.1.50";
int accelPort = 81;
int accelRetryCount = 0;
unsigned long accelStartTime = 0;

ICACHE_FLASH_ATTR void discoverAccelHost();

char global_obf_buf[MAX_INPUT_LEN];
#define _OSTR(str)                                                             \
  ([]() -> const char * {                                                      \
    constexpr Obfuscator<sizeof(str)> obf(str, XOR_KEY);                       \
    for (size_t i = 0; i < sizeof(str) - 1; ++i) {                             \
      global_obf_buf[i] = (char)(obf.data[i] ^ XOR_KEY);                        \
    }                                                                          \
    global_obf_buf[sizeof(str) - 1] = '\0';                                   \
    return global_obf_buf;                                                      \
  }())
template <size_t N> struct Obfuscator {
  char data[N];
  constexpr Obfuscator(const char *str, char key) : data{} {
    for (size_t i = 0; i < N; ++i) {
      data[i] = str[i] ^ key;
    }
  }
};

const char CLR_RST[] PROGMEM = "\033[0m";
const char CLR_RED[] PROGMEM = "\033[1;31m";
const char CLR_GRN[] PROGMEM = "\033[1;32m";
const char CLR_YLW[] PROGMEM = "\033[1;33m";
const char CLR_BLU[] PROGMEM = "\033[1;34m";
const char CLR_MAG[] PROGMEM = "\033[1;35m";
const char CLR_CYN[] PROGMEM = "\033[1;36m";
const char CLR_WHT[] PROGMEM = "\033[1;37m";



void kprintProgmem(const char *pgmStr) {
  char buf[16];
  strcpy_P(buf, pgmStr);
  kprint(buf);
}

void kprintColor(const char *c) {
  if (useColor) kprint(c);
}

void kprintColor_P(const char *pgmColor) {
  if (useColor) kprintProgmem(pgmColor);
}

void kprintColor_ctx(const char *c, bool fromSerial, bool isSSH) {
  if (useColor) kprint_ctx(c, fromSerial, isSSH);
}

void kprintColor_ctx_P(const char *pgmColor, bool fromSerial, bool isSSH) {
  if (useColor) {
    char buf[16];
    strcpy_P(buf, pgmColor);
    kprint_ctx(buf, fromSerial, isSSH);
  }
}


Task taskTable[MAX_TASKS];

CronEntry cronTable[MAX_CRON];
Trigger triggerTable[MAX_TRIGS];

char inputBuffer[MAX_INPUT_LEN] = "";
int inputLen = 0;


DmesgEntry dmesg[DMESG_LINES];
int dmesgIndex = 0;
int shellDepth = 0;
int escState = 0;
bool sshAuthenticated = false;
bool isSSHInput = false;
bool needsSetup = false;
bool telnetEnabled = false;
bool webEnabled = false;
bool otaEnabled = false;
bool sshEnabled = false;
unsigned long otaEndTime = 0;
#define OTA_WINDOW 300000
int redirectionFileIdx = -1;

#define MAX_HISTORY 4
char cmdHistory[MAX_HISTORY][MAX_INPUT_LEN];
int historyWriteIdx = 0;
int historyViewIdx = -1;
int historyCount = 0;

unsigned long lastSerialActivity = 0;
unsigned long lastTelnetActivity = 0;
unsigned long lastSSHActivity = 0;

#define MAX_SHELL_DEPTH 2
#define SESSION_TIMEOUT 300000
#define MAX_FAIL_COUNT 5
#define LOCKOUT_DURATION 300000




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
ICACHE_FLASH_ATTR void parseAndExecute(char *line, size_t maxLen,
                                       bool fromSerial);
ICACHE_FLASH_ATTR void kPulse();
ICACHE_FLASH_ATTR void runScript(const char *content);
ICACHE_FLASH_ATTR void setupWebServer();
void taskBlink();
void taskMemoryMonitor();

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
int authFailures = 0;
unsigned long lockoutEnd = 0;
unsigned long lastActivity = 0;
ESP8266WebServer webServer(80);
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
WiFiServer telnetServer(23);
WiFiClient telnetClient;
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
  fetch('/api/stats', { headers: {'Authorization': pw} }).then(r=>r.json()).then(d=>{
  if (d.error) return;
  const p = Math.round((1 - d.free/81920)*100);
  document.getElementById('g-mem').style.strokeDashoffset = 440 - (440 * p / 100);
  document.getElementById('t-mem').innerText = p + '%';
});}
async function toggle(p,v){ const pw=document.getElementById('p').value; await fetch('/api/gpio', { method: 'POST', headers: {'Content-Type': 'application/json', 'Authorization': pw}, body: JSON.stringify({pin: p, val: v}) }); }
async function doSleep(){ const pw=document.getElementById('p').value; const s = document.getElementById('sl').value; await fetch('/api/sleep?s='+s, { headers: {'Authorization': pw} }); }
setInterval(update, 5000); update();
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
    sshClient.print(String(s));
#endif
#if defined(ESP32)
  if (btEnabled)
    SerialBT.print(s);
#endif
}
void kprint(char c) {
  if (c == '\0')
    return;
  if (redirectionFileIdx != -1) {
    size_t len = strlen(vfs[redirectionFileIdx].content);
    if (len < CONTENT_LEN - 1) {
      vfs[redirectionFileIdx].content[len] = c;
      vfs[redirectionFileIdx].content[len + 1] = '\0';
    }
    return;
  }
  Serial.write(c);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
  if (telnetClient && telnetClient.connected())
    telnetClient.write((uint8_t)c);
  if (sshClient && sshClient.connected())
    sshClient.print(c);
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
  char buf[12];
  itoa(n, buf, 10);
  kprint(buf);
}
ICACHE_FLASH_ATTR void kprint(float n) {
  char buf[16];
  dtostrf(n, 4, 2, buf);
  kprint(buf);
}

ICACHE_FLASH_ATTR void kprint(unsigned int n) {
  char buf[12];
  utoa(n, buf, 10);
  kprint(buf);
}

ICACHE_FLASH_ATTR void kprint(long n) {
  char buf[16];
  ltoa(n, buf, 10);
  kprint(buf);
}

ICACHE_FLASH_ATTR void kprint(unsigned long n) {
  char buf[16];
  ultoa(n, buf, 10);
  kprint(buf);
}

ICACHE_FLASH_ATTR void kprint(int n, int base) {
  char buf[12];
  itoa(n, buf, base);
  kprint(buf);
}

ICACHE_FLASH_ATTR void kprint_ctx(const char *s, bool fromSerial, bool isSSH) {
  if (fromSerial) Serial.print(s);
  else if (isSSH) {
    if (sshClient && sshClient.connected()) sshClient.print(s);
  } else {
    if (telnetClient && telnetClient.connected()) telnetClient.print(s);
  }
}
ICACHE_FLASH_ATTR void kprintln_ctx(bool fromSerial, bool isSSH) { kprint_ctx("\r\n", fromSerial, isSSH); }

ICACHE_FLASH_ATTR void kprintln() { kprint(F("\r\n")); }

ICACHE_FLASH_ATTR void kprint_sys(const char *s) {
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


void redrawPrompt() {
  redrawPromptAll();
}

void redrawPrompt(bool fromSerial, bool isSSH) {
  kprint_ctx("\r\033[K", fromSerial, isSSH);
  printPrompt(fromSerial, isSSH);
  kprint_ctx(inputBuffer, fromSerial, isSSH);
}

void redrawPromptAll() {
  redrawPrompt(true, false);
  if (telnetClient && telnetClient.connected()) redrawPrompt(false, false);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (sshClient && sshClient.connected()) redrawPrompt(false, true);
#endif
}

void kprintLog(const String &msg, bool fromSerial, bool isSSH) {
  if (inputLen > 0) {
    kprint_ctx("\r\033[2K", fromSerial, isSSH);
    kprint_ctx(msg.c_str(), fromSerial, isSSH);
    redrawPrompt(fromSerial, isSSH);
  } else {
    kprint_ctx(msg.c_str(), fromSerial, isSSH);
  }
}

void kprintlnLog(const String &msg) {
  if (inputLen > 0) {
    kprint("\r\033[2K");
    kprintln(msg.c_str());
    redrawPromptAll();
  } else {
    kprintln(msg.c_str());
  }
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

void kprint(String s) { kprint(s.c_str()); }

void kprintln(String s) { kprintln(s.c_str()); }

ICACHE_FLASH_ATTR void kprintln(const char *s) {
  kprint(s);
  kprintln();
}

ICACHE_FLASH_ATTR void kprintln(const __FlashStringHelper *s) {
  kprint(s);
  kprintln();
}

ICACHE_FLASH_ATTR void kprintln(int n) {
  kprint(n);
  kprintln();
}

ICACHE_FLASH_ATTR void kprintln(unsigned int n) {
  kprint(n);
  kprintln();
}

ICACHE_FLASH_ATTR void kprintln(long n) {
  kprint(n);
  kprintln();
}

ICACHE_FLASH_ATTR void kprintln(unsigned long n) {
  kprint(n);
  kprintln();
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
    if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(vfs[j].name, "error.log") == 0 &&
        strcmp(vfs[j].parentDir, "/sys") == 0) {
      found = j;
      break;
    }
  }
  if (found != -1) {
    char entry[CONTENT_LEN];
    snprintf(entry, sizeof(entry), "[%lu] %s\n", millis() / 1000, msg);
    size_t currentLen = strlen(vfs[found].content);
    strncat(vfs[found].content, entry, CONTENT_LEN - currentLen - 1);
  }
}



ICACHE_FLASH_ATTR void printPrompt(bool fromSerial, bool isSSH) {
  if (accelChatMode) {
    kprintColor_ctx_P(CLR_MAG, fromSerial, isSSH);
    kprint_ctx(currentModelName, fromSerial, isSSH);
    kprint_ctx("@", fromSerial, isSSH);
    kprint_ctx(accelHost, fromSerial, isSSH);
    kprint_ctx("> ", fromSerial, isSSH);
    kprintColor_ctx_P(CLR_RST, fromSerial, isSSH);
    return;
  }
  bool currentAuth = fromSerial ? serialAuthenticated : (telnetAuthenticated || sshAuthenticated);
  kprintColor_ctx_P(CLR_CYN, fromSerial, isSSH);
  kprint_ctx(currentAuth ? "root@" : "guest@", fromSerial, isSSH);
  kprint_ctx(BOARD_NAME, fromSerial, isSSH);
  kprint_ctx(":", fromSerial, isSSH);
  kprintColor_ctx_P(CLR_BLU, fromSerial, isSSH);
  kprint_ctx(currentPath, fromSerial, isSSH);
  kprintColor_ctx_P(CLR_RST, fromSerial, isSSH);
  kprint_ctx(currentAuth ? "# " : "> ", fromSerial, isSSH);
}

ICACHE_FLASH_ATTR void checkMemorySafeguard() {
  int free = freeMemory();
  

  if (free < 1500) {
    addDmesg(F("CRITICAL: Emergency OOM Recovery!"));
    emergencyMemoryCleanup();
  } else if (free < 2500) {
    addDmesg(F("WARNING: Low memory - Service optimization"));
    optimizeMemoryUsage();
  } else if (free < 4000) {
    addDmesg(F("INFO: Memory pressure detected"));
    preventiveMemoryCleanup();
  }
}

ICACHE_FLASH_ATTR void emergencyMemoryCleanup() {

  if (webEnabled) {
    webEnabled = false;
    webServer.stop();
    addDmesg(F("Emergency: Web service terminated"));
  }
  if (telnetEnabled) {
    telnetEnabled = false;
    telnetServer.stop();
    addDmesg(F("Emergency: Telnet service terminated"));
  }
  if (sshEnabled) {
    sshEnabled = false;
    sshServer.stop();
    addDmesg(F("Emergency: SSH service terminated"));
  }
  

  historyCount = 0;
  historyWriteIdx = 0;
  memset(inputBuffer, 0, sizeof(inputBuffer));
  inputLen = 0;
  

  for (int i = 0; i < MAX_FILES; i++) {
    if ((vfs[i].flags & FLAG_ACTIVE) && 
        !(vfs[i].flags & FLAG_ISDIR) &&
        strcmp(vfs[i].name, "error.log") != 0) {
      vfs[i].content[0] = '\0';
    }
  }
  
  yield();
  delay(100);
}

ICACHE_FLASH_ATTR void optimizeMemoryUsage() {

  if (webEnabled) {
    webEnabled = false;
    webServer.stop();
    addDmesg(F("Optimized: Web service paused"));
  }
  

  if (historyCount > 2) {
    historyCount = 2;
    addDmesg(F("Optimized: Command history reduced"));
  }
  

  if (inputLen > 0) {
    memset(inputBuffer, 0, sizeof(inputBuffer));
    inputLen = 0;
  }
}

ICACHE_FLASH_ATTR void preventiveMemoryCleanup() {
 
  if (historyCount > 4) {
    historyCount = 4;
  }
  
 
  yield();
}

ICACHE_FLASH_ATTR void logResetReason() {
  String reason = ESP.getResetReason();
  File f = LittleFS.open("/crash.log", "a");
  if (f) {
    f.print("[");
    f.print(millis() / 1000);
    f.print("] Reset: ");
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
  Serial.println(
      F("[Hardware] Checking for reset button presses (2s window)..."));

  while (millis() - windowStart < 2000) {
    if (digitalRead(bootBtn) == LOW) {
      delay(50);
      if (digitalRead(bootBtn) == LOW) {
        presses++;
        digitalWrite(LED_BUILTIN, LOW);
        delay(200);
        digitalWrite(LED_BUILTIN, HIGH);
        while (digitalRead(bootBtn) == LOW)
          yield();
        windowStart = millis();
        Serial.print(F("Press detected: "));
        Serial.println(presses);
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
  initShell();
#if defined(ESP8266) || defined(ESP32)
  EEPROM.begin(4096);
#endif

  uint16_t magic;
  int addr = EEPROM_VFS_ADDR;
  EEPROM.get(addr, magic);
  if (magic == VFS_MAGIC) {
    EEPROM.get(addr + 2, vfs);
    addDmesg(F("Filesystem restored from EEPROM"));
    bool hasSys = false;
    for (int i = 0; i < 16; i++) {
        if ((vfs[i].flags & FLAG_ACTIVE) && strcmp(vfs[i].name, "sys") == 0) hasSys = true;
    }
    if (!hasSys) initFS();
  }

  Wire.begin();
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
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
  initUniAccel();
  logResetReason();
  char otaHash[33];
  EEPROM.get(EEPROM_OTA_PASS_ADDR, otaHash);
  otaHash[32] = '\0';

  ArduinoOTA.setHostname("UniKernel-Node");
  if (otaHash[0] != 0xFF && otaHash[0] != 0x00) {
    ArduinoOTA.setPasswordHash(otaHash);
  } else {
    addDmesg(F("OTA: No password set, OTA disabled"));
  }
  ArduinoOTA.onStart([]() { addDmesg(F("OTA: Starting Update")); });
  ArduinoOTA.onEnd([]() { addDmesg(F("OTA: Update Finished")); });
  ArduinoOTA.onError(
      [](ota_error_t error) { addDmesg(F("OTA: Error occurred")); });
  MDNS.begin("unikernel");

  static const uint8_t eckey[] PROGMEM = {
      0x30, 0x77, 0x02, 0x01, 0x01, 0x04, 0x20, 0x25, 0xe8, 0xec, 0x1e,
      0x7e, 0x5e, 0xd4, 0x54, 0x53, 0x6a, 0x80, 0xd0, 0xf3, 0xf8, 0x30,
      0xe5, 0x36, 0x1a, 0xb2, 0x35, 0xfb, 0x82, 0xd7, 0x4a, 0x82, 0x73,
      0x73, 0x15, 0x4c, 0x02, 0x49, 0xa2, 0xa0, 0x0a, 0x06, 0x08, 0x2a,
      0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0xa1, 0x44, 0x03, 0x42,
      0x00, 0x04, 0x4d, 0x8f, 0x0c, 0xd2, 0x99, 0xe6, 0x8d, 0xe6, 0xfb,
      0xac, 0x8c, 0x5e, 0xfe, 0xa3, 0xe3, 0x99, 0x4b, 0xc8, 0x0c, 0x16,
      0x26, 0x5f, 0xa1, 0xa4, 0x12, 0xdd, 0x71, 0x5c, 0x36, 0x8b, 0x3f,
      0xe1, 0x9a, 0xe8, 0x4f, 0xfb, 0x2b, 0xbc, 0xd3, 0x6d, 0xa7, 0x07,
      0x36, 0xf3, 0xd5, 0xba, 0x0a, 0x7e, 0xba, 0x7d, 0xec, 0xc3, 0x38,
      0xd6, 0xca, 0xfb, 0x1c, 0xbf, 0x37, 0x44, 0x4a, 0x02, 0xcb, 0xf1};
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

  registerCommands();

  for (int t = 0; t < MAX_TASKS; t++) {
    taskTable[t].active = false;
    taskTable[t].executionCount = 0;
  }
  
  taskTable[0].func = taskBlink;
  taskTable[0].interval = 500;
  taskTable[0].lastRun = 0;
  taskTable[0].active = false;
  
 
  taskTable[1].func = taskMemoryMonitor;
  taskTable[1].interval = 5000;
  taskTable[1].lastRun = 0;
  taskTable[1].active = true;
  strcpy(taskTable[1].name, "memmon");
  
  fastDigitalWrite(LED_BUILTIN, HIGH);

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
      if (bootFile[i] < 32 || bootFile[i] > 126) {
        isValid = false;
        break;
      }
    }
  }

  if (!isValid) {
    strcpy(bootFile, "0rc.sh");
  }

  serialAuthenticated = false;
  addDmesg(F("System: Automated Boot Sequence Started"));

  delay(500);

  Serial.println(F("\n[System] UniKernel Ready."));
}

ICACHE_FLASH_ATTR void setupWebServer() {
  webServer.collectHeaders("Host", "Authorization");
  webServer.on("/", HTTP_GET,
               []() { webServer.send_P(200, "text/html", DASHBOARD_HTML); });

  webServer.on("/api/gpio", HTTP_POST, []() {
    if (!webServer.hasArg("plain")) {
      webServer.send(400, "application/json",
                     _OSTR("{\"error\":\"Bad Request\"}"));
      return;
    }
    static JsonDocument doc;
    doc.clear();
    DeserializationError error = deserializeJson(doc, webServer.arg("plain"));
    if (error) {
      webServer.send(400, "application/json",
                     _OSTR("{\"error\":\"Invalid JSON\"}"));
      return;
    }
    String pass = webServer.header("Authorization");
    if (pass.length() == 0) {
      webServer.send(401, "application/json", _OSTR("{\"error\":\"Auth Required\"}"));
      return;
    }

    if (!checkWebAuth(pass, webServer.client().remoteIP())) {
      webServer.send(401, "application/json",
                     _OSTR("{\"error\":\"Unauthorized\"}"));
      return;
    }
    int pin = doc["pin"] | 2;
    int val = doc["val"] | 1;
    fastPinMode(pin, OUTPUT);
    fastDigitalWrite(pin, val);
    webServer.send(200, "application/json", _OSTR("{\"status\":\"success\"}"));
  });

  webServer.on("/api/stats", []() {
    String auth = webServer.header("Authorization");
    if (auth.length() > 0) {
      if (!checkWebAuth(auth, webServer.client().remoteIP())) {
        webServer.send(401, "application/json",
                       _OSTR("{\"error\":\"Unauthorized or Rate-limited\"}"));
        return;
      }
    } else {
      webServer.send(401, "application/json",
                     _OSTR("{\"error\":\"Auth Required\"}"));
      return;
    }

    char json[64];
    snprintf(json, sizeof(json), "{\"free\":%d,\"up\":%lu}", 
             ESP.getFreeHeap(), millis() / 1000);
    webServer.send(200, "application/json", json);
  });

  webServer.on("/api/sleep", []() {
    String auth = webServer.header("Authorization");
    if (!checkWebAuth(auth, webServer.client().remoteIP())) {
      webServer.send(401, "application/json", _OSTR("{\"error\":\"Unauthorized\"}"));
      return;
    }
    int s = webServer.arg("s").toInt();
    webServer.send(200, "application/json", _OSTR("{\"status\":\"sleeping\"}"));
    delay(100);
    ESP.deepSleep(s * 1000000);
  });

}

#include "include/shell.h"

ICACHE_FLASH_ATTR void executeCommand(char *line, bool fromSerial) {
    char resolved[MAX_INPUT_LEN];
    if (resolveAlias(line, resolved)) {
        dispatchCommand(resolved, fromSerial);
    } else {
        dispatchCommand(line, fromSerial);
    }
}

ICACHE_FLASH_ATTR void processTriggers() {
  static unsigned long lastTrig = 0;
  if (millis() - lastTrig < 5000)
    return;
  lastTrig = millis();

  for (int i = 0; i < MAX_TRIGS; i++) {
    if (!triggerTable[i].active)
      continue;
    int current = 0;
    if (strcmp(triggerTable[i].cond, "vcc") == 0)
      current = ESP.getVcc();
    else if (strcmp(triggerTable[i].cond, "temp") == 0)
      current = 25 + (millis() % 5);
    else if (strcmp(triggerTable[i].cond, "ram") == 0)
      current = freeMemory();

    bool fire = false;
    if (triggerTable[i].op == '<' && current < triggerTable[i].val)
      fire = true;
    if (triggerTable[i].op == '>' && current > triggerTable[i].val)
      fire = true;

    if (fire) {
      char buf[MAX_INPUT_LEN];
      strncpy(buf, triggerTable[i].action, MAX_INPUT_LEN - 1);
      buf[MAX_INPUT_LEN - 1] = '\0';
      addDmesg(F("Trigger Fired!"));

      executeCommand(buf, false);
    }
  }
}

void doTabCompletion(bool fromSerial, bool isSSH) {
  if (inputLen == 0)
    return;
  inputBuffer[inputLen] = '\0';

  const char *cmds[] = {
      "ls",     "cd",    "pwd",      "cat",  "echo",     "rm",     "mkdir",
      "touch",  "wifi",  "accel",    "sys",  "help",     "clear",  "reboot",
      "uptime", "free",  "neofetch", "ping", "ifconfig", "hwinfo", "top",
      "ps",     "login", "logout",   "df",   "dmesg", "sh"};
  int numCmds = 27;

  int matches = 0;
  const char *match = NULL;
  for (int i = 0; i < numCmds; i++) {
    if (strncmp(inputBuffer, cmds[i], inputLen) == 0) {
      matches++;
      match = cmds[i];
    }
  }

  if (matches == 1) {
    while (inputLen > 0) {
      kprint_ctx("\b \b", fromSerial, isSSH);
      inputLen--;
    }
    strcpy(inputBuffer, match);
    inputLen = strlen(inputBuffer);
    kprint_ctx(inputBuffer, fromSerial, isSSH);
    kprint_ctx(" ", fromSerial, isSSH);
    inputBuffer[inputLen++] = ' ';
    inputBuffer[inputLen] = '\0';
  } else if (matches > 1) {
    kprintln_ctx(fromSerial, isSSH);
    for (int i = 0; i < numCmds; i++) {
      if (strncmp(inputBuffer, cmds[i], strlen(inputBuffer)) == 0) {
        kprint_ctx(cmds[i], fromSerial, isSSH);
        kprint_ctx("  ", fromSerial, isSSH);
      }
    }
    kprintln_ctx(fromSerial, isSSH);
    printPrompt(fromSerial, isSSH);
    kprint_ctx(inputBuffer, fromSerial, isSSH);
  }
}

ICACHE_FLASH_ATTR void loop() {
  ESP.wdtFeed();
  checkMemorySafeguard();
  processTriggers();

  if (webEnabled)
    webServer.handleClient();

  if (!accelStopRequested)
    loopUniAccel();
  

  if (telnetClient && !telnetClient.connected()) {
    telnetClient.stop();
    telnetAuthenticated = false;
    addDmesg(F("Telnet: Connection cleaned up"));
  }
  
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  if (sshClient && !sshClient.connected()) {
    sshClient.stop();
    sshAuthenticated = false;
    addDmesg(F("SSH: Connection cleaned up"));
  }
#endif

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
    if (freeMemory() < 4000) {
      WiFiClientSecure c = sshServer.available();
      c.println(F("Server busy: Low memory"));
      c.stop();
      addDmesg(F("SSH: Connection rejected - low memory"));
    } else {
      WiFiClientSecure c = sshServer.available();
      if (!isIpAllowed(c.remoteIP())) {
        c.println(F("Access Denied: Firewall Block"));
        c.stop();
      } else if (sshClient && sshClient.connected()) {
        c.println(F("Busy: Another SSH session active."));
        c.stop();
      } else {
        sshClient = c;
        sshAuthenticated = false;
        ESP.wdtFeed();
        printPrompt(false, true); 
        lastSSHActivity = millis();
        addDmesg(F("SSH: New connection established"));
      }
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
      if (cronTable[i].active && cronTable[i].h == ti->tm_hour &&
          cronTable[i].m == ti->tm_min) {
        char buf[MAX_INPUT_LEN];
        strncpy(buf, cronTable[i].cmd, MAX_INPUT_LEN - 1);
        executeCommand(buf, true);
      }
    }
  }
  for (int t = 0; t < MAX_TASKS; t++) {
    if (taskTable[t].active &&
        (now - taskTable[t].lastRun >= taskTable[t].interval)) {
      taskTable[t].lastRun = now;
      taskTable[t].executionCount++;
      taskTable[t].func();
    }
  }

  
  static bool firstBoot = true;
  if (firstBoot) {
    firstBoot = false;
    kprintln(F("[System] Checking for Boot Scripts (0rc-2rc.sh)..."));
    
    bool oldAuth = serialAuthenticated;
    serialAuthenticated = true; 
    shellDepth++;
    
    for (int i = 0; i <= 2; i++) {
        char bootFile[16];
        snprintf(bootFile, sizeof(bootFile), "%drc.sh", i);
        int idx = findFile(bootFile, "/");
        if (idx != -1) {
            kprint(F("[System] Executing Standard: "));
            kprintln(bootFile);
            char bootAction[24];
            snprintf(bootAction, sizeof(bootAction), "sh %s", bootFile);
            executeCommand(bootAction, true);
        }
    }
    
    char customBoot[NAME_LEN];
    EEPROM.get(EEPROM_BOOT_FILE_ADDR, customBoot);
    customBoot[NAME_LEN-1] = '\0';
    if (customBoot[0] != 0xFF && customBoot[0] != '\0') {
      
        if (!isSystemProtected(customBoot)) {
            int idx = findFile(customBoot, "/");
            if (idx != -1) {
                kprint(F("[System] Executing Custom: "));
                kprintln(customBoot);
                char bootAction[NAME_LEN + 8];
                snprintf(bootAction, sizeof(bootAction), "sh %s", customBoot);
                executeCommand(bootAction, true);
            }
        }
    }
    
    serialAuthenticated = oldAuth;
    shellDepth--;
    printPrompt(true, false); 
  }

  if (serialAuthenticated && isTimeout(lastSerialActivity, SESSION_TIMEOUT)) {
    serialAuthenticated = false;
    accelChatMode = false;
    Serial.println(F("\nSerial session timeout. Logged out."));
    printPrompt(true, false);
  }

  if (sshAuthenticated && isTimeout(lastSSHActivity, SESSION_TIMEOUT)) {
    sshAuthenticated = false;
    accelChatMode = false;
    if (sshClient && sshClient.connected()) {
      sshClient.println(F("\nSSH session timeout. Closing connection."));
      sshClient.stop();
    }
  }

  if (telnetAuthenticated && isTimeout(lastTelnetActivity, SESSION_TIMEOUT)) {
    telnetAuthenticated = false;
    accelChatMode = false;
    if (telnetClient && telnetClient.connected()) {
      telnetClient.println(F("\nTelnet session timeout. Closing connection."));
      telnetClient.stop();
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
    if (freeMemory() < 4000) {
      WiFiClient tc = telnetServer.available();
      tc.println(F("Server busy: Low memory"));
      tc.stop();
      addDmesg(F("Telnet: Connection rejected - low memory"));
    } else {
      WiFiClient tc = telnetServer.available();
      if (!isIpAllowed(tc.remoteIP())) {
        tc.println(F("Access Denied: Firewall Block"));
        tc.stop();
      } else if (!telnetClient || !telnetClient.connected()) {
        telnetClient = tc;
        telnetAuthenticated = false;
        printPrompt(false, false); 
        addDmesg(F("Telnet: New connection established"));
      } else {
        tc.println(F("Busy: Another telnet session active."));
        tc.stop();
      }
    }
  }

  if (!hasInput && telnetEnabled && telnetClient &&
      telnetClient.available() > 0) {
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
    if (fromSerial) lastSerialActivity = millis();
    else if (isSSHInput) lastSSHActivity = millis();
    else lastTelnetActivity = millis();

    if (!fromSerial && (isLockedOut || (millis() - lastLoginAttempt < loginCooldown))) {
      if (!isLockedOut) {
          kprint_ctx("\rCooldown Active: ", fromSerial, isSSHInput);
          char buf[12]; ltoa((loginCooldown - (millis() - lastLoginAttempt)) / 1000, buf, 10);
          kprint_ctx(buf, fromSerial, isSSHInput);
          kprint_ctx("s left.\r\n", fromSerial, isSSHInput);
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
        kprintln_ctx(fromSerial, isSSHInput);
        executeCommand(inputBuffer, fromSerial);
        inputLen = 0;
        memset(inputBuffer, 0, sizeof(inputBuffer));
        printPrompt(fromSerial, isSSHInput);
      } else {
        kprintln_ctx(fromSerial, isSSHInput);
        printPrompt(fromSerial, isSSHInput);
      }
      return;
    } 

    if (c == 3) { 
      inputLen = 0;
      memset(inputBuffer, 0, sizeof(inputBuffer));
      kprint_ctx("^C\r\n", fromSerial, isSSHInput);
      printPrompt(fromSerial, isSSHInput);
      lastChar = c;
      return;
    }

    if (c == 8 || c == 127) { 
      if (inputLen > 0) {
        inputLen--;
        inputBuffer[inputLen] = '\0';
        kprint_ctx("\b \b", fromSerial, isSSHInput);
      }
      lastChar = c;
      return;
    }

    if (c == '\t') {
      doTabCompletion(fromSerial, isSSHInput);
      lastChar = c;
      return;
    }

    if (c >= 32 && c <= 126 && inputLen < MAX_INPUT_LEN - 1) {
      inputBuffer[inputLen++] = c;
      inputBuffer[inputLen] = '\0';
      char buf[2] = {c, 0};
      kprint_ctx(buf, fromSerial, isSSHInput);
    } else if (c == 0x1b) {
      inEscSeq = true;
      escState = 0;
    } else if (inEscSeq) {
      if (escState == 0 && c == '[') escState = 1;
      else if (escState == 1) {
        if (c == 'A' || c == 'B') {
          if (historyCount > 0) {
            if (c == 'A') {
                if (historyViewIdx == -1) historyViewIdx = (historyWriteIdx + MAX_HISTORY - 1) % MAX_HISTORY;
                else historyViewIdx = (historyViewIdx + MAX_HISTORY - 1) % MAX_HISTORY;
            } else {
                if (historyViewIdx != -1) historyViewIdx = (historyViewIdx + 1) % MAX_HISTORY;
            }
            while (inputLen > 0) { kprint_ctx("\b \b", fromSerial, isSSHInput); inputLen--; }
            strncpy(inputBuffer, cmdHistory[historyViewIdx], MAX_INPUT_LEN - 1);
            inputLen = strlen(inputBuffer);
            kprint_ctx(inputBuffer, fromSerial, isSSHInput);
          }
        }
        escState = 0;
        inEscSeq = false;
      }
    }
    lastChar = c;
  }
}


ICACHE_FLASH_ATTR bool checkPermission(int fileIdx, uint8_t action,
                                       bool fromSerial) {
  bool currentAuth = fromSerial ? serialAuthenticated
                                : (telnetAuthenticated || sshAuthenticated);
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
  if (strcmp_P(cmd, PSTR("top")) == 0) return true;
  if (strcmp_P(cmd, PSTR("date")) == 0) return true;
  if (strcmp_P(cmd, PSTR("help")) == 0) return true;
  if (strcmp_P(cmd, PSTR("neofetch")) == 0) return true;
  if (strcmp_P(cmd, PSTR("clear")) == 0) return true;
  if (strcmp_P(cmd, PSTR("read")) == 0) return true;
  if (strcmp_P(cmd, PSTR("ping")) == 0) return true;
  if (strcmp_P(cmd, PSTR("ifconfig")) == 0) return true;
  if (strcmp_P(cmd, PSTR("wifi")) == 0) return true;
  if (strcmp_P(cmd, PSTR("hwinfo")) == 0) return true;
  if (strcmp_P(cmd, PSTR("sys")) == 0) return true;
  if (strcmp_P(cmd, PSTR("color")) == 0) return true;
  if (strcmp_P(cmd, PSTR("env")) == 0) return true;
  if (strcmp_P(cmd, PSTR("alias")) == 0) return true;
  if (strcmp_P(cmd, PSTR("login")) == 0) return true;
  if (strcmp_P(cmd, PSTR("logout")) == 0) return true;
  if (strcmp_P(cmd, PSTR("exit")) == 0) return true;
  if (strcmp_P(cmd, PSTR("telnet")) == 0) return true;
  if (strcmp_P(cmd, PSTR("ssh")) == 0) return true;
  if (strcmp_P(cmd, PSTR("web")) == 0) return true;
  if (strcmp_P(cmd, PSTR("ota")) == 0) return true;
  if (strcmp_P(cmd, PSTR("netstat")) == 0) return true;
  if (strcmp_P(cmd, PSTR("accel")) == 0) return true;
  if (strcmp_P(cmd, PSTR("hf")) == 0) return true;
  if (strcmp_P(cmd, PSTR("chat")) == 0) return true;

  return false;
}

ICACHE_FLASH_ATTR void expandVars(char *line, size_t maxLen) {
  if (!line || strlen(line) >= maxLen - 1) {
    return;
  }
  
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

ICACHE_FLASH_ATTR void parseAndExecute(char *line, size_t maxLen,
                                       bool fromSerial) {
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
    fastDigitalWrite(LED_BUILTIN, LOW);
    delay(30);
    fastDigitalWrite(LED_BUILTIN, HIGH);
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

ICACHE_FLASH_ATTR void taskMemoryMonitor() {
  static unsigned long lastCriticalAlert = 0;
  static unsigned long lastWarningAlert = 0;
  static unsigned long lastInfoAlert = 0;
  static int lastFreeMemory = 0;
  
  ESP.wdtFeed();
  int free = freeMemory();
  unsigned long currentTime = millis();
  
  bool memoryDecreased = (lastFreeMemory - free) > 200;
  bool memoryCritical = free < 1500;
  bool memoryLow = free < 3000;
  
  if (memoryCritical && memoryDecreased && (currentTime - lastCriticalAlert > 60000)) {
    addDmesg(F("CRITICAL: Emergency memory recovery!"));
    lastCriticalAlert = currentTime;
    emergencyMemoryCleanup();
    ESP.wdtFeed();
  }
  else if (memoryLow && memoryDecreased && (currentTime - lastWarningAlert > 120000)) {
    addDmesg(F("WARNING: Low memory - optimizing"));
    lastWarningAlert = currentTime;
    optimizeMemoryUsage();
    ESP.wdtFeed();
  }
  else if (free < 5000 && (currentTime - lastInfoAlert > 300000)) {
    addDmesg(F("INFO: Memory pressure monitoring"));
    lastInfoAlert = currentTime;
    preventiveMemoryCleanup();
    ESP.wdtFeed();
  }
  
  lastFreeMemory = free;
  
  for (int i = 0; i < MAX_TASKS; i++) {
    if (taskTable[i].active && strcmp(taskTable[i].name, "memmon") == 0) {
      taskTable[i].executionCount++;
      break;
    }
  }
}
