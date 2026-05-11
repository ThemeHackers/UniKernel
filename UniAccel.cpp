#include "UniAccel.h"
#include <ESP8266mDNS.h>

extern void kprint(const char *s);
extern void kprint(const __FlashStringHelper *s);
extern void kprint(char c);
extern void kprint(int n);
extern void kprint(unsigned long n);
extern void kprint(float f);
extern void kprint(int n, int base);
extern void kprintln();
extern void kprintln(const char *s);
extern void kprintln(const __FlashStringHelper *s);
extern void kprintln(int n);
extern void kprintln(unsigned long n);
extern void kprintColor(const char *c);
extern void kprintlnLog(const String &msg);
extern void addDmesg(const __FlashStringHelper *msg);

#define MAX_FILES 16
#define FLAG_ACTIVE 0x01
#define NAME_LEN 12
#define PATH_LEN 16
#define CONTENT_LEN 128

typedef struct {
  char name[NAME_LEN];
  char content[CONTENT_LEN];
  char parentDir[PATH_LEN];
  uint8_t flags;
  uint16_t mode;
  uint8_t ownerId;
} RAMFile;

extern RAMFile vfs[MAX_FILES];
extern char currentPath[PATH_LEN];
bool accelAnimating = false;
bool accelChatMode = false;
char currentModelName[32] = "tinyllama";
unsigned long lastFrameTime = 0;

bool accelModelLoaded = false;
unsigned long lastRequestStartTime = 0;
static uint8_t renderBuffer[1024];

uint8_t hex2int(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

void initUniAccel() {
  addDmesg(F("UniAccel: Module V2.1.0-A Loaded"));
}

void redrawPrompt(); 

void loopUniAccel() {
  if (!accelStopRequested) {
    webSocket.loop();
    if (accelConnected && accelAnimating && (millis() - lastFrameTime > 100)) {
      static JsonDocument doc;
      doc.clear();
   
      doc["cmd"] = "gpu_physics"; 
      doc["kernel"] = "render_3d";
      uint8_t buffer[256];
      size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
      for (size_t i = 0; i < len; i++) buffer[i] ^= XOR_KEY;
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
void handleAccelCommand(char *args) {
  char sub[16] = {0};
  sscanf(args, "%15s", sub);
  char *subArgs = strchr(args, ' ');
  if (subArgs)
    subArgs++;

  if (strcmp_P(sub, PSTR("chat")) == 0) {
    if (!accelConnected) {
      kprintln(F("Error: Not connected to GPU Host."));
      return;
    }
    if (!accelModelLoaded) {
      kprintln(F("Error: No model loaded. Use 'accel load <model>' first."));
      return;
    }
    accelChatMode = true;
    accelAnimating = false; 
    return;
  }

  if (strcmp_P(sub, PSTR("connect")) == 0) {
    accelStopRequested = false;
    if (subArgs) {
      char host[16];
      int port = 81;
      if (sscanf(subArgs, "%15s %d", host, &port) >= 1) {
        strncpy(accelHost, host, 15);
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
    char filename[16];
    sscanf(subArgs, "%15s", filename);
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
    for (size_t i = 0; i < len; i++)
      buffer[i] ^= XOR_KEY;
    webSocket.sendBIN(buffer, len);
    kprintln(F("Injecting CUDA code to Host..."));

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
    for (size_t i = 0; i < msgLen; i++)
      msgBuffer[i] ^= XOR_KEY;

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
    char rType[16] = {0};
    sscanf(subArgs, "%15s", rType);

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
      for (size_t i = 0; i < len; i++)
        buf[i] ^= XOR_KEY;

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
      for (size_t i = 0; i < len; i++)
        buf[i] ^= XOR_KEY;

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
      for (size_t i = 0; i < len; i++)
        buf[i] ^= XOR_KEY;

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
      for (size_t i = 0; i < len; i++)
        buf[i] ^= XOR_KEY;

      accelStartTime = millis();
      webSocket.sendBIN(buf, len);
      kprintln(F("Security Research: RSA 2048 offloaded to GPU..."));

    } else {
      kprintln(F("Usage: accel research [crack/prime/match/rsa]"));
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
    for (size_t j = 0; j < len; j++)
      buf[j] ^= XOR_KEY;

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
    for (size_t i = 0; i < len; i++)
      buffer[i] ^= XOR_KEY;
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
    for (size_t i = 0; i < len; i++)
      buffer[i] ^= XOR_KEY;
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
    for (size_t i = 0; i < len; i++)
      buffer[i] ^= XOR_KEY;
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
    for (size_t i = 0; i < len; i++)
      buffer[i] ^= XOR_KEY;
    webSocket.sendBIN(buffer, len);
    kprintln(F("Thinking..."));
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
    for (size_t i = 0; i < len; i++) buffer[i] ^= XOR_KEY;
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
    for (size_t i = 0; i < len; i++) buffer[i] ^= XOR_KEY;
    lastRequestStartTime = millis();
    webSocket.sendBIN(buffer, len);
    kprintln(F("Offloading FFT to GPU..."));
  } else if (strcmp_P(sub, PSTR("cluster")) == 0) {
    if (!accelConnected) {
      kprintln(F("Not connected."));
      return;
    }
    static JsonDocument doc;
    doc.clear();
    doc["cmd"] = "gpu_cluster_list";
    uint8_t buffer[64];
    size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    for (size_t i = 0; i < len; i++) buffer[i] ^= XOR_KEY;
    lastRequestStartTime = millis();
    webSocket.sendBIN(buffer, len);
    kprintln(F("Requesting Cluster Node List..."));
  } else {

    kprintColor(CLR_CYN);
    kprint(F(""));
    kprintColor(CLR_RST);
    kprintln(
        F("Usage: accel "
          "[connect/load/ask/chat/animate/stop/list/unload/discover/research/bench/"
          "status/physics/signal/cluster/disconnect]"));
  }
}

void onGpuResponse(uint8_t *payload, size_t length) {
  for (size_t i = 0; i < length; i++)
    payload[i] ^= XOR_KEY;

  static JsonDocument res;
  res.clear();
  DeserializationError error = deserializeMsgPack(res, payload);
  if (error) {
    error = deserializeJson(res, payload);
    if (error) {
      kprintColor(CLR_RED);
      kprint(F("GPU Decode Error: "));
      kprintln(error.c_str());
      kprintColor(CLR_RST);
      return;
    }
  }

  if (strcmp(res["status"], "ok") == 0) {
    bool isRender =
        res.containsKey("kernel") && strcmp(res["kernel"], "render_3d") == 0;
    int side = 0;

    if (accelAnimating && isRender) {
      side = res.containsKey("height") ? res["height"].as<int>() : 24;

      for (int i = 0; i < side + 2; i++)
        kprint("\033[A");
    }

    if (res.containsKey("telemetry") && !accelChatMode) {
      JsonObject tel = res["telemetry"];
      kprintColor(CLR_CYN);
      kprint(F("[GPU] "));
      kprintColor(CLR_RST);
      kprintColor(CLR_RED);
      kprint(tel["temp"].as<int>());
      kprint(F("°C"));
      kprintColor(CLR_RST);
      kprint(F(" | "));
      kprintColor(CLR_GRN);
      kprint(tel["util"].as<int>());
      kprint(F("% Util"));
      kprintColor(CLR_RST);
      kprint(F(" | "));
      kprintColor(CLR_BLU);
      kprint(tel["mem"].as<int>());
      kprint(F("MB"));
      kprintColor(CLR_RST);
      kprint(F(" | "));
      kprintColor(CLR_YLW);
      kprint(tel["pwr"].as<float>());
      kprint(F("W"));
      kprintColor(CLR_RST);
      kprint(F(" | "));
      kprintColor(CLR_MAG);
      kprint(tel["clk"].as<int>());
      kprintln(F("MHz"));
      kprintColor(CLR_RST);
    }
    if (res.containsKey("kernel") && strcmp(res["kernel"], "render_3d") == 0) {
      int w = res.containsKey("width") ? res["width"].as<int>() : 0;
      int h = res.containsKey("height") ? res["height"].as<int>() : 0;

      if (w <= 48) {
        float* dPtr = nullptr; 
        uint8_t* uPtr = nullptr;
        int tot = 0;
        if (res.containsKey("hex")) {
            const char* hex = res["data"].as<const char*>();
            int hexLen = strlen(hex);
            tot = hexLen / 2;
            if (tot > 1024) tot = 1024;
            for (int i = 0; i < tot; i++) {
                renderBuffer[i] = (hex2int(hex[i*2]) << 4) | hex2int(hex[i*2+1]);
            }
            uPtr = renderBuffer;
        } else if (res.containsKey("bin")) {
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
      kprintColor(CLR_GRN);
      kprintln(F("\nBenchmark Results"));

      kprintColor(CLR_RST);
      kprint(F(" - Memory Bandwidth: "));
      kprintColor(CLR_YLW);
      kprint(data["bandwidth_gbs"].as<float>());
      kprintln(F(" GB/s"));
      kprintColor(CLR_RST);
      kprint(F(" - Compute Throughput: "));
      kprintColor(CLR_YLW);
      kprint(data["compute_gflops"].as<float>());
      kprintln(F(" GFLOPS"));
      kprintColor(CLR_RST);
      kprint(F(" - Shared Memory Acc: "));
      kprintColor(CLR_YLW);
      kprint(data["shm_lat_ms"].as<float>());
      kprintln(F(" ms"));
      kprintColor(CLR_RST);
      kprint(F(" - Atomic Ops Speed: "));
      kprintColor(CLR_YLW);
      kprint(data["atomic_ms"].as<float>());
      kprintln(F(" ms"));
      kprintColor(CLR_RST);
      kprint(F(" - Kernel Launch Lat: "));
      kprintColor(CLR_YLW);
      kprint(data["launch_lat_us"].as<float>());
      kprintln(F(" us"));
      kprintColor(CLR_RST);
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
        kprintColor(CLR_CYN);
        kprintln(F("\n--- UniKernel Cluster Nodes ---"));
        kprintColor(CLR_RST);
        for (JsonObject node : nodes) {
            kprint(F("Node: ")); kprint(node["ip"].as<const char*>());
            kprint(F(" | Req: ")); kprint(node["req"].as<int>());
            kprint(F(" | Uptime: ")); kprint(node["uptime"].as<int>());
            kprintln(F("s"));
        }
        if (res.containsKey("dashboard")) {
            kprint(F("Dashboard: "));
            kprintColor(CLR_YLW);
            kprintln(res["dashboard"].as<const char*>());
            kprintColor(CLR_RST);
        }
        kprintColor(CLR_CYN);
        kprintln(F("-------------------------------"));
        kprintColor(CLR_RST);
    } else if (res.containsKey("cmd") && strcmp(res["cmd"], "ask_delta") == 0) {
        if (res.containsKey("data")) {
            String delta = res["data"].as<String>();
            static bool inCodeBlock = false;
            static int btCount = 0;
            
            for (int i=0; i<delta.length(); i++) {
                char c = delta[i];
                if (c == '`') {
                    btCount++;
                    if (btCount == 3) {
                        inCodeBlock = !inCodeBlock;
                        btCount = 0;
                        kprintColor(inCodeBlock ? CLR_YLW : CLR_GRN);
                    }
                    kprint(c);
                } else {
                    btCount = 0;
                    kprint(c);
                    if (c == '\n') {
                        kprint('\r');
                        if (inCodeBlock) kprint(F("  "));
                    }
                }
            }
        }
    } else if (res.containsKey("cmd") && strcmp(res["cmd"], "ask_end") == 0) {
        kprintln();
        kprintColor(CLR_CYN);
        kprintln(F("-------------------------------"));
        kprintColor(CLR_RST);
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
    if (res.containsKey("compute_ms") && !accelChatMode && !accelAnimating) {
      unsigned long rtt = millis() - lastRequestStartTime;
      kprint(F("Compute: "));
      kprint(res["compute_ms"].as<float>());
      kprint(F("ms | RTT: "));
      kprint(rtt);
      kprintln(F("ms"));
    }

  } else if (res.containsKey("status") && strcmp(res["status"], "info") == 0) {
    kprintColor(CLR_CYN);
    kprint(F("[GPU] "));
    kprintColor(CLR_RST);
    kprintln(res["message"].as<const char *>());
  } else {
    kprintColor(CLR_RED);
    kprint(F("GPU Error: "));
    kprintln(res["message"].as<const char *>());
    kprintColor(CLR_RST);
  }
}

void webSocketEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
  case WStype_DISCONNECTED:
    if (accelStopRequested)
      break;
    accelConnected = false;
    accelRetryCount++;
    {
      kprintColor(CLR_RED);
      kprint(F("[System] GPU Link Lost. Re-syncing ("));
      kprint(accelRetryCount);
      kprintln(F("/3)..."));
      kprintColor(CLR_RST);
      accelChatMode = false;
    }
    if (accelRetryCount >= 3) {
      accelStopRequested = true;
      webSocket.setReconnectInterval(0);
      webSocket.disconnect();
      kprintColor(CLR_RED);
      kprintln(F("[CRITICAL] GPU Acceleration Offline. Link failed after 3 attempts."));
      kprintColor(CLR_RST);
    }

    break;
  case WStype_CONNECTED:
    accelConnected = true;
    accelModelLoaded = false;
    MDNS.update();
    accelRetryCount = 0;
    addDmesg(F("UniAccel: Connected"));
    kprint(F("\n"));
    kprintColor(CLR_GRN);
    kprintln(F("Connected to GPU Host Successfully"));
    kprintColor(CLR_RST);
    {
      static JsonDocument doc;
      doc.clear();
      doc["cmd"] = "gpu_list";
      uint8_t buffer[64];
      size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
      for (size_t i = 0; i < len; i++)
        buffer[i] ^= XOR_KEY;
      webSocket.sendBIN(buffer, len);
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

void accelExec(const char *kernel, JsonArray data) {
  if (!accelConnected)
    return;
  static JsonDocument doc;
  doc.clear();
  doc["cmd"] = "gpu_exec";
  doc["kernel"] = kernel;
  doc["data"] = data;

  uint8_t buffer[256];
  size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
  for (size_t i = 0; i < len; i++)
    buffer[i] ^= XOR_KEY;

  accelStartTime = millis();
  webSocket.sendBIN(buffer, len);
}

void handleHfCommand(char *args) {
  if (!accelConnected) {
    kprintln(F("Error: Not connected to GPU Host."));
    return;
  }
  char sub[16] = {0};
  sscanf(args, "%15s", sub);
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
    for (size_t i = 0; i < len; i++) buffer[i] ^= XOR_KEY;
    webSocket.sendBIN(buffer, len);
    kprintln(F("Sending token to GPU Host..."));
  } else if (strcmp(sub, "status") == 0) {
    static JsonDocument doc;
    doc.clear();
    doc["cmd"] = "hf_status";
    uint8_t buffer[64];
    size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    for (size_t i = 0; i < len; i++) buffer[i] ^= XOR_KEY;
    webSocket.sendBIN(buffer, len);
  } else if (strcmp(sub, "offline") == 0) {
    static JsonDocument doc;
    doc.clear();
    doc["cmd"] = "hf_offline";
    doc["value"] = true;
    uint8_t buffer[64];
    size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    for (size_t i = 0; i < len; i++) buffer[i] ^= XOR_KEY;
    webSocket.sendBIN(buffer, len);
    kprintln(F("Enabling Offline Mode..."));
  } else if (strcmp(sub, "list") == 0) {
    JsonDocument doc;
    doc["cmd"] = "hf_list";
    uint8_t buffer[64];
    size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    for (size_t i = 0; i < len; i++) buffer[i] ^= XOR_KEY;
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
