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
#include "UniAccel.h"
#include "include/auth.h"
#include "include/commands.h"
#include "include/common.h"
#include "include/shell.h"
#include "include/vfs.h"
#define PRODUCTION_BUILD
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
ICACHE_FLASH_ATTR void kprintln(IPAddress ip);
#endif
WebSocketsClient webSocket;
volatile bool accelConnected = false; 
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
      global_obf_buf[i] = (char)(obf.data[i] ^ XOR_KEY);                       \
    }                                                                          \
    global_obf_buf[sizeof(str) - 1] = '\0';                                    \
    return global_obf_buf;                                                     \
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
  if (useColor)
    kprint(c);
}
void kprintColor_P(const char *pgmColor) {
  if (useColor)
    kprintProgmem(pgmColor);
}
void kprintColor_ctx(const char *c, bool fromSerial) {
  if (useColor)
    kprint_ctx(c, fromSerial);
}
void kprintColor_ctx_P(const char *pgmColor, bool fromSerial) {
  if (useColor) {
    char buf[16];
    strcpy_P(buf, pgmColor);
    kprint_ctx(buf, fromSerial);
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
bool needsSetup = false;
bool telnetEnabled = false;
bool webEnabled = false;
bool otaEnabled = false;
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
WiFiServer telnetServer(23);
WiFiClient telnetClient;
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
<title>Dashboard</title>
<link href='https://fonts.googleapis.com/css2?family=Outfit:wght@300;500;700&family=JetBrains+Mono&display=swap' rel='stylesheet'>
<style>
:root { --bg: #030816; --card: rgba(255,255,255,0.03); --primary: #00f2ff; --accent: #7000ff; --danger: #ff4757; --success: #00ff88; }
body { background: var(--bg); color: #fff; font-family: 'Outfit', sans-serif; margin: 0; padding: 0; overflow-x: hidden; }
.container { max-width: 1200px; margin: 40px auto; padding: 20px; }
.glass { background: var(--card); backdrop-filter: blur(15px); border: 1px solid rgba(255,255,255,0.08); border-radius: 24px; box-shadow: 0 8px 32px 0 rgba(0,0,0,0.5); }
header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 40px; border-bottom: 1px solid rgba(255,255,255,0.05); padding-bottom: 20px; }
h1 { font-weight: 700; font-size: 2.5rem; background: linear-gradient(90deg, var(--primary), var(--accent)); -webkit-background-clip: text; -webkit-text-fill-color: transparent; margin: 0; }
.badge { padding: 6px 12px; border-radius: 20px; font-size: 0.8rem; font-weight: 700; text-transform: uppercase; letter-spacing: 1px; }
.badge-on { background: rgba(0,255,136,0.1); color: var(--success); border: 1px solid var(--success); box-shadow: 0 0 10px rgba(0,255,136,0.2); }
.badge-off { background: rgba(255,71,87,0.1); color: var(--danger); border: 1px solid var(--danger); }
.grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 24px; }
.card { padding: 30px; position: relative; transition: 0.4s; }
.card:hover { border-color: rgba(0,242,255,0.3); transform: translateY(-5px); }
.card h3 { margin-top: 0; color: rgba(255,255,255,0.6); font-size: 0.9rem; letter-spacing: 2px; text-transform: uppercase; }
.stat-val { font-size: 3rem; font-weight: 700; font-family: 'JetBrains Mono'; margin: 10px 0; display: flex; align-items: baseline; }
.stat-val span { font-size: 1.2rem; color: rgba(255,255,255,0.4); margin-left: 8px; }
.gpu-metrics { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-top: 20px; }
.metric { background: rgba(255,255,255,0.02); padding: 15px; border-radius: 15px; border: 1px solid rgba(255,255,255,0.03); }
.metric-label { font-size: 0.7rem; color: rgba(255,255,255,0.4); text-transform: uppercase; }
.metric-val { font-family: 'JetBrains Mono'; font-weight: 700; color: var(--primary); }
.btn { padding: 14px; border-radius: 12px; border: none; color: #fff; font-weight: 700; cursor: pointer; transition: 0.3s; width: 100%; margin-top: 10px; }
.btn-primary { background: linear-gradient(45deg, var(--primary), var(--accent)); }
.btn-outline { background: transparent; border: 1px solid rgba(255,255,255,0.1); }
.btn:hover { filter: brightness(1.2); transform: scale(1.02); }
input { width: 100%; padding: 14px; border-radius: 12px; border: 1px solid rgba(255,255,255,0.1); background: rgba(0,0,0,0.3); color: #fff; margin-bottom: 15px; box-sizing: border-box; font-family: 'JetBrains Mono'; }
</style></head><body>
<div class='container'>
  <header><h1>UniKernel Web Dashboard</h1><div id='st-badge' class='badge badge-off'>OFFLINE</div></header>
  <div class='grid'>
    <div class='card glass'>
      <h3>GPU Accelerator Host</h3>
      <div id='h-name' class='stat-val' style='font-size:1.8rem; color:var(--primary); overflow:hidden; text-overflow:ellipsis;'>DISCONNECTED</div>
      <div class='gpu-metrics'>
        <div class='metric'><div class='metric-label'>TEMP</div><div id='g-temp' class='metric-val'>--°C</div></div>
        <div class='metric'><div class='metric-label'>UTIL</div><div id='g-util' class='metric-val'>--%</div></div>
        <div class='metric'><div class='metric-label'>VRAM</div><div id='g-mem' class='metric-val'>--MB</div></div>
        <div class='metric'><div class='metric-label'>CLOCK</div><div id='g-clk' class='metric-val'>--MHz</div></div>
      </div>
      <button class='btn btn-outline' onclick='gpuCmd("status")'>Host Status</button>
      <button class='btn btn-outline' style='border-color:var(--danger); color:var(--danger)' onclick='gpuCmd("unload")'>Unload GPU Model</button>
    </div>
    <div class='card glass'>
      <h3>Memory Saturation</h3>
      <div id='m-val' class='stat-val'>00<span>%</span></div>
      <div style='height:8px; width:100%; background:rgba(255,255,255,0.05); border-radius:4px; overflow:hidden;'>
        <div id='m-bar' style='height:100%; width:0%; background:var(--primary); transition:1s;'></div>
      </div>
      <p style='color:rgba(255,255,255,0.3); font-size:0.8rem; margin-top:15px;'>Current heap allocation across xtensa-lx106 architecture.</p>
    </div>
    <div class='card glass'>
      <h3>Command Center</h3>
      <input type='password' id='p' placeholder='System Auth Token'>
      <div style='display:grid; grid-template-columns:1fr 1fr; gap:10px;'>
        <button class='btn btn-primary' onclick='toggle(2,0)'>LED ON</button>
        <button class='btn btn-outline' onclick='toggle(2,1)'>LED OFF</button>
      </div>
      <button class='btn btn-outline' style='background:rgba(112,0,255,0.1);' onclick='gpuCmd("discover")'>Discover Hosts</button>
    </div>
  </div>
</div>
<script>
function update(){
  const pw = document.getElementById('p').value;
  fetch('/api/stats', { headers: {'Authorization': pw} }).then(r=>r.json()).then(d=>{
    if (d.error) return;
    document.getElementById('st-badge').className = 'badge badge-on';
    const p = Math.round((1 - d.free/81920)*100);
    document.getElementById('m-val').innerHTML = p + '<span>%</span>';
    document.getElementById('m-bar').style.width = p + '%';
  }).catch(()=>document.getElementById('st-badge').className = 'badge badge-off');
  fetch('/api/gpu', { headers: {'Authorization': pw} }).then(r=>r.json()).then(d=>{
    if (d.error) return;
    if (d.conn) {
      document.getElementById('h-name').innerText = d.host;
      document.getElementById('g-temp').innerText = d.temp + '°C';
      document.getElementById('g-util').innerText = d.util + '%';
      document.getElementById('g-mem').innerText = d.mem + 'MB';
      document.getElementById('g-clk').innerText = d.clk + 'MHz';
    } else {
      document.getElementById('h-name').innerText = 'DISCONNECTED';
    }
  });
}
async function toggle(p,v){ const pw=document.getElementById('p').value; await fetch('/api/gpio', { method: 'POST', headers: {'Content-Type': 'application/json', 'Authorization': pw}, body: JSON.stringify({pin: p, val: v}) }); }
async function gpuCmd(c){ const pw=document.getElementById('p').value; await fetch('/api/accel_cmd', { method: 'POST', headers: {'Content-Type': 'application/json', 'Authorization': pw}, body: JSON.stringify({cmd: c}) }); }
setInterval(update, 3000); update();
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
ICACHE_FLASH_ATTR void kprint_ctx(const char *s, bool fromSerial) {
  if (fromSerial)
    Serial.print(s);
  else {
    if (telnetClient && telnetClient.connected())
      telnetClient.print(s);
  }
}
ICACHE_FLASH_ATTR void kprintln_ctx(bool fromSerial) {
  kprint_ctx("\r\n", fromSerial);
}
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
void redrawPrompt() { redrawPromptAll(); }
void redrawPrompt(bool fromSerial) {
  kprint_ctx("\r\033[K", fromSerial);
  printPrompt(fromSerial);
  kprint_ctx(inputBuffer, fromSerial);
}
void redrawPromptAll() {
  redrawPrompt(true);
  if (telnetClient && telnetClient.connected())
    redrawPrompt(false);
}
void kprintLog(const String &msg, bool fromSerial) {
  if (inputLen > 0) {
    kprint_ctx("\r\033[2K", fromSerial);
    kprint_ctx(msg.c_str(), fromSerial);
    redrawPrompt(fromSerial);
  } else {
    kprint_ctx(msg.c_str(), fromSerial);
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
}
#endif
void kprint(String s) { kprint(s.c_str()); }
void kprintln(String s) { kprintln(s.c_str()); }
void kprintln(char c) {
  kprint(c);
  kprintln();
}
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
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)&__brkval);
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
ICACHE_FLASH_ATTR void printPrompt(bool fromSerial) {
  if (accelChatMode && fromSerial) {
    kprintColor_ctx_P(CLR_MAG, fromSerial);
    kprint_ctx(currentModelName, fromSerial);
    kprint_ctx("@", fromSerial);
    kprint_ctx(accelHost, fromSerial);
    kprint_ctx("> ", fromSerial);
    kprintColor_ctx_P(CLR_RST, fromSerial);
    return;
  }
  bool currentAuth = fromSerial ? serialAuthenticated : telnetAuthenticated;
  kprintColor_ctx_P(CLR_CYN, fromSerial);
  kprint_ctx(currentAuth ? "root@" : "guest@", fromSerial);
  kprint_ctx(BOARD_NAME, fromSerial);
  kprint_ctx(":", fromSerial);
  kprintColor_ctx_P(CLR_BLU, fromSerial);
  kprint_ctx(currentPath, fromSerial);
  kprintColor_ctx_P(CLR_RST, fromSerial);
  kprint_ctx(currentAuth ? "# " : "> ", fromSerial);
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
  historyCount = 0;
  historyWriteIdx = 0;
  memset(inputBuffer, 0, sizeof(inputBuffer));
  inputLen = 0;
  for (int i = 0; i < MAX_FILES; i++) {
    if ((vfs[i].flags & FLAG_ACTIVE) && !(vfs[i].flags & FLAG_ISDIR) &&
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
  if (accelConnected && historyCount > 0) {
    addDmesg(F("Memory: Swapping history to GPU Host"));
    for (int i = 0; i < historyCount; i++) {
      char key[16];
      snprintf(key, sizeof(key), "hist_%d", i);
      char swapCmd[MAX_INPUT_LEN + 32];
      snprintf(swapCmd, sizeof(swapCmd), "accel swap %s %s", key,
               cmdHistory[i]);
      dispatchCommand(swapCmd, true);
    }
    historyCount = 0;
    historyWriteIdx = 0;
    addDmesg(F("Memory: Swap Complete"));
  }
  if (historyCount > 4)
    historyCount = 4;
  yield();
}
ICACHE_FLASH_ATTR void logResetReason() {
  String reason = ESP.getResetReason();
  int vcc = ESP.getVcc();
  File f = LittleFS.open("/crash.log", "a");
  if (f) {
    if (f.size() > 5120) {
      f.close();
      File f2 = LittleFS.open("/crash.log", "w");
      if (f2) {
        f2.println(F("--- Log Rotated ---"));
        f2.close();
      }
      f = LittleFS.open("/crash.log", "a");
    }
    if (f) {
      f.print("[");
      f.print(millis() / 1000);
      f.print("] Reset: ");
      f.print(reason);
      f.print(" | VCC: ");
      f.print(vcc);
      f.println("mV");
      f.close();
    }
  }
  int idx = findFile("crash.log", "/sys");
  if (idx == -1) {
    for (int i = 0; i < MAX_FILES; i++) {
      if (!(vfs[i].flags & FLAG_ACTIVE)) {
        idx = i;
        strcpy(vfs[idx].name, "crash.log");
        strcpy(vfs[idx].parentDir, "/sys");
        vfs[idx].flags = FLAG_ACTIVE;
        vfs[idx].mode = 0444;
        vfs[idx].ownerId = 0;
        break;
      }
    }
  }
  if (idx != -1) {
    File f = LittleFS.open("/crash.log", "r");
    if (f) {
      size_t s = f.size();
      if (s > CONTENT_LEN - 1) {
        f.seek(s - (CONTENT_LEN - 1));
      }
      f.read((uint8_t *)vfs[idx].content, CONTENT_LEN - 1);
      vfs[idx].content[f.available()] = '\0';
      f.close();
    }
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
  while (((long)(millis() - windowStart)) < 2000) {
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
    EEPROM.get(EEPROM_VFS_DATA_ADDR, vfs);
    addDmesg(F("Filesystem restored from EEPROM"));
    bool hasSys = false;
    for (int i = 0; i < MAX_FILES; i++) {
      if ((vfs[i].flags & FLAG_ACTIVE) && strcmp(vfs[i].name, "sys") == 0)
        hasSys = true;
    }
    if (!hasSys)
      initFS();
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
 
  char check = 0;
  int eepromResult = EEPROM.get(EEPROM_PASS_ADDR, check);
  if (eepromResult < 0 || check == 0xFF || check == 0x00) {
    needsSetup = true;
    addDmesg(F("Security Setup Required"));
  }
  uint8_t storedFails = 0xFF;
  eepromResult = EEPROM.get(EEPROM_FAIL_COUNT_ADDR, storedFails);
  if (eepromResult >= 0 && storedFails != 0xFF) {
    loginFailCount = storedFails;
  }
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
    String payload = webServer.arg("plain");
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      webServer.send(400, "application/json",
                     _OSTR("{\"error\":\"Invalid JSON\"}"));
      return;
    }
    String pass = webServer.header("Authorization");
    if (pass.length() == 0) {
      webServer.send(401, "application/json",
                     _OSTR("{\"error\":\"Auth Required\"}"));
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
    snprintf(json, sizeof(json), "{\"free\":%d,\"up\":%lu}", ESP.getFreeHeap(),
             millis() / 1000);
    webServer.send(200, "application/json", json);
  });
  webServer.on("/api/sleep", []() {
    String auth = webServer.header("Authorization");
    if (!checkWebAuth(auth, webServer.client().remoteIP())) {
      webServer.send(401, "application/json",
                     _OSTR("{\"error\":\"Unauthorized\"}"));
      return;
    }
    int s = webServer.arg("s").toInt();
    webServer.send(200, "application/json", _OSTR("{\"status\":\"sleeping\"}"));
    delay(100);
    ESP.deepSleep(s * 1000000);
  });
  webServer.on("/api/gpu", []() {
    String auth = webServer.header("Authorization");
    if (!checkWebAuth(auth, webServer.client().remoteIP())) {
      webServer.send(401, "application/json",
                     _OSTR("{\"error\":\"Unauthorized\"}"));
      return;
    }
    char json[128];
    snprintf(json, sizeof(json),
             "{\"conn\":%d,\"host\":\"%s\",\"temp\":%d,\"util\":%d,\"mem\":%d,"
             "\"pwr\":%.1f,\"clk\":%d}",
             accelConnected ? 1 : 0, accelHost, gpuTemp, gpuUtil, gpuMem,
             gpuPwr, gpuClk);
    webServer.send(200, "application/json", json);
  });
  webServer.on("/api/accel_cmd", HTTP_POST, []() {
    String auth = webServer.header("Authorization");
    if (!checkWebAuth(auth, webServer.client().remoteIP())) {
      webServer.send(401, "application/json",
                     _OSTR("{\"error\":\"Unauthorized\"}"));
      return;
    }
    if (!webServer.hasArg("plain")) {
      webServer.send(400, "application/json", _OSTR("{\"error\":\"No body\"}"));
      return;
    }
    static JsonDocument doc;
    doc.clear();
    String payload = webServer.arg("plain");
    deserializeJson(doc, payload);
    const char *cmd = doc["cmd"];
    if (cmd) {
      char buf[64];
      snprintf(buf, sizeof(buf), "accel %s", cmd);
      dispatchCommand(buf, true);
      webServer.send(200, "application/json", _OSTR("{\"status\":\"sent\"}"));
    } else {
      webServer.send(400, "application/json", _OSTR("{\"error\":\"No cmd\"}"));
    }
  });
}
#include "include/shell.h"
ICACHE_FLASH_ATTR void executeCommand(char *line, bool fromSerial) {
  if (accelChatMode) {
    char *trimmed = kTrim(line);
    if (strncmp(trimmed, "accel", 5) != 0 && strncmp(trimmed, "chat", 4) != 0 &&
        strcmp(trimmed, "exit") != 0 && strcmp(trimmed, "logout") != 0 &&
        strcmp(trimmed, "clear") != 0 && strcmp(trimmed, "reboot") != 0) {
      char askCmd[MAX_INPUT_LEN + 16];
      snprintf(askCmd, sizeof(askCmd), "accel ask %s", trimmed);
      dispatchCommand(askCmd, fromSerial);
      return;
    }
  }
  char resolved[MAX_INPUT_LEN];
  if (resolveAlias(line, resolved)) {
    dispatchCommand(resolved, fromSerial);
  } else {
    dispatchCommand(line, fromSerial);
  }
}
ICACHE_FLASH_ATTR void processTriggers() {
  static unsigned long lastTrig = 0;
  if (((long)(millis() - lastTrig)) < 5000) 
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
void doTabCompletion(bool fromSerial) {
  if (inputLen == 0)
    return;
  inputBuffer[inputLen] = '\0';
  const char *cmds[] = {
      "ls",     "cd",    "pwd",      "cat",  "echo",     "rm",     "mkdir",
      "touch",  "wifi",  "accel",    "sys",  "help",     "clear",  "reboot",
      "uptime", "free",  "neofetch", "ping", "ifconfig", "hwinfo", "top",
      "ps",     "login", "logout",   "df",   "dmesg",    "sh"};
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
      kprint_ctx("\b \b", fromSerial);
      inputLen--;
    }
    strcpy(inputBuffer, match);
    inputLen = strlen(inputBuffer);
    kprint_ctx(inputBuffer, fromSerial);
    kprint_ctx(" ", fromSerial);
    inputBuffer[inputLen++] = ' ';
    inputBuffer[inputLen] = '\0';
  } else if (matches > 1) {
    kprintln_ctx(fromSerial);
    for (int i = 0; i < numCmds; i++) {
      if (strncmp(inputBuffer, cmds[i], strlen(inputBuffer)) == 0) {
        kprint_ctx(cmds[i], fromSerial);
        kprint_ctx("  ", fromSerial);
      }
    }
    kprintln_ctx(fromSerial);
    printPrompt(fromSerial);
    kprint_ctx(inputBuffer, fromSerial);
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

  static unsigned long lastCheck = 0;
  if (((long)(millis() - lastCheck) > 1000)) { 
  
    lastCheck = millis();
    if (telnetClient && !telnetClient.connected()) {
      telnetClient.stop();
      telnetAuthenticated = false;
      addDmesg(F("Telnet: Connection cleaned up"));
    }
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
    customBoot[NAME_LEN - 1] = '\0';
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
    printPrompt(true);
  }
  if (serialAuthenticated && isTimeout(lastSerialActivity, SESSION_TIMEOUT)) {
    serialAuthenticated = false;
    accelChatMode = false;
    Serial.println(F("\nSerial session timeout. Logged out."));
    printPrompt(true);
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
  char c = 0;
  bool hasInput = false;
  bool fromSerial = false;
  redirectionFileIdx = -1;
  if (Serial.available() > 0) {
    c = Serial.read();
    hasInput = true;
    fromSerial = true;
  }
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266) || defined(ESP32)
  if (!hasInput && telnetEnabled && telnetServer.hasClient()) {
    if (freeMemory() < 6000) {
      WiFiClient tc = telnetServer.available();
      tc.println(F("Server busy: Low memory for Telnet"));
      tc.stop();
      addDmesg(F("Telnet: Connection rejected - low memory (<6K)"));
    } else {
      WiFiClient tc = telnetServer.available();
      if (!isIpAllowed(tc.remoteIP())) {
        tc.println(F("Access Denied: Firewall Block"));
        tc.stop();
      } else if (!telnetClient || !telnetClient.connected()) {
        telnetClient = tc;
        telnetAuthenticated = false;
        printPrompt(false);
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
  }
#endif
  if (hasInput) {
    if (fromSerial)
      lastSerialActivity = millis();
    else
      lastTelnetActivity = millis();
    if (!fromSerial &&
        (isLockedOut || (((long)(millis() - lastLoginAttempt)) < loginCooldown))) { 
      if (!isLockedOut) {
        kprint_ctx("\rCooldown Active: ", fromSerial);
        char buf[12];
        ltoa((loginCooldown - (((long)(millis() - lastLoginAttempt)))) / 1000, buf, 10);  
        kprint_ctx(buf, fromSerial);
        kprint_ctx("s left.\r\n", fromSerial);
      }
      return;
    }
    if (c == '\r' || c == '\n') {
      if (c == '\n' && lastChar == '\r') {
        lastChar = c;
        return;
      }
      inputBuffer[inputLen] = '\0';
      kprintln_ctx(fromSerial);
      if (inputLen > 0) {
        executeCommand(inputBuffer, fromSerial);
        inputLen = 0;
        memset(inputBuffer, 0, sizeof(inputBuffer));
        printPrompt(fromSerial);
      } else {
        printPrompt(fromSerial);
      }
      lastChar = c;
      return;
    }
    if (c == 3) {
      inputLen = 0;
      memset(inputBuffer, 0, sizeof(inputBuffer));
      kprint_ctx("^C\r\n", fromSerial);
      printPrompt(fromSerial);
      lastChar = c;
      return;
    }
    if (c == 8 || c == 127) {
      if (inputLen > 0) {
        inputLen--;
        inputBuffer[inputLen] = '\0';
        kprint_ctx("\b \b", fromSerial);
      }
      lastChar = c;
      return;
    }
    if (c == '\t') {
      doTabCompletion(fromSerial);
      lastChar = c;
      return;
    }
    if (c >= 32 && c <= 126 && inputLen < MAX_INPUT_LEN - 1) {
      inputBuffer[inputLen++] = c;
      inputBuffer[inputLen] = '\0';
      char buf[2] = {c, 0};
      kprint_ctx(buf, fromSerial);
    } else if (c == 0x1b) {
      inEscSeq = true;
      escState = 0;
    } else if (inEscSeq) {
      if (escState == 0 && c == '[')
        escState = 1;
      else if (escState == 1) {
        if (c == 'A' || c == 'B') {
          if (historyCount > 0) {
            if (c == 'A') {
              if (historyViewIdx == -1)
                historyViewIdx =
                    (historyWriteIdx + MAX_HISTORY - 1) % MAX_HISTORY;
              else
                historyViewIdx =
                    (historyViewIdx + MAX_HISTORY - 1) % MAX_HISTORY;
            } else {
              if (historyViewIdx != -1)
                historyViewIdx = (historyViewIdx + 1) % MAX_HISTORY;
            }
            while (inputLen > 0) {
              kprint_ctx("\b \b", fromSerial);
              inputLen--;
            }
            strncpy(inputBuffer, cmdHistory[historyViewIdx], MAX_INPUT_LEN - 1);
            inputLen = strlen(inputBuffer);
            kprint_ctx(inputBuffer, fromSerial);
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
  if (strcmp_P(cmd, PSTR("top")) == 0)
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
  if (strcmp_P(cmd, PSTR("wifi")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("hwinfo")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("sys")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("color")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("env")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("alias")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("login")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("logout")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("exit")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("telnet")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("ssh")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("web")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("ota")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("netstat")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("accel")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("hf")) == 0)
    return true;
  if (strcmp_P(cmd, PSTR("chat")) == 0)
    return true;
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
  char scriptLine[MAX_INPUT_LEN];
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
  if (memoryCritical && memoryDecreased &&
      (currentTime - lastCriticalAlert > 60000)) {
    addDmesg(F("CRITICAL: Emergency memory recovery!"));
    lastCriticalAlert = currentTime;
    emergencyMemoryCleanup();
    ESP.wdtFeed();
  } else if (memoryLow && memoryDecreased &&
             (currentTime - lastWarningAlert > 120000)) {
    addDmesg(F("WARNING: Low memory - optimizing"));
    lastWarningAlert = currentTime;
    optimizeMemoryUsage();
    ESP.wdtFeed();
  } else if (free < 5000 && (currentTime - lastInfoAlert > 300000)) {
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
