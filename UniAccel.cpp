#include "UniAccel.h"
#include "include/common.h"
#include "include/commands.h"
#include "include/vfs.h"
#include "include/shell.h"
#include <ESP8266mDNS.h>

extern void kprint(const char *s);
extern void kprint(const __FlashStringHelper *s);
extern void kprint(char c);
extern void kprint(int n);
extern void kprint(unsigned long n);
extern void kprint(float f);
extern void kprint(int n, int base);
extern const char CLR_RST[] PROGMEM;
extern const char CLR_RED[] PROGMEM;
extern const char CLR_GRN[] PROGMEM;
extern const char CLR_YLW[] PROGMEM;
extern const char CLR_BLU[] PROGMEM;
extern const char CLR_MAG[] PROGMEM;
extern const char CLR_CYN[] PROGMEM;
extern const char CLR_WHT[] PROGMEM;
extern void kprintln();
extern void kprintln(const char *s);
extern void kprintln(const __FlashStringHelper *s);
extern void kprintln(int n);
extern void kprintln(unsigned long n);
extern void kprintColor(const char *c);
extern void kprintlnLog(const String &msg);
extern void addDmesg(const __FlashStringHelper *msg);

extern char currentPath[PATH_LEN];
extern volatile bool accelConnected; 
volatile bool accelAnimating = false;  
volatile bool accelChatMode = false;  
char currentModelName[32] = "None";
bool accelModelLoaded = false;
uint16_t gpuTemp = 0;
uint8_t gpuUtil = 0;
uint16_t gpuMem = 0;
float gpuPwr = 0.0f;
uint16_t gpuClk = 0;
unsigned long lastFrameTime = 0;

BuildStatus buildStatus = {0, "", "", false, 0};
bool showBuildProgress = false;

unsigned long lastRequestStartTime = 0;
static uint8_t renderBuffer[1024];
bool aiBlockStarted = false;
bool aiInCodeBlock = false;
bool aiLastWasNL = true;
int aiBtCount = 0;


static bool sessionKeyInitialized = false;

void secure_crypt(uint8_t *data, size_t len) {
    if (!sessionKeyInitialized) {
        kprintColor(CLR_RED);
        kprintln(F("ERROR: Session key not initialized!"));
        kprintColor(CLR_RST);
        return;
    }
    for (size_t i = 0; i < len; i++) {
        data[i] ^= session_key[i % 16];
    }
}

void initSessionKey() {
    sessionKeyInitialized = true;
}

ICACHE_FLASH_ATTR bool hex2int_safe(char c, uint8_t *out) {
  if (c >= '0' && c <= '9') { *out = c - '0'; return true; }
  if (c >= 'a' && c <= 'f') { *out = c - 'a' + 10; return true; }
  if (c >= 'A' && c <= 'F') { *out = c - 'A' + 10; return true; }
  return false;  
}


ICACHE_FLASH_ATTR uint8_t hex2int(char c) {
  uint8_t out;
  if (hex2int_safe(c, &out)) return out;
  return 0;  
}

ICACHE_FLASH_ATTR void displayBuildProgress(float progress, const char* phase, const char* message) {
  buildStatus.progress = progress;
  buildStatus.phase = phase;
  buildStatus.message = message;
  buildStatus.isBuilding = (progress < 100.0);

  if (showBuildProgress) {
    kprintColor(CLR_CYN);
    kprint(F("[BUILD] "));
    kprintColor(CLR_RST);
    kprintColor(CLR_YLW);
    kprint(phase);
    kprintColor(CLR_RST);
    kprint(F(" ["));
    kprintColor(CLR_GRN);
    kprint((int)progress);
    kprintColor(CLR_RST);
    kprint(F("%] "));
    kprintColor(CLR_BLU);
    kprintln(message);
    kprintColor(CLR_RST);
  }
}

ICACHE_FLASH_ATTR void displayTelemetryBox() {
  if (accelChatMode) return;

  kprintColor(CLR_CYN);
  kprintln(F("╔════════════════════════════════════════╗"));
  kprintln(F("║        GPU SYSTEM TELEMETRY           ║"));
  kprintln(F("╠════════════════════════════════════════╣"));
  kprintColor(CLR_RST);

  kprint(F("║ "));
  kprintColor(CLR_RED);
  kprint(F("◆ Temperature: "));
  kprint(gpuTemp);
  kprintln(F("°C"));
  kprintColor(CLR_RST);

  kprint(F("║ "));
  kprintColor(CLR_GRN);
  kprint(F("◆ Utilization: "));
  kprint(gpuUtil);
  kprintln(F("%"));
  kprintColor(CLR_RST);

  kprint(F("║ "));
  kprintColor(CLR_BLU);
  kprint(F("◆ VRAM Used: "));
  kprint(gpuMem);
  kprintln(F("MB"));
  kprintColor(CLR_RST);

  kprint(F("║ "));
  kprintColor(CLR_YLW);
  kprint(F("◆ Power Draw: "));
  kprint(gpuPwr);
  kprintln(F("W"));
  kprintColor(CLR_RST);

  kprint(F("║ "));
  kprintColor(CLR_MAG);
  kprint(F("◆ Clock Speed: "));
  kprint(gpuClk);
  kprintln(F("MHz"));
  kprintColor(CLR_RST);

  kprintColor(CLR_CYN);
  kprintln(F("╚════════════════════════════════════════╝"));
  kprintColor(CLR_RST);
}

ICACHE_FLASH_ATTR void initUniAccel() {
  addDmesg(F("UniAccel: Module V2.1.0-A Loaded"));
}

void redrawPrompt();

ICACHE_FLASH_ATTR void loopUniAccel() {
  if (!accelStopRequested) {
    webSocket.loop();
    if (accelConnected && ESP.getFreeHeap() < 5000) {
        for (int i = 0; i < MAX_FILES; i++) {
            if ((vfs[i].flags & FLAG_ACTIVE) && !(vfs[i].flags & FLAG_ISDIR) && strlen(vfs[i].content) > 32) {
                char swapCmd[128];
                snprintf(swapCmd, sizeof(swapCmd), "swap %s %s", vfs[i].name, vfs[i].content);
                handleAccelCommand(swapCmd);
                vfs[i].flags &= ~FLAG_ACTIVE;
                addDmesg(F("V-HEAP: Swapped file to Host to free RAM"));
                break;
            }
        }
    }
    static unsigned long lastHb = 0;
  
    if (accelConnected && ((long)(millis() - lastHb) > 10000)) {
        if (!webSocket.isConnected()) {
            accelConnected = false;
            return; 
        }
        int freeR = ESP.getFreeHeap();
        static JsonDocument hdoc; hdoc.clear();
        hdoc["cmd"] = "heartbeat"; hdoc["heap"] = freeR;
        if (freeR < 2000) hdoc["alert"] = "LOW_RAM_OVERLOAD";
        uint8_t hbuf[128]; size_t hlen = serializeMsgPack(hdoc, hbuf, sizeof(hbuf));
        if (hlen == 0) {
            kprintln(F("Error: Heartbeat serialization failed"));
            return;
        }
        secure_crypt(hbuf, hlen);
        webSocket.sendBIN(hbuf, hlen);
        lastHb = millis();
    }
    if (accelConnected && accelAnimating && ((long)(millis() - lastFrameTime) > 100)) {
      if (!webSocket.isConnected()) {
          accelConnected = false;
          return; 
      }
      static JsonDocument doc;
      doc.clear();
      doc["cmd"] = "gpu_physics";
      doc["kernel"] = "render_3d";
      uint8_t buffer[256];
      size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
      if (len == 0) {
          kprintln(F("Error: Physics command serialization failed"));
          return;
      }
      secure_crypt(buffer, len);
      lastRequestStartTime = millis();
      webSocket.sendBIN(buffer, len);
      lastFrameTime = millis();
    }
  }
}

void discoverAccelHost() {
  kprintln(F("Scanning network for GPU Host..."));
  int n = MDNS.queryService("uniaccel", "tcp");
  if (n == 0) {
    kprintln(F("No host found. Make sure UniAccelHost.py is running."));
  } else {
    kprint(F("Found host: "));
    kprintln(MDNS.hostname(0).c_str());
    strncpy(accelHost, MDNS.IP(0).toString().c_str(), 15);
    accelPort = MDNS.port(0);
    kprint(F("Target set to: "));
    kprint(accelHost);
    kprint(F(":"));
    kprintln(accelPort);
  }
}
ICACHE_FLASH_ATTR void handleAccelCommand(char *args) {
  char *argv[8];
  int argc = kParseArgs(args, argv, 8);
  if (argc == 0) {
    kprintln(F("Usage: accel [subcommand] [args]"));
    return;
  }
  char *sub = argv[0];
  char *subArgs = (argc > 1) ? argv[1] : NULL;
  if (strcmp_P(sub, PSTR("nodes")) == 0) {
      static JsonDocument doc; doc.clear(); doc["cmd"] = "cluster_list";
      uint8_t buffer[64]; size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
      secure_crypt(buffer, len); webSocket.sendBIN(buffer, len);
      kprintln(F("Scanning Cluster Nodes...")); return;
  }
  if (strcmp_P(sub, PSTR("sync")) == 0 && subArgs) {
      int fIdx = findFile(subArgs, currentPath);
      if (fIdx == -1) { kprintln(F("File not found locally")); return; }
      static JsonDocument doc; doc.clear();
      doc["cmd"] = "cluster_sync"; doc["path"] = subArgs; doc["data"] = vfs[fIdx].content;
      static uint8_t sync_buffer[2048]; size_t len = serializeMsgPack(doc, sync_buffer, sizeof(sync_buffer));
      secure_crypt(sync_buffer, len); webSocket.sendBIN(sync_buffer, len);
      kprint(F("Syncing file to cluster: ")); kprintln(subArgs); return;
  }
  if (strcmp_P(sub, PSTR("rexec")) == 0) {
      if (argc < 3) { kprintln(F("Usage: accel rexec <ip/all> <cmd>")); return; }
      char* target = argv[1];
      char e_cmd[128] = "";
      size_t remaining = sizeof(e_cmd) - 1;
      for (int i = 2; i < argc && remaining > 0; i++) {
          size_t arg_len = strlen(argv[i]);
          if (arg_len > remaining) { kprintln(F("Error: Command too long")); return; }
          strncat(e_cmd, argv[i], remaining);
          remaining -= arg_len;
          if (i < argc - 1 && remaining > 1) { strncat(e_cmd, " ", remaining--); }
      }
      static JsonDocument doc; doc.clear();
      doc["cmd"] = "cluster_exec"; doc["target"] = target; doc["exec"] = e_cmd;
      static uint8_t rexec_buffer[256]; size_t len = serializeMsgPack(doc, rexec_buffer, sizeof(rexec_buffer));
      if (len == 0) { kprintln(F("Error: Serialization failed")); return; }
      secure_crypt(rexec_buffer, len); webSocket.sendBIN(rexec_buffer, len);
      kprint(F("Rexec on ")); kprint(target); kprint(F(": ")); kprintln(e_cmd); return;
  }
  if (strcmp_P(sub, PSTR("proxy")) == 0 && argc > 1) {
      kprint(F("Entering Proxy Shell for ")); kprintln(argv[1]);
      kprintln(F("Type 'exit' to leave.")); setEnv("PROXY_TARGET", argv[1]); return;
  }
  if (strcmp_P(sub, PSTR("kvset")) == 0) {
      if (argc < 3) { kprintln(F("Usage: accel kvset <k> <v>")); return; }
      static JsonDocument doc; doc.clear();
      doc["cmd"] = "cluster_kv_set"; doc["key"] = argv[1]; doc["val"] = argv[2];
      static uint8_t kv_buffer[512]; size_t len = serializeMsgPack(doc, kv_buffer, sizeof(kv_buffer));
      secure_crypt(kv_buffer, len); webSocket.sendBIN(kv_buffer, len);
      kprint(F("Setting Global KV: ")); kprintln(argv[1]); return;
  }
  if (strcmp_P(sub, PSTR("kvget")) == 0 && argc > 1) {
      static JsonDocument doc; doc.clear(); doc["cmd"] = "cluster_kv_get"; doc["key"] = argv[1];
      static uint8_t kvget_buffer[128]; size_t len = serializeMsgPack(doc, kvget_buffer, sizeof(kvget_buffer));
      secure_crypt(kvget_buffer, len); webSocket.sendBIN(kvget_buffer, len); return;
  }
  if (strcmp_P(sub, PSTR("migrate")) == 0) {
      if (argc < 3) { kprintln(F("Usage: accel migrate <ip> <task>")); return; }
      char* target = argv[1]; char* taskName = argv[2];
      for (int i = 0; i < MAX_TASKS; i++) {
          if (taskTable[i].active && strcmp(taskTable[i].name, taskName) == 0) {
              char rexecCmd[128]; snprintf(rexecCmd, sizeof(rexecCmd), "bg %s", taskName);
              static JsonDocument doc; doc.clear();
              doc["cmd"] = "cluster_exec"; doc["target"] = target; doc["exec"] = rexecCmd;
              static uint8_t mig_buffer[256]; size_t len = serializeMsgPack(doc, mig_buffer, sizeof(mig_buffer));
              secure_crypt(mig_buffer, len); webSocket.sendBIN(mig_buffer, len);
              taskTable[i].active = false; kprint(F("Migrated task ")); kprintln(taskName); break;
          }
      } return;
  }
  if (strcmp_P(sub, PSTR("mount")) == 0 && argc > 1) {
      static JsonDocument doc; doc.clear();
      if (argc > 2 && strcmp(argv[1], "ls") == 0) {
          doc["cmd"] = "fs_ls"; doc["path"] = argv[2];
      } else {
          doc["cmd"] = "fs_read"; doc["path"] = argv[1];
      }
      static uint8_t mount_buffer[256]; size_t len = serializeMsgPack(doc, mount_buffer, sizeof(mount_buffer));
      secure_crypt(mount_buffer, len); webSocket.sendBIN(mount_buffer, len); return;
  }
  if (strcmp_P(sub, PSTR("swap")) == 0 && argc > 2) {
      static JsonDocument doc; doc.clear();
      if (strcmp(argv[1], "out") == 0 && argc > 3) {
          doc["cmd"] = "swap_out"; doc["key"] = argv[2]; doc["data"] = argv[3];
      } else {
          doc["cmd"] = "swap_in"; doc["key"] = argv[2];
      }
      static uint8_t swap_buffer[512]; size_t len = serializeMsgPack(doc, swap_buffer, sizeof(swap_buffer));
      secure_crypt(swap_buffer, len); webSocket.sendBIN(swap_buffer, len); return;
  }
  if (strcmp_P(sub, PSTR("broadcast")) == 0 && argc > 1) {
      char msg[256] = "";
      size_t remaining = sizeof(msg) - 1;
      for (int i = 1; i < argc && remaining > 0; i++) {
          size_t arg_len = strlen(argv[i]);
          if (arg_len > remaining) { kprintln(F("Error: Broadcast message too long")); return; }
          strncat(msg, argv[i], remaining);
          remaining -= arg_len;
          if (i < argc - 1 && remaining > 1) { strncat(msg, " ", remaining--); }
      }
      static JsonDocument doc; doc.clear(); doc["cmd"] = "broadcast"; doc["data"] = msg;
      static uint8_t bcast_buffer[512]; size_t len = serializeMsgPack(doc, bcast_buffer, sizeof(bcast_buffer));
      if (len == 0) { kprintln(F("Error: Serialization failed")); return; }
      secure_crypt(bcast_buffer, len); webSocket.sendBIN(bcast_buffer, len);
      kprintln(F("Broadcast request sent...")); return;
  }
  if (strcmp_P(sub, PSTR("status")) == 0 || strcmp_P(sub, PSTR("health")) == 0 || strcmp_P(sub, PSTR("top")) == 0) {
      static JsonDocument doc; doc.clear(); doc["cmd"] = "cluster_top";
      static uint8_t top_buffer[64]; size_t len = serializeMsgPack(doc, top_buffer, sizeof(top_buffer));
      secure_crypt(top_buffer, len); webSocket.sendBIN(top_buffer, len); return;
  }
  if (strcmp_P(sub, PSTR("discover")) == 0) {
      discoverAccelHost(); return;
  }
  if (strcmp_P(sub, PSTR("bench")) == 0) {
      static JsonDocument doc; doc.clear(); doc["cmd"] = "gpu_bench";
      static uint8_t bench_buffer[64]; size_t len = serializeMsgPack(doc, bench_buffer, sizeof(bench_buffer));
      secure_crypt(bench_buffer, len); webSocket.sendBIN(bench_buffer, len); return;
  }
  if (strcmp_P(sub, PSTR("physics")) == 0) {
      static JsonDocument doc; doc.clear(); doc["cmd"] = "gpu_physics";
      static uint8_t phys_buffer[64]; size_t len = serializeMsgPack(doc, phys_buffer, sizeof(phys_buffer));
      secure_crypt(phys_buffer, len); webSocket.sendBIN(phys_buffer, len); return;
  }
  if (strcmp_P(sub, PSTR("build")) == 0 || strcmp_P(sub, PSTR("compile")) == 0) {
      if (!accelConnected) {
        kprintln(F("Error: Not connected to GPU Host."));
        return;
      }
      showBuildProgress = true;
      buildStatus.startTime = millis();
      kprintColor(CLR_CYN);
      kprintln(F("╔════════════════════════════════════════╗"));
      kprintln(F("║     Starting CUDA Build Process       ║"));
      kprintln(F("╚════════════════════════════════════════╝"));
      kprintColor(CLR_RST);
      static JsonDocument doc; doc.clear(); doc["cmd"] = "build_cu";
      doc["verbose"] = true;
      static uint8_t build_buffer[128]; size_t len = serializeMsgPack(doc, build_buffer, sizeof(build_buffer));
      secure_crypt(build_buffer, len); webSocket.sendBIN(build_buffer, len);
      return;
  }
  if (strcmp_P(sub, PSTR("chat")) == 0) {
    if (!accelConnected) {
      kprintln(F("Error: Not connected to GPU Host."));
      return;
    }
    if (!accelModelLoaded) {
      kprintln(F("Error: No model loaded. Use 'accel load <model>' first."));
      return;
    }
    static JsonDocument doc;
    doc.clear();
    doc["cmd"] = "chat_start";
    uint8_t buffer[64];
    size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    if (len == 0) {
      kprintColor(CLR_RED);
      kprintln(F("Error: Failed to serialize chat_start"));
      kprintColor(CLR_RST);
      return;
    }
    secure_crypt(buffer, len);
    webSocket.sendBIN(buffer, len);
    accelChatMode = true;
    accelAnimating = false;
    kprintColor(CLR_CYN);
    kprintln(F("Chat mode enabled. Ready for interaction."));
    kprintColor(CLR_RST);
    return;
  }
  if (strcmp_P(sub, PSTR("connect")) == 0) {
    accelStopRequested = false;
    if (subArgs) {
      char host[32] = {0}; 
      int port = 81;
      if (sscanf(subArgs, "%31s %d", host, &port) >= 1) { 
        host[31] = '\0';  
        strncpy(accelHost, host, sizeof(accelHost) - 1);
        accelPort = port;
      }
    } else {
      discoverAccelHost();
    }
    if (accelHost[0] == '\0' || strcmp(accelHost, "0.0.0.0") == 0) {
      kprintColor(CLR_RED);
      kprintln(F("Error: No host IP set. Try 'accel discover' first."));
      kprintColor(CLR_RST);
      return;
    }
    kprintColor(CLR_CYN);
    kprint(F("Connecting... "));
    kprintColor(CLR_RST);
    kprint(F("to "));
    kprintColor(CLR_BLU);
    kprint(accelHost);
    kprintColor(CLR_RST);
    kprint(F(":"));
    kprintln(accelPort);
    webSocket.begin(accelHost, accelPort, "/");
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(0);
    accelRetryCount = 0;
    accelStopRequested = false;
  } else if (strcmp_P(sub, PSTR("disconnect")) == 0) {
    accelStopRequested = true;
    webSocket.disconnect();
    accelConnected = false;
    kprintln(F("Disconnected manually."));
  } else if (strcmp_P(sub, PSTR("inject")) == 0) {
    if (!accelConnected) {
      kprintln(F("Not connected."));
      return;
    }
    char filename[64] = {0};  
    if (sscanf(subArgs, "%63s", filename) == 1) {  
      filename[63] = '\0';
      int fIdx = -1;
      for (int j = 0; j < MAX_FILES; j++) {
        if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(vfs[j].name, filename) == 0 &&
            strcmp(vfs[j].parentDir, currentPath) == 0) {
          fIdx = j;
          break;
        }
      }
      if (fIdx == -1) {
        kprintln(F("File not found."));
        return;
      }
      static JsonDocument doc;
      doc.clear();
      doc["cmd"] = "gpu_inject";
      doc["code"] = vfs[fIdx].content;
      uint8_t buffer[CONTENT_LEN + 128];
      size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
      secure_crypt(buffer, len);
      webSocket.sendBIN(buffer, len);
      kprintln(F("Injecting CUDA code to Host..."));
    }
  } else if (strcmp_P(sub, PSTR("encrypt")) == 0) {
    if (!accelConnected) {
      kprintln(F("Not connected."));
      return;
    }
    if (!subArgs) {
      kprintln(F("Usage: accel encrypt [text] [key]"));
      return;
    }
    char textBuf[64] = "";
    int keyVal = 0x5A;
    sscanf(subArgs, "%63s %x", textBuf, &keyVal);
    static JsonDocument doc;
    doc.clear();
    doc["cmd"] = "gpu_encrypt";
    doc["text"] = textBuf;
    doc["key"] = (uint8_t)keyVal;
    uint8_t msgBuffer[256];
    size_t msgLen = serializeMsgPack(doc, msgBuffer, sizeof(msgBuffer));
    secure_crypt(msgBuffer, msgLen);
    accelStartTime = millis();
    webSocket.sendBIN(msgBuffer, msgLen);
    kprintColor(CLR_CYN);
    kprintln(F("Requesting GPU-Parallel XOR Encryption..."));
    kprintColor(CLR_RST);
  } else if (strcmp_P(sub, PSTR("research")) == 0) {
    if (!accelConnected) {
      kprintColor(CLR_RED);
      kprintln(F("Error: Not connected."));
      kprintColor(CLR_RST);
      return;
    }
    if (!subArgs || strlen(subArgs) == 0) {
      kprintln(F("Usage: accel research [crack/prime/match/rsa]"));
      return;
    }
    char rType[32] = {0};   
    if (sscanf(subArgs, "%31s", rType) == 1) {
      rType[31] = '\0';
      if (strcmp(rType, "crack") == 0) {
        if (strlen(subArgs) < 7) {
          kprintln(
              F("Usage: accel research crack [target_hash] [start] [range]"));
          return;
        }
        unsigned int h;
        int s, r;
        if (sscanf(subArgs + 6, "%u %d %d", &h, &s, &r) < 3) {
          kprintln(F("Error: Missing parameters."));
          kprintln(
              F("Usage: accel research crack [target_hash] [start] [range]"));
          return;
        }
        static JsonDocument doc;
        doc.clear();
        doc["cmd"] = "gpu_exec";
        doc["kernel"] = "hash_crack";
        JsonArray data = doc["data"].to<JsonArray>();
        data.add(h);
        data.add(s);
        data.add(r);
        uint8_t buf[128];
        size_t len = serializeMsgPack(doc, buf, sizeof(buf));
        secure_crypt(buf, len);
        accelStartTime = millis();
        webSocket.sendBIN(buf, len);
        kprintln(F("Security Research: Hash crack offloaded to GPU..."));
      } else if (strcmp(rType, "prime") == 0) {
      int s, r;
      if (sscanf(subArgs + 6, "%d %d", &s, &r) < 2) {
        kprintln(F("Usage: accel research prime [start] [range]"));
        return;
      }
      static JsonDocument doc;
      doc.clear();
      doc["cmd"] = "gpu_exec";
      doc["kernel"] = "prime_search";
      JsonArray data = doc["data"].to<JsonArray>();
      data.add(s);
      data.add(r);
      uint8_t buf[128];
      size_t len = serializeMsgPack(doc, buf, sizeof(buf));
      secure_crypt(buf, len);
      accelStartTime = millis();
      webSocket.sendBIN(buf, len);
      kprintln(F("Security Research: Prime search started..."));
    } else if (strcmp(rType, "match") == 0) {
      char blob[32], pat[16];
      if (sscanf(subArgs + 6, "%31s %15s", blob, pat) < 2) {
        kprintln(F("Usage: accel research match [text] [pattern]"));
        return;
      }
      static JsonDocument doc;
      doc.clear();
      doc["cmd"] = "gpu_exec";
      doc["kernel"] = "pattern_match";
      JsonArray data = doc["data"].to<JsonArray>();
      JsonArray b = data.add<JsonArray>();
      for (int i = 0; blob[i]; i++)
        b.add((uint8_t)blob[i]);
      JsonArray p = data.add<JsonArray>();
      for (int i = 0; pat[i]; i++)
        p.add((uint8_t)pat[i]);
      uint8_t buf[256];
      size_t len = serializeMsgPack(doc, buf, sizeof(buf));
      secure_crypt(buf, len);
      accelStartTime = millis();
      webSocket.sendBIN(buf, len);
      kprintln(F("Security Research: Pattern match offloaded..."));
    } else if (strcmp(rType, "rsa") == 0) {
      kprintln(F("RSA 2048-bit High-Throughput Acceleration..."));
      static JsonDocument doc;
      doc.clear();
      doc["cmd"] = "gpu_exec";
      doc["kernel"] = "rsa_2048";
      JsonArray data = doc["data"].to<JsonArray>();
      JsonArray msg = data.add<JsonArray>();
      for (int i = 0; i < 64; i++)
        msg.add((uint32_t)0xDEADBEEF);
      JsonArray exp = data.add<JsonArray>();
      for (int i = 0; i < 64; i++)
        exp.add((uint32_t)0x00000001);
      JsonArray mod = data.add<JsonArray>();
      for (int i = 0; i < 64; i++)
        mod.add((uint32_t)0xFFFFFFFF);
      data.add((uint32_t)0x12345678);
      uint8_t buf[1024];
      size_t len = serializeMsgPack(doc, buf, sizeof(buf));
      secure_crypt(buf, len);
      accelStartTime = millis();
      webSocket.sendBIN(buf, len);
      kprintln(F("Security Research: RSA 2048 offloaded to GPU..."));
    } else {
      kprintln(F("Usage: accel research [crack/prime/match/rsa]"));
    }
    }  
  } else if (strcmp_P(sub, PSTR("bench")) == 0) {
    if (!accelConnected) {
      kprintln(F("Not connected."));
      return;
    }
    kprintColor(CLR_MAG);
    kprintln(F("GPU Benchmark"));
    kprintColor(CLR_RST);
    kprintln(F("Performing Memory & Compute Stress Analysis..."));
    static JsonDocument doc;
    doc.clear();
    doc["cmd"] = "gpu_bench";
    uint8_t buf[128];
    size_t len = serializeMsgPack(doc, buf, sizeof(buf));
    secure_crypt(buf, len);
    accelStartTime = millis();
    webSocket.sendBIN(buf, len);
    kprintln(F("Request sent. Waiting for deep analysis..."));
  } else if (strcmp_P(sub, PSTR("animate")) == 0) {
    if (!accelConnected) {
      kprintln(F("Not connected."));
      return;
    }
    accelAnimating = !accelAnimating;
    if (accelAnimating) {
      kprintln(F("Starting GPU Animation... (Type 'accel animate' or 'accel "
                 "stop' to stop)"));
      kprint("\033[?25l");
    } else {
      kprintln(F("Animation stopped."));
      kprint("\033[?25h");
    }
  } else if (strcmp_P(sub, PSTR("stop")) == 0) {
    accelAnimating = false;
    kprintln(F("Stopping all acceleration tasks..."));
    kprint("\033[?25h");
  } else if (strcmp_P(sub, PSTR("discover")) == 0) {
    discoverAccelHost();
  } else if (strcmp_P(sub, PSTR("status")) == 0) {
    kprintColor(CLR_CYN);
    kprintln(F("╭── Accelerator System Status ──╮"));
    kprintColor(CLR_RST);
    kprint(F("│ Status: "));
    if (accelConnected) {
      kprintColor(CLR_GRN);
      kprint(F("CONNECTED"));
      kprintColor(CLR_RST);
      kprintln(F("         │"));
    } else {
      kprintColor(CLR_RED);
      kprint(F("DISCONNECTED"));
      kprintColor(CLR_RST);
      kprintln(F("      │"));
    }
    kprint(F("│ Host: "));
    kprint(accelHost);
    kprint(F(":"));
    kprint(accelPort);
    kprintln(F("    │"));
    kprintColor(CLR_CYN);
    kprintln(F("╰────────────────────────────╯"));
    kprintColor(CLR_RST);
  } else if (strcmp_P(sub, PSTR("list")) == 0) {
    if (!accelConnected) {
      kprintln(F("Not connected."));
      return;
    }
    static JsonDocument doc;
    doc.clear();
    doc["cmd"] = "gpu_list";
    uint8_t buffer[64];
    size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    secure_crypt(buffer, len);
    webSocket.sendBIN(buffer, len);
    kprintln(F("Requesting model presets..."));
  } else if (strcmp_P(sub, PSTR("unload")) == 0) {
    if (!accelConnected) {
      kprintln(F("Not connected."));
      return;
    }
    static JsonDocument doc;
    doc.clear();
    doc["cmd"] = "gpu_unload";
    uint8_t buffer[64];
    size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    secure_crypt(buffer, len);
    webSocket.sendBIN(buffer, len);
    kprintln(F("Sending unload request..."));
  } else if (strcmp_P(sub, PSTR("load")) == 0) {
    if (!accelConnected) {
      kprintln(F("Not connected."));
      return;
    }
    if (!subArgs) {
      kprintln(F("Usage: accel load [model_id/preset]"));
      return;
    }
    static JsonDocument doc;
    doc.clear();
    doc["cmd"] = "load_hf";
    doc["model_id"] = subArgs;
    uint8_t buffer[256];
    size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    secure_crypt(buffer, len);
    webSocket.sendBIN(buffer, len);
    kprint(F("Loading model: "));
    kprintln(subArgs);
  } else if (strcmp_P(sub, PSTR("ask")) == 0) {
    if (!accelConnected) {
      kprintln(F("Not connected."));
      return;
    }
    if (!subArgs) {
      kprintln(F("Usage: accel ask [prompt]"));
      return;
    }
    static JsonDocument doc;
    doc.clear();
    doc["cmd"] = "ask";
    doc["prompt"] = subArgs;
    uint8_t buffer[512];
    size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    if (len == 0) {
      kprintColor(CLR_RED);
      kprintln(F("Error: Failed to serialize message"));
      kprintColor(CLR_RST);
      return;
    }
    secure_crypt(buffer, len);
    webSocket.sendBIN(buffer, len);
    kprintln();
    kprintColor(CLR_MAG);
    kprint(F(" ✦ "));
    kprintColor(CLR_CYN);
    kprintln(F("Consulting Neural Engine..."));
    kprintColor(CLR_RST);
  } else if (strcmp_P(sub, PSTR("physics")) == 0) {
    if (!accelConnected) {
      kprintln(F("Not connected."));
      return;
    }
    accelAnimating = true;
    static JsonDocument doc;
    doc.clear();
    doc["cmd"] = "gpu_physics";
    uint8_t buffer[64];
    size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    secure_crypt(buffer, len);
    lastRequestStartTime = millis();
    webSocket.sendBIN(buffer, len);
    kprintln(F("Starting N-Body Physics Simulation..."));
  } else if (strcmp_P(sub, PSTR("signal")) == 0) {
    if (!accelConnected) {
      kprintln(F("Not connected."));
      return;
    }
    static JsonDocument doc;
    doc.clear();
    doc["cmd"] = "gpu_signal";
    JsonArray data = doc["data"].to<JsonArray>();
    for (int i = 0; i < 64; i++) {
        data.add(sin(i * 0.2) + ((rand() % 100) / 100.0) * 0.5);
    }
    uint8_t buffer[1024];
    size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    secure_crypt(buffer, len);
    lastRequestStartTime = millis();
    webSocket.sendBIN(buffer, len);
    kprintln(F("Offloading FFT to GPU..."));
  } else if (strcmp_P(sub, PSTR("swap")) == 0) {
    if (!accelConnected || !subArgs) return;
    char* key = strtok(subArgs, " ");
    char* val = strtok(NULL, "");
    static JsonDocument doc; doc.clear();
    if (val) {
        doc["cmd"] = "swap_out"; doc["key"] = key; doc["data"] = val;
        kprint(F("Swapping out: ")); kprintln(key);
    } else {
        doc["cmd"] = "swap_in"; doc["key"] = key;
        kprint(F("Requesting swap in: ")); kprintln(key);
    }
    uint8_t buffer[512]; size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    secure_crypt(buffer, len);
    webSocket.sendBIN(buffer, len);
  } else if (strcmp_P(sub, PSTR("mount")) == 0) {
    if (!accelConnected || !subArgs) return;
    char* mode = strtok(subArgs, " ");
    char* path = strtok(NULL, "");
    static JsonDocument doc; doc.clear();
    if (path && strcmp(mode, "ls") == 0) {
        doc["cmd"] = "fs_ls"; doc["path"] = path;
        kprint(F("Remote LS: ")); kprintln(path);
    } else {
        doc["cmd"] = "fs_read"; doc["path"] = path ? path : mode;
        kprint(F("Remote Cat: ")); kprintln(path ? path : mode);
    }
    uint8_t buffer[256]; size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    secure_crypt(buffer, len);
    webSocket.sendBIN(buffer, len);
  } else if (strcmp_P(sub, PSTR("pipe")) == 0) {
    if (!accelConnected || !subArgs) return;
    char* model = strtok(subArgs, " ");
    char* data = strtok(NULL, " ");
    char* callback = strstr(data ? data : "", "--on-match");
    if (callback) {
        *callback = '\0';
        callback = kTrim(callback + 10);
        if (callback[0] == '"') {
            callback++;
            char* end = strchr(callback, '"');
            if (end) *end = '\0';
        }
    }
    static JsonDocument doc; doc.clear();
    doc["cmd"] = "edge_pipe"; doc["model"] = model; doc["data"] = data ? data : "";
    if (callback) doc["on_match"] = callback;
    uint8_t buffer[1024]; size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    secure_crypt(buffer, len);
    webSocket.sendBIN(buffer, len);
    kprintln(F("Pushing data to Edge-AI Pipeline..."));
  } else if (strcmp_P(sub, PSTR("cluster")) == 0) {
    if (!accelConnected || !subArgs) {
        kprintln(F("Usage: accel cluster [list|top|sync|rexec|proxy|broadcast|kvset|kvget]"));
        return;
    }
    char* c_sub = strtok(subArgs, " ");
    char* c_args = strtok(NULL, "");
    static JsonDocument doc; doc.clear();
    if (strcmp(c_sub, "list") == 0) {
        doc["cmd"] = "cluster_list";
    } else if (strcmp(c_sub, "broadcast") == 0) {
        doc["cmd"] = "broadcast"; doc["data"] = c_args;
        kprintln(F("Broadcasting message..."));
    } else if (strcmp(c_sub, "rexec") == 0) {
        char* target = strtok(c_args, " ");
        char* e_cmd = strtok(NULL, "");
        if (!target || !e_cmd) { kprintln(F("Usage: rexec <ip/all> <cmd>")); return; }
        doc["cmd"] = "cluster_exec"; doc["target"] = target; doc["exec"] = e_cmd;
        kprint(F("Rexec on ")); kprint(target); kprint(F(": ")); kprintln(e_cmd);
    } else if (strcmp(c_sub, "kvset") == 0) {
        char* key = strtok(c_args, " ");
        char* val = strtok(NULL, "");
        if (!key || !val) { kprintln(F("Usage: kvset <k> <v>")); return; }
        doc["cmd"] = "cluster_kv_set"; doc["key"] = key; doc["val"] = val;
    } else if (strcmp(c_sub, "kvget") == 0) {
        if (!c_args) { kprintln(F("Usage: kvget <k>")); return; }
        doc["cmd"] = "cluster_kv_get"; doc["key"] = c_args;
    } else if (strcmp(c_sub, "top") == 0) {
        doc["cmd"] = "cluster_top";
    } else if (strcmp(c_sub, "sync") == 0) {
        if (!c_args) { kprintln(F("Usage: sync <file>")); return; }
        int fIdx = findFile(c_args, currentPath);
        if (fIdx == -1) { kprintln(F("File not found locally")); return; }
        doc["cmd"] = "cluster_sync"; doc["path"] = c_args; doc["data"] = vfs[fIdx].content;
    } else if (strcmp(c_sub, "proxy") == 0) {
        if (!c_args) { kprintln(F("Usage: proxy <ip>")); return; }
        kprint(F("Entering Proxy Shell for ")); kprintln(c_args);
        kprintln(F("Type 'exit' to leave remote shell."));
        setEnv("PROXY_TARGET", c_args);
        return;
    }
    if (!doc.isNull()) {
        uint8_t buffer[1024]; size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
        secure_crypt(buffer, len);
        webSocket.sendBIN(buffer, len);
    }
  } else {
    kprintColor_P(CLR_CYN);
    kprintln(F("╭─── \x1B[1mUniAccel Universal Dashboard (Distributed Suite)\x1B[22m ───╮"));
    kprintColor_P(CLR_WHT);
    kprintln(F("│ COMMAND / PATTERN           │ DESCRIPTION             │"));
    kprintln(F("├─────────────────────────────┼─────────────────────────┤"));
    kprintln(F("│ connect <ip> <port>         │ Link to Cluster Host    │"));
    kprintln(F("│ status / health             │ Node & GPU Telemetry    │"));
    kprintln(F("│ discover                    │ Auto-mDNS Host Search   │"));
    kprintln(F("├─────────────────────────────┼─────────────────────────┤"));
    kprintln(F("│ ask <prompt> / chat         │ AI Neural Engine        │"));
    kprintln(F("│ load <model_id>             │ Prepare AI VRAM         │"));
    kprintln(F("│ bench / physics             │ GPU Stress & Compute    │"));
    kprintln(F("├─────────────────────────────┼─────────────────────────┤"));
    kprintln(F("│ nodes                       │ Scan Cluster Topology   │"));
    kprintln(F("│ sync <file>                 │ Cluster-wide File Sync  │"));
    kprintln(F("│ rexec <ip/all> <cmd>        │ Remote Task Execution   │"));
    kprintln(F("│ migrate <ip> <task>         │ Task Workload Move      │"));
    kprintln(F("│ broadcast <msg>             │ Send Message to All     │"));
    kprintln(F("├─────────────────────────────┼─────────────────────────┤"));
    kprintln(F("│ kvset <key> <val>           │ Cluster Global RAM      │"));
    kprintln(F("│ kvget <key>                 │ Retrieve Cluster RAM    │"));
    kprintln(F("│ swap out/in <key>           │ Virtual Memory Offload  │"));
    kprintln(F("│ mount <path>                │ Remote UniFS Mapping    │"));
    kprintColor_P(CLR_CYN);
    kprintln(F("╰─────────────────────────────┴─────────────────────────╯"));
    kprintColor_P(CLR_RST);
  }
}
ICACHE_FLASH_ATTR void onGpuResponse(uint8_t *payload, size_t length) {
  if (!payload) {
    kprintColor(CLR_RED);
    kprintln(F("GPU Response: Invalid payload (null pointer)"));
    kprintColor(CLR_RST);
    return;
  }
  if (length == 0) {
    kprintColor(CLR_RED);
    kprintln(F("GPU Response: Invalid payload (zero length)"));
    kprintColor(CLR_RST);
    return;
  }
  if (length > 4096) {
    kprintColor(CLR_RED);
    kprint(F("GPU Response: Invalid payload (too large: "));
    kprint((int)length);
    kprintln(F(" bytes)"));
    kprintColor(CLR_RST);
    return;
  }
  secure_crypt(payload, length);
  static JsonDocument res;
  res.clear();
  DeserializationError error = deserializeMsgPack(res, payload, length);
  if (error) {
    error = deserializeJson(res, payload, length);
    if (error) {
      kprintColor(CLR_RED);
      kprint(F("GPU Decode Error: "));
      kprintln(error.c_str());
      kprintColor(CLR_RST);
      return;
    }
  }
  if (!res.containsKey("status") || res["status"].isNull()) {
    kprintColor(CLR_RED);
    kprintln(F("GPU Response: Missing or invalid status field"));
    kprintColor(CLR_RST);
    return;
  }
  const char* status = res["status"].as<const char*>();
  if (!status) {
    kprintColor(CLR_RED);
    kprintln(F("GPU Response: Status field is null"));
    kprintColor(CLR_RST);
    return;
  }
  if (res.containsKey("build_status") && !res["build_status"].isNull()) {
    JsonObject bs = res["build_status"];
    if (bs.containsKey("progress") && !bs["progress"].isNull() &&
        bs.containsKey("phase") && !bs["phase"].isNull() &&
        bs.containsKey("message") && !bs["message"].isNull()) {
      float progress = bs["progress"].as<float>();
      const char* phase = bs["phase"].as<const char*>();
      const char* msg = bs["message"].as<const char*>();
      if (phase && msg) {
        displayBuildProgress(progress, phase, msg);
        if (progress >= 100.0) {
          showBuildProgress = false;
          kprintColor(CLR_GRN);
          kprintln(F("✓ Build completed successfully!"));
          kprintColor(CLR_RST);
        }
      }
    }
  }
  if (strcmp(status, "ok") == 0) {
    bool isRender = false;
    if (res.containsKey("kernel") && !res["kernel"].isNull()) {
      const char* kernel = res["kernel"].as<const char*>();
      if (kernel) isRender = (strcmp(kernel, "render_3d") == 0);
    }
    int side = 0;
    if (accelAnimating && isRender) {
      if (res.containsKey("height") && !res["height"].isNull()) {
        side = res["height"].as<int>();
      } else {
        side = 24;
      }
      for (int i = 0; i < side + 2; i++)
        kprint("\033[A");
    }
    if (res.containsKey("telemetry") && !res["telemetry"].isNull() && !accelChatMode) {
      JsonObject tel = res["telemetry"];
      if (tel.containsKey("temp") && !tel["temp"].isNull() &&
          tel.containsKey("util") && !tel["util"].isNull() &&
          tel.containsKey("mem") && !tel["mem"].isNull() &&
          tel.containsKey("pwr") && !tel["pwr"].isNull() &&
          tel.containsKey("clk") && !tel["clk"].isNull()) {
        gpuTemp = tel["temp"].as<int>();
        gpuUtil = tel["util"].as<int>();
        gpuMem = tel["mem"].as<int>();
        gpuPwr = tel["pwr"].as<float>();
        gpuClk = tel["clk"].as<int>();
        bool useBox = false;
        if (res.containsKey("display_format") && !res["display_format"].isNull()) {
          const char* fmt = res["display_format"].as<const char*>();
          if (fmt) useBox = (strcmp(fmt, "box") == 0);
        }
        if (useBox) {
          displayTelemetryBox();
        } else {
          kprintColor(CLR_CYN);
          kprint(F("[GPU] "));
          kprintColor(CLR_RST);
          kprintColor(CLR_RED);
          kprint(gpuTemp);
          kprint(F("°C"));
          kprintColor(CLR_RST);
          kprint(F(" | "));
        kprintColor(CLR_GRN);
        kprint(gpuUtil);
        kprint(F("% Util"));
        kprintColor(CLR_RST);
        kprint(F(" | "));
        kprintColor(CLR_BLU);
        kprint(gpuMem);
        kprint(F("MB"));
        kprintColor(CLR_RST);
        kprint(F(" | "));
        kprintColor(CLR_YLW);
        kprint(gpuPwr);
        kprint(F("W"));
        kprintColor(CLR_RST);
        kprint(F(" | "));
        kprintColor(CLR_MAG);
        kprint(gpuClk);
        kprint(F("MHz"));
        kprintColor(CLR_RST);
        kprintln();
      }
    }
    if (res.containsKey("cmd") && !res["cmd"].isNull()) {
        const char* cmd = res["cmd"].as<const char*>();
        if (!cmd) return;
        if (strcmp(cmd, "swap_ack") == 0) {
            if (res.containsKey("key") && !res["key"].isNull()) {
              kprint(F("[SWAP] Ack for key: ")); kprintln(res["key"].as<const char*>());
            }
        } else if (strcmp(cmd, "swap_data") == 0) {
            if (res.containsKey("data") && !res["data"].isNull()) {
              kprint(F("[SWAP] Received: ")); kprintln(res["data"].as<const char*>());
            }
        } else if (strcmp(cmd, "fs_content") == 0) {
            kprintln(F("╭─── Remote File Content ───────────────────────────"));
            if (res.containsKey("data") && !res["data"].isNull()) {
              String content = res["data"].as<String>();
              kprintln(content.c_str());
            }
            kprintln(F("╰───────────────────────────────────────────────────"));
        } else if (strcmp(cmd, "fs_list") == 0) {
            if (res.containsKey("path") && !res["path"].isNull()) {
              kprint(F("Remote Directory: ")); kprintln(res["path"].as<const char*>());
            }
            if (res.containsKey("files") && !res["files"].isNull()) {
              JsonArray files = res["files"].as<JsonArray>();
              for (JsonVariant f : files) {
                if (!f.isNull()) kprint(F("  - ")), kprintln(f.as<const char*>());
              }
            }
            if (res.containsKey("callback") && !res["callback"].isNull()) {
                const char* cb = res["callback"].as<const char*>();
                if (cb) {
                  kprint(F("[PIPE] Triggering callback: ")); kprintln(cb);
                  dispatchCommand((char*)cb, true);
                }
            }
        } else if (strcmp(cmd, "cluster_msg") == 0) {
            kprintColor_P(CLR_MAG);
            if (res.containsKey("from") && !res["from"].isNull()) {
              kprint(F("[CLUSTER:")); kprint(res["from"].as<const char*>()); kprint(F("] "));
            }
            kprintColor_P(CLR_RST);
            if (res.containsKey("data") && !res["data"].isNull()) {
              kprintln(res["data"].as<const char*>());
            }
        } else if (strcmp(cmd, "cluster_list") == 0) {
            kprintColor_P(CLR_CYN); kprintln(F("╭── Cluster Nodes Discovery ──────────────────────────╮"));
            kprintln(F("│ IP Address        │ Uptime(s) │ Requests │ Status   │"));
            kprintln(F("├───────────────────┼───────────┼──────────┼──────────┤"));
            kprintColor_P(CLR_RST);
            if (res.containsKey("nodes") && !res["nodes"].isNull()) {
              JsonArray nodes = res["nodes"].as<JsonArray>();
              for (JsonVariant n : nodes) {
                if (n.isNull() || !n.is<JsonObject>()) continue;
                if (!n.containsKey("ip") || !n.containsKey("uptime") || !n.containsKey("reqs")) continue;
                char buf[80];
                snprintf(buf, sizeof(buf), "│ %-17s │ %-9d │ %-8d │ ",
                    n["ip"].as<const char*>(), n["uptime"].as<int>(), n["reqs"].as<int>());
                kprint(buf);
                kprintColor_P(CLR_GRN); kprint(F("ONLINE")); kprintColor_P(CLR_RST);
                kprintln(F("   │"));
              }
            }
            kprintColor_P(CLR_CYN);
            kprintln(F("╰───────────────────┴───────────┴──────────┴──────────╯"));
            kprintColor_P(CLR_RST);
        } else if (strcmp(cmd, "remote_exec") == 0) {
            if (res.containsKey("from") && !res["from"].isNull() &&
                res.containsKey("exec_cmd") && !res["exec_cmd"].isNull()) {
              kprintColor_P(CLR_RED);
              kprint(F("! [CLUSTER-EXEC] ")); kprintColor_P(CLR_YLW); kprint(res["from"].as<const char*>());
              kprintColor_P(CLR_WHT); kprint(F(" orders: ")); kprintColor_P(CLR_CYN); kprintln(res["exec_cmd"].as<const char*>());
              kprintColor_P(CLR_RST);
              dispatchCommand((char*)res["exec_cmd"].as<const char*>(), false);
            }
        } else if (strcmp(cmd, "kv_update") == 0 || strcmp(cmd, "kv_data") == 0) {
            if (res.containsKey("key") && !res["key"].isNull() &&
                res.containsKey("val") && !res["val"].isNull()) {
              kprintColor_P(CLR_MAG); kprint(F("[GLOBAL-KV] "));
              kprintColor_P(CLR_CYN); kprint(res["key"].as<const char*>());
              kprintColor_P(CLR_WHT); kprint(F(" -> "));
              kprintColor_P(CLR_GRN); kprintln(res["val"].as<const char*>());
              kprintColor_P(CLR_RST);
            }
        } else if (strcmp(cmd, "cluster_top") == 0) {
            kprintColor_P(CLR_YLW);
            kprintln(F("╭── Global Cluster System Monitor ────────────────────╮"));
            kprintln(F("│ IP Address        │ Heap Free │ Uptime   │ Requests │"));
            kprintln(F("├───────────────────┼───────────┼──────────┼──────────┤"));
            kprintColor_P(CLR_RST);
            if (res.containsKey("nodes") && !res["nodes"].isNull()) {
              JsonArray nodes = res["nodes"].as<JsonArray>();
              for (JsonVariant n : nodes) {
                if (n.isNull() || !n.is<JsonObject>()) continue;
                if (!n.containsKey("ip") || !n.containsKey("heap") || !n.containsKey("uptime") || !n.containsKey("reqs")) continue;
                int heap = n["heap"].as<int>();
                char buf[80];
                snprintf(buf, sizeof(buf), "│ %-17s │ ", n["ip"].as<const char*>());
                kprint(buf);
                if (heap < 10000) kprintColor_P(CLR_RED);
                else if (heap < 20000) kprintColor_P(CLR_YLW);
                else kprintColor_P(CLR_GRN);
                snprintf(buf, sizeof(buf), "%-9d", heap); kprint(buf);
                kprintColor_P(CLR_RST);
                snprintf(buf, sizeof(buf), " │ %-8d │ %-8d │", n["uptime"].as<int>(), n["reqs"].as<int>());
                kprintln(buf);
              }
            }
            kprintColor_P(CLR_YLW);
            kprintln(F("╰───────────────────┴───────────┴──────────┴──────────╯"));
            kprintColor_P(CLR_RST);
        } else if (strcmp(cmd, "fs_sync") == 0) {
            if (res.containsKey("path") && !res["path"].isNull() &&
                res.containsKey("data") && !res["data"].isNull()) {
              const char* path = res["path"].as<const char*>();
              const char* data = res["data"].as<const char*>();
              if (!path || !data) { kprintln(F("[CLUSTER-SYNC:ERR] Malformed packet")); return; }
              int fIdx = findFile(path, "/");
              if (fIdx != -1) {
                  strncpy(vfs[fIdx].content, data, sizeof(vfs[fIdx].content)-1);
                  kprintColor_P(CLR_GRN); kprint(F("[CLUSTER-SYNC:UPDATE] ")); kprintColor_P(CLR_WHT); kprintln(path); kprintColor_P(CLR_RST);
              } else {
                  int newIdx = -1;
                  for (int i = 0; i < MAX_FILES; i++) {
                      if (!(vfs[i].flags & FLAG_ACTIVE)) {
                          newIdx = i;
                          break;
                      }
                  }
                  if (newIdx != -1) {
                      vfs[newIdx].flags |= FLAG_ACTIVE;
                      strncpy(vfs[newIdx].name, path, sizeof(vfs[newIdx].name)-1);
                      strncpy(vfs[newIdx].parentDir, "/", sizeof(vfs[newIdx].parentDir)-1);
                      strncpy(vfs[newIdx].content, data, sizeof(vfs[newIdx].content)-1);
                      kprintColor_P(CLR_GRN); kprint(F("[CLUSTER-SYNC:NEW] ")); kprintColor_P(CLR_WHT); kprintln(path); kprintColor_P(CLR_RST);
                  } else {
                      kprintln(F("[CLUSTER-SYNC:ERR] No VFS slots available"));
                  }
              }
            }
        } else if (strcmp(cmd, "proxy_in") == 0) {
            if (res.containsKey("from") && !res["from"].isNull() &&
                res.containsKey("data") && !res["data"].isNull()) {
              const char* from = res["from"].as<const char*>();
              const char* data = res["data"].as<const char*>();
              if (!from || !data) return;
              kprintColor_P(CLR_MAG);
              kprint(F("╭─ REMOTE SESSION: ")); kprint(from); kprintln(F(" ─╮"));
              kprintColor_P(CLR_RST);
              kprint(F("│ CMD: ")); kprintln(data);
              kprintColor_P(CLR_MAG);
              kprintln(F("╰─────────────────────────────────────╯"));
              kprintColor_P(CLR_RST);
              setEnv("PROXY_MASTER", from);
              dispatchCommand((char*)data, false);
            }
        } else if (strcmp(cmd, "node_fs_req") == 0) {
            if (res.containsKey("from") && !res["from"].isNull() &&
                res.containsKey("path") && !res["path"].isNull() &&
                res.containsKey("action") && !res["action"].isNull()) {
              const char* from = res["from"].as<const char*>();
              const char* path = res["path"].as<const char*>();
              const char* action = res["action"].as<const char*>();
              if (!from || !path || !action) return;
              static JsonDocument rdoc; rdoc.clear();
              rdoc["cmd"] = "node_fs_res"; rdoc["target"] = from; rdoc["path"] = path;
              if (strcmp(action, "list") == 0) {
                  JsonArray files = rdoc["files"].to<JsonArray>();
                  for (int i = 0; i < 16; i++) {
                      if ((vfs[i].flags & FLAG_ACTIVE) && strcmp(vfs[i].parentDir, path) == 0) {
                          files.add(vfs[i].name);
                      }
                  }
              } else if (strcmp(action, "read") == 0) {
                  int fIdx = findFile(path + (path[0] == '/' ? 1 : 0), "/");
                  if (fIdx != -1) rdoc["data"] = vfs[fIdx].content;
                  else rdoc["data"] = "File not found";
              }
              uint8_t rbuf[1024]; size_t rlen = serializeMsgPack(rdoc, rbuf, sizeof(rbuf));
              secure_crypt(rbuf, rlen);
              webSocket.sendBIN(rbuf, rlen);
            }
        } else if (strcmp(cmd, "node_fs_data") == 0) {
            if (res.containsKey("path") && !res["path"].isNull()) {
              kprintColor_P(CLR_CYN);
              kprint(F("╭── Remote Node File System [")); kprint(res["path"].as<const char*>()); kprintln(F("] ──╮"));
              kprintColor_P(CLR_RST);
              if (res.containsKey("files") && !res["files"].isNull()) {
                  JsonArray files = res["files"].as<JsonArray>();
                  for (JsonVariant f : files) {
                      if (!f.isNull()) {
                        kprint(F("  - ")); kprintln(f.as<const char*>());
                      }
                  }
              } else if (res.containsKey("data") && !res["data"].isNull()) {
                  kprintln(res["data"].as<const char*>());
              }
              kprintColor_P(CLR_CYN);
              kprintln(F("╰──────────────────────────────────────────────────╯"));
              kprintColor_P(CLR_RST);
            }
        } else if (strcmp(cmd, "proxy_data") == 0) {
            if (res.containsKey("from") && !res["from"].isNull() &&
                res.containsKey("data") && !res["data"].isNull()) {
              kprintColor_P(CLR_CYN);
              kprint(F("[")); kprint(res["from"].as<const char*>()); kprint(F("] "));
              kprintColor_P(CLR_RST);
              kprintln(res["data"].as<const char*>());
            }
        }
        if (res.containsKey("model_id")) {
            const char* full_id = res["model_id"].as<const char*>();
            if (full_id) {
                const char* last_slash = strrchr(full_id, '/');
                const char* short_name = (last_slash) ? last_slash + 1 : full_id;
                strncpy(currentModelName, short_name, sizeof(currentModelName)-1);
            }
        }
    }
    if (res.containsKey("exec_cmd") && !res["exec_cmd"].isNull()) {
        const char* e_cmd = res["exec_cmd"].as<const char*>();
        if (e_cmd) {
          kprintColor_P(CLR_YLW);
          kprint(F(" ✦ [AGENT] Executing: ")); kprintln(e_cmd);
          kprintColor_P(CLR_RST);
          dispatchCommand((char*)e_cmd, false);
        }
    }
    if (res.containsKey("kernel") && !res["kernel"].isNull() && strcmp(res["kernel"].as<const char*>(), "render_3d") == 0) {
      int w = res.containsKey("width") && !res["width"].isNull() ? res["width"].as<int>() : 0;
      int h = res.containsKey("height") && !res["height"].isNull() ? res["height"].as<int>() : 0;
      if (w <= 48) {
        float* dPtr = nullptr;
        uint8_t* uPtr = nullptr;
        int tot = 0;
        if (res.containsKey("hex") && !res["hex"].isNull()) {
            const char* hex = res["hex"].as<const char*>();
            if (hex && strlen(hex) >= 2) {
              int hexLen = strlen(hex);
              tot = hexLen / 2;
              if (tot > 1024) tot = 1024;
              bool validHex = true;
              for (int i = 0; i < tot; i++) {
                  uint8_t hi, lo;
                 
                  if (!hex2int_safe(hex[i*2], &hi) || !hex2int_safe(hex[i*2+1], &lo)) {
                      kprintColor(CLR_RED);
                      kprintln(F("Invalid hex data in render command"));
                      kprintColor(CLR_RST);
                      validHex = false;
                      break;
                  }
                  renderBuffer[i] = (hi << 4) | lo;
              }
              if (validHex) uPtr = renderBuffer;
            }
        } else if (res.containsKey("bin") && !res["bin"].isNull()) {
            size_t bLen = res["data"].size();
            if (bLen == w * h) {
                uPtr = (uint8_t*)res["data"].as<const char*>();
                tot = bLen;
            } else {
                dPtr = (float*)res["data"].as<const char*>();
                tot = bLen / 4;
            }
        } else {
            JsonArray data = res["data"];
            tot = data.size();
        }
        if (dPtr || uPtr || res["data"].is<JsonArray>()) {
            int s = w > 0 ? w : sqrt(tot);
            kprintln(F("--- GPU 3D RENDER ---"));
            for (int i = 0; i < tot; i++) {
                float v = 0;
                if (dPtr) v = dPtr[i];
                else if (uPtr) v = uPtr[i] / 255.0f;
                else v = res["data"][i].as<float>();
                if (v > 0.8f) kprint(F("@"));
                else if (v > 0.6f) kprint(F("#"));
                else if (v > 0.4f) kprint(F("*"));
                else if (v > 0.2f) kprint(F("."));
                else kprint(F(" "));
                if ((i + 1) % s == 0) kprintln();
            }
            if (!accelAnimating) kprintln(F("-------------------------------"));
        }
      } else {
        kprint(F("High-Res Complete ("));
        if (w > 0) { kprint(w); kprint(F("x")); kprint(h); }
        else kprint(F("Unknown Res"));
        kprintln(F(") - No Draw"));
      }
    } else if (res.containsKey("cmd") && strcmp(res["cmd"], "gpu_bench") == 0) {
      JsonObject data = res["data"];
      kprintColor_P(CLR_GRN);
      kprintln(F("\nBenchmark Results"));
      kprintColor_P(CLR_RST);
      kprint(F(" - Memory Bandwidth: "));
      kprintColor_P(CLR_YLW);
      kprint(data["bandwidth_gbs"].as<float>());
      kprintln(F(" GB/s"));
      kprintColor_P(CLR_RST);
      kprint(F(" - Compute Throughput: "));
      kprintColor_P(CLR_YLW);
      kprint(data["compute_gflops"].as<float>());
      kprintln(F(" GFLOPS"));
      kprintColor_P(CLR_RST);
      kprint(F(" - Shared Memory Acc: "));
      kprintColor_P(CLR_YLW);
      kprint(data["shm_lat_ms"].as<float>());
      kprintln(F(" ms"));
      kprintColor_P(CLR_RST);
      kprint(F(" - Atomic Ops Speed: "));
      kprintColor_P(CLR_YLW);
      kprint(data["atomic_ms"].as<float>());
      kprintln(F(" ms"));
      kprintColor_P(CLR_RST);
      kprint(F(" - Kernel Launch Lat: "));
      kprintColor_P(CLR_YLW);
      kprint(data["launch_lat_us"].as<float>());
      kprintln(F(" us"));
      kprintColor_P(CLR_RST);
      kprintln(F("--------------------------------------"));
    } else if ((res.containsKey("cmd") &&
                strcmp(res["cmd"], "gpu_encrypt") == 0) ||
               (res.containsKey("kernel") &&
                strcmp(res["kernel"], "encrypt") == 0)) {
      JsonArray data = res["data"];
      kprint(F("GPU Cipher: "));
      for (size_t i = 0; i < data.size(); i++) {
        int v = data[i];
        if (v < 16)
          kprint(F("0"));
        kprint(v, HEX);
      }
      kprintln();
      kprint(F("Latency: "));
      kprint(res["compute_ms"].as<float>());
      kprintln(F("ms"));
    } else if (res.containsKey("kernel") && strcmp(res["kernel"], "rsa_2048") == 0) {
        if (res.containsKey("bin")) {
            uint32_t* rPtr = (uint32_t*)res["data"].as<const char*>();
            int rLen = res["data"].size() / 4;
            kprint(F("RSA 2048 Result: "));
            for(int i=0; i<rLen && i<8; i++) {
                kprint(rPtr[i], HEX); kprint(F(" "));
            }
        }
    } else if (res.containsKey("kernel") && strcmp(res["kernel"], "signal_fft") == 0) {
        JsonArray data = res["data"];
        kprintln(F("\nFFT Magnitude Spectrum:"));
        for (int i = 0; i < data.size() / 2; i++) {
            float v = data[i];
            int bars = (int)(v * 20);
            kprint(i); kprint(F(": "));
            for(int j=0; j<bars; j++) kprint(F("|"));
            kprintln();
        }
    } else if (res.containsKey("kernel") && strcmp(res["kernel"], "cluster_list") == 0) {
        JsonArray nodes = res["data"];
        kprintColor_P(CLR_CYN);
        kprintln(F("\n--- UniKernel Cluster Nodes ---"));
        kprintColor_P(CLR_RST);
        for (JsonObject node : nodes) {
            kprint(F("Node: ")); kprint(node["ip"].as<const char*>());
            kprint(F(" | Req: ")); kprint(node["req"].as<int>());
            kprint(F(" | Uptime: ")); kprint(node["uptime"].as<int>());
            kprintln(F("s"));
        }
        if (res.containsKey("dashboard")) {
            kprint(F("Dashboard: "));
            kprintColor_P(CLR_YLW);
            kprintln(res["dashboard"].as<const char*>());
            kprintColor_P(CLR_RST);
        }
        kprintColor_P(CLR_CYN);
        kprintln(F("-------------------------------"));
        kprintColor_P(CLR_RST);
    } else if (res.containsKey("cmd") && strcmp(res["cmd"], "ask_delta") == 0) {
        if (res.containsKey("data")) {
            String delta = res["data"].as<String>();
            if (!aiBlockStarted) {
                kprintColor_P(CLR_CYN);
                kprintln(F("╭─── AI Response ───────────────────────────────────"));
                kprintColor_P(CLR_RST);
                aiBlockStarted = true;
                aiLastWasNL = true;
                aiInCodeBlock = false;
                aiBtCount = 0;
            }
            for (int i=0; i<delta.length(); i++) {
                if (aiLastWasNL) {
                    kprintColor_P(CLR_CYN);
                    kprint(F("│ "));
                    kprintColor_P(CLR_RST);
                    if (aiInCodeBlock) kprintColor_P(CLR_YLW);
                    aiLastWasNL = false;
                }
                char c = delta[i];
                if (c == '`') {
                    aiBtCount++;
                    if (aiBtCount == 3) {
                        aiInCodeBlock = !aiInCodeBlock;
                        aiBtCount = 0;
                        kprintColor_P(aiInCodeBlock ? CLR_YLW : CLR_RST);
                    }
                    kprint(c);
                } else {
                    aiBtCount = 0;
                    kprint(c);
                    if (c == '\n') {
                        kprint('\r');
                        aiLastWasNL = true;
                        if (aiInCodeBlock) kprintColor_P(CLR_RST);
                    }
                }
            }
        }
    } else if (res.containsKey("cmd") && strcmp(res["cmd"], "ask_end") == 0) {
        kprintColor_P(CLR_CYN);
        kprintln(F("╰───────────────────────────────────────────────────"));
        kprintColor_P(CLR_RST);
        aiBlockStarted = false;
        aiInCodeBlock = false;
        aiLastWasNL = true;
        aiBtCount = 0;
        if (accelChatMode) redrawPrompt();
    } else {
      if (res.containsKey("message")) {
        const char* msg = res["message"].as<const char *>();
        kprintln(msg);
        if (strstr(msg, "Model") && (strstr(msg, "loaded") || strstr(msg, "ONLINE"))) accelModelLoaded = true;
        if (strstr(msg, "Unloaded")) accelModelLoaded = false;
        if (strstr(msg, "Current Model:")) accelModelLoaded = true;
      }
      if (res.containsKey("data")) {
        String output;
        if (res["data"].is<JsonArray>() || res["data"].is<JsonObject>()) {
          serializeJson(res["data"], output);
        } else {
          output = res["data"].as<String>();
        }
        kprintln(output.c_str());
        if (output.indexOf("Current Model:") >= 0) accelModelLoaded = true;
      }
    }
    } 
    if (res.containsKey("compute_ms") && !accelChatMode && !accelAnimating) {
      unsigned long rtt = ((long)(millis() - lastRequestStartTime));
      kprint(F("Compute: "));
      kprint(res["compute_ms"].as<float>());
      kprint(F("ms | RTT: "));
      kprint(rtt);
      kprintln(F("ms"));
    }
  } else if (res.containsKey("status") && !res["status"].isNull() && strcmp(res["status"].as<const char*>(), "info") == 0) {
    kprintColor_P(CLR_CYN);
    kprint(F("[GPU] "));
    kprintColor_P(CLR_RST);
    if (res.containsKey("message") && !res["message"].isNull()) {
      kprintln(res["message"].as<const char *>());
    }
  } else {
    kprintColor_P(CLR_RED);
    kprint(F("GPU Error: "));
    if (res.containsKey("message") && !res["message"].isNull()) {
      kprintln(res["message"].as<const char *>());
    }
    kprintColor_P(CLR_RST);
  }
}

ICACHE_FLASH_ATTR void webSocketEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
  case WStype_DISCONNECTED:
    if (accelStopRequested)
      break;
    accelConnected = false;
    accelRetryCount++;
    {
      kprintColor_P(CLR_RED);
      kprint(F("[System] GPU Link Lost. Re-syncing ("));
      kprint(accelRetryCount);
      kprintln(F("/3)..."));
      kprintColor_P(CLR_RST);
      accelChatMode = false;
    }
    if (accelRetryCount >= 3) {
      accelStopRequested = true;
      webSocket.setReconnectInterval(0);
      webSocket.disconnect();
      kprintColor_P(CLR_RED);
      kprintln(F("[CRITICAL] GPU Acceleration Offline. Link failed after 3 attempts."));
      kprintColor_P(CLR_RST);
    }
    break;
  case WStype_CONNECTED:
    accelConnected = true;
    accelModelLoaded = false;
    initSessionKey();  
    MDNS.update();
    accelRetryCount = 0;
    addDmesg(F("UniAccel: Connected"));
    kprint(F("\n"));
    kprintColor_P(CLR_GRN);
    kprintln(F("Connected to GPU Host Successfully"));
    kprintColor_P(CLR_RST);
    {
      static JsonDocument doc;
      doc.clear();
      doc["cmd"] = "gpu_list";
      uint8_t buffer[64];
      size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
      if (len > 0) {
        secure_crypt(buffer, len);
        webSocket.sendBIN(buffer, len);
      }
    }
    break;
  case WStype_TEXT:
  case WStype_BIN:
    onGpuResponse(payload, length);
    break;
  case WStype_ERROR:
    kprintln(F("\nWebSocket Error"));
    break;
  }
}

ICACHE_FLASH_ATTR void accelExec(const char *kernel, JsonArray data) {
  if (!accelConnected)
    return;
  static JsonDocument doc;
  doc.clear();
  doc["cmd"] = "gpu_exec";
  doc["kernel"] = kernel;
  doc["data"] = data;
  uint8_t buffer[256];
  size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
  if (len == 0) {
    kprintColor(CLR_RED);
    kprintln(F("Error: gpu_exec serialization failed"));
    kprintColor(CLR_RST);
    return;
  }
  secure_crypt(buffer, len);
  accelStartTime = millis();
  webSocket.sendBIN(buffer, len);
}

ICACHE_FLASH_ATTR void handleHfCommand(char *args) {
  if (!accelConnected) {
    kprintln(F("Error: Not connected to GPU Host."));
    return;
  }
  char sub[16] = {0};
  if (sscanf(args, "%15s", sub) != 1) {
    kprintln(F("Error: Invalid command format"));
    return;
  }
  char *subArgs = strchr(args, ' ');
  if (subArgs) subArgs++;
  if (strcmp(sub, "token") == 0) {
    if (!subArgs) {
      kprintln(F("Usage: hf token <token>"));
      return;
    }
    static JsonDocument doc;
    doc.clear();
    doc["cmd"] = "hf_token";
    doc["token"] = subArgs;
    uint8_t buffer[256];
    size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    if (len == 0) { kprintln(F("Error: Serialization failed")); return; }
    secure_crypt(buffer, len);
    webSocket.sendBIN(buffer, len);
    kprintln(F("Sending token to GPU Host..."));
  } else if (strcmp(sub, "status") == 0) {
    static JsonDocument doc;
    doc.clear();
    doc["cmd"] = "hf_status";
    uint8_t buffer[64];
    size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    if (len == 0) { kprintln(F("Error: Serialization failed")); return; }
    secure_crypt(buffer, len);
    webSocket.sendBIN(buffer, len);
  } else if (strcmp(sub, "offline") == 0) {
    static JsonDocument doc;
    doc.clear();
    doc["cmd"] = "hf_offline";
    doc["value"] = true;
    uint8_t buffer[64];
    size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    if (len == 0) { kprintln(F("Error: Serialization failed")); return; }
    secure_crypt(buffer, len);
    webSocket.sendBIN(buffer, len);
    kprintln(F("Enabling Offline Mode..."));
  } else if (strcmp(sub, "list") == 0) {
    JsonDocument doc;
    doc["cmd"] = "hf_list";
    uint8_t buffer[64];
    size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    if (len == 0) { kprintln(F("Error: Serialization failed")); return; }
    secure_crypt(buffer, len);
    webSocket.sendBIN(buffer, len);
  } else if (strcmp(sub, "help") == 0) {
    kprintln(F("HF CLI Usage:"));
    kprintln(F("  hf token <key> : Set HF_TOKEN for gated models"));
    kprintln(F("  hf status      : Show GPU Host HF authentication status"));
    kprintln(F("  hf offline     : Force offline mode for local model loading"));
    kprintln(F("  hf list        : List models currently cached on GPU Host"));
  } else {
    kprintln(F("Usage: hf [token/status/offline/list/help]"));
  }
}

ICACHE_FLASH_ATTR void sendProxyData(const char* target, const char* msg) {
    if (!accelConnected) return;
    JsonDocument doc;
    doc["cmd"] = "proxy_out"; doc["target"] = target; doc["data"] = msg;
    uint8_t buffer[512]; size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    secure_crypt(buffer, len);
    webSocket.sendBIN(buffer, len);
}
