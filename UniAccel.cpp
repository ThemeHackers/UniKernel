#include "UniAccel.h"
#include <ESP8266mDNS.h>

extern void kprint(const char* s);
extern void kprint(const __FlashStringHelper* s);
extern void kprint(int n);
extern void kprint(int n, int base);
extern void kprintln();
extern void kprintln(const char* s);
extern void kprintln(const __FlashStringHelper* s);
extern void kprintln(int n);
extern void kprintln(unsigned long n);
extern void kprintColor(const char* c);
extern void kprintlnLog(const String& msg);
extern void addDmesg(const __FlashStringHelper* msg);

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

void initUniAccel() {
}

void loopUniAccel() {
    if (!accelStopRequested) {
        webSocket.loop();
    }
}

void discoverAccelHost() {
    kprintln(F("[UniAccel] Scanning network for GPU Host..."));
    int n = MDNS.queryService("uniaccel", "tcp");
    if (n == 0) {
        kprintln(F("[UniAccel] No host found. Make sure UniAccelHost.py is running."));
    } else {
        kprint(F("[UniAccel] Found host: ")); kprintln(MDNS.hostname(0).c_str());
        strncpy(accelHost, MDNS.IP(0).toString().c_str(), 15);
        accelPort = MDNS.port(0);
        kprint(F("[UniAccel] Target set to: ")); kprint(accelHost); kprint(F(":")); kprintln(accelPort);
    }
}

void handleAccelCommand(char* args) {
    char sub[16] = {0};
    sscanf(args, "%15s", sub);
    char *subArgs = strchr(args, ' ');
    if (subArgs) subArgs++;

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
            kprintln(F("[UniAccel] Error: No host IP set. Try 'accel discover' first."));
            kprintColor(CLR_RST);
            return;
        }
        
        kprintColor(CLR_CYN); kprint(F("[UniAccel] ")); kprintColor(CLR_RST);
        kprint(F("Connecting to ")); kprintColor(CLR_BLU); kprint(accelHost); kprintColor(CLR_RST);
        kprint(F(":")); kprintln(accelPort);
        webSocket.begin(accelHost, accelPort, "/");
        webSocket.onEvent(webSocketEvent);
        webSocket.setReconnectInterval(0); 
        accelRetryCount = 0;
        accelStopRequested = false;
    } else if (strcmp_P(sub, PSTR("disconnect")) == 0) {
        accelStopRequested = true;
        webSocket.disconnect();
        accelConnected = false;
        kprintln(F("[UniAccel] Disconnected manually."));
    } else if (strcmp_P(sub, PSTR("inject")) == 0) {
        if (!accelConnected) { kprintln(F("Not connected.")); return; }
        char filename[16];
        sscanf(subArgs, "%15s", filename);
        int fIdx = -1;
        for (int j = 0; j < MAX_FILES; j++) {
            if ((vfs[j].flags & FLAG_ACTIVE) && strcmp(vfs[j].name, filename) == 0 &&
                strcmp(vfs[j].parentDir, currentPath) == 0) {
                fIdx = j; break;
            }
        }
        if (fIdx == -1) { kprintln(F("File not found.")); return; }
        
        JsonDocument doc;
        doc["cmd"] = "gpu_inject";
        doc["code"] = vfs[fIdx].content;
        uint8_t buffer[CONTENT_LEN + 128];
        size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
        for(size_t i=0; i<len; i++) buffer[i] ^= XOR_KEY;
        webSocket.sendBIN(buffer, len);
        kprintln(F("[UniAccel] Injecting CUDA code to Host..."));

    } else if (strcmp_P(sub, PSTR("encrypt")) == 0) {
        if (!accelConnected) { kprintln(F("Not connected.")); return; }
        if (!subArgs) { kprintln(F("Usage: accel encrypt [text] [key]")); return; }
        
        char textBuf[64] = "";
        int keyVal = 0x5A;
        sscanf(subArgs, "%63s %x", textBuf, &keyVal);
        
        JsonDocument doc;
        doc["cmd"] = "gpu_encrypt";
        doc["text"] = textBuf;
        doc["key"] = (uint8_t)keyVal;
        
        uint8_t msgBuffer[256];
        size_t msgLen = serializeMsgPack(doc, msgBuffer, sizeof(msgBuffer));
        for(size_t i=0; i<msgLen; i++) msgBuffer[i] ^= XOR_KEY;
        
        accelStartTime = millis();
        webSocket.sendBIN(msgBuffer, msgLen);
        kprintColor(CLR_CYN);
        kprintln(F("[UniAccel] Requesting GPU-Parallel XOR Encryption..."));
        kprintColor(CLR_RST);
    } else if (strcmp_P(sub, PSTR("research")) == 0) {
        if (!accelConnected) { 
            kprintColor(CLR_RED);
            kprintln(F("[UniAccel] Error: Not connected.")); 
            kprintColor(CLR_RST);
            return; 
        }
        if (!subArgs || strlen(subArgs) == 0) {
            kprintln(F("Usage: accel research crack [target_hash] [start] [range]"));
            return;
        }
        char rType[16] = {0};
        sscanf(subArgs, "%15s", rType);
        
        if (strcmp(rType, "crack") == 0) {
            if (strlen(subArgs) < 7) {
                kprintln(F("Usage: accel research crack [target_hash] [start] [range]"));
                return;
            }
            unsigned int h; int s, r;
            if (sscanf(subArgs + 6, "%u %d %d", &h, &s, &r) < 3) {
                kprintln(F("Error: Missing parameters."));
                kprintln(F("Usage: accel research crack [target_hash] [start] [range]"));
                return;
            }
            JsonDocument doc;
            doc["cmd"] = "gpu_exec";
            doc["kernel"] = "hash_crack";
            JsonArray data = doc["data"].to<JsonArray>();
            data.add(h); data.add(s); data.add(r);
            
            uint8_t buf[128];
            size_t len = serializeMsgPack(doc, buf, sizeof(buf));
            for(size_t i=0; i<len; i++) buf[i] ^= XOR_KEY;
            
            accelStartTime = millis();
            webSocket.sendBIN(buf, len);
            kprintln(F("[UniAccel] Security Research: Hash crack offloaded to GPU..."));
        } else if (strcmp(rType, "prime") == 0) {
            int s, r;
            if (sscanf(subArgs + 6, "%d %d", &s, &r) < 2) {
                kprintln(F("Usage: accel research prime [start] [range]"));
                return;
            }
            JsonDocument doc;
            doc["cmd"] = "gpu_exec";
            doc["kernel"] = "prime_search";
            JsonArray data = doc["data"].to<JsonArray>();
            data.add(s); data.add(r);
            
            uint8_t buf[128];
            size_t len = serializeMsgPack(doc, buf, sizeof(buf));
            for(size_t i=0; i<len; i++) buf[i] ^= XOR_KEY;
            
            accelStartTime = millis();
            webSocket.sendBIN(buf, len);
            kprintln(F("[UniAccel] Security Research: Prime search started..."));
        } else if (strcmp(rType, "match") == 0) {
            char blob[32], pat[16];
            if (sscanf(subArgs + 6, "%31s %15s", blob, pat) < 2) {
                kprintln(F("Usage: accel research match [text] [pattern]"));
                return;
            }
            JsonDocument doc;
            doc["cmd"] = "gpu_exec";
            doc["kernel"] = "pattern_match";
            JsonArray data = doc["data"].to<JsonArray>();
            JsonArray b = data.add<JsonArray>();
            for(int i=0; blob[i]; i++) b.add((uint8_t)blob[i]);
            JsonArray p = data.add<JsonArray>();
            for(int i=0; pat[i]; i++) p.add((uint8_t)pat[i]);
            
            uint8_t buf[256];
            size_t len = serializeMsgPack(doc, buf, sizeof(buf));
            for(size_t i=0; i<len; i++) buf[i] ^= XOR_KEY;
            
            accelStartTime = millis();
            webSocket.sendBIN(buf, len);
            kprintln(F("[UniAccel] Security Research: Pattern match offloaded..."));
        } else {
            kprintln(F("Usage: accel research [crack/prime/match]"));
        }
    } else if (strcmp_P(sub, PSTR("bench")) == 0) {
        if (!accelConnected) { kprintln(F("Not connected.")); return; }
        kprintColor(CLR_MAG);
        kprintln(F("UniAccel GPU Benchmark"));
        kprintColor(CLR_RST);
        kprintln(F("Performing Memory & Compute Stress Analysis..."));
        
        JsonDocument doc;
        doc["cmd"] = "gpu_bench";
        
        uint8_t buf[128];
        size_t len = serializeMsgPack(doc, buf, sizeof(buf));
        for(size_t j=0; j<len; j++) buf[j] ^= XOR_KEY;
        
        accelStartTime = millis();
        webSocket.sendBIN(buf, len);
        kprintln(F("Request sent. Waiting for deep analysis..."));
    } else if (strcmp_P(sub, PSTR("discover")) == 0) {
        discoverAccelHost();
    } else if (strcmp_P(sub, PSTR("status")) == 0) {
        kprintColor(CLR_CYN);
        kprintln(F("╭── UniAccel System Status ──╮"));
        kprintColor(CLR_RST);
        kprint(F("│ Status: ")); 
        if (accelConnected) {
            kprintColor(CLR_GRN); kprint(F("CONNECTED")); kprintColor(CLR_RST);
            kprintln(F("         │"));
        } else {
            kprintColor(CLR_RED); kprint(F("DISCONNECTED")); kprintColor(CLR_RST);
            kprintln(F("      │"));
        }
        kprint(F("│ Host: ")); kprint(accelHost); kprint(F(":")); kprint(accelPort);
        kprintln(F("    │"));
        kprintColor(CLR_CYN);
        kprintln(F("╰────────────────────────────╯"));
        kprintColor(CLR_RST);
    } else {
        kprintColor(CLR_CYN);
        kprint(F("[UniAccel] "));
        kprintColor(CLR_RST);
        kprintln(F("Usage: accel [connect/discover/research/bench/inject/encrypt/status/disconnect]"));
    }
}

void onGpuResponse(uint8_t * payload, size_t length) {
    for(size_t i=0; i<length; i++) payload[i] ^= XOR_KEY;
    
    JsonDocument res;
    DeserializationError error = deserializeMsgPack(res, payload);
    if (error) {
        error = deserializeJson(res, payload);
        if (error) {
            kprintColor(CLR_RED);
            kprintln(F("[UniAccel] Error: Invalid response format"));
            kprintColor(CLR_RST);
            return;
        }
    }
    
    if (strcmp(res["status"], "ok") == 0) {
        if (res.containsKey("telemetry")) {
            JsonObject tel = res["telemetry"];
            kprintColor(CLR_CYN); kprint(F("[GPU] ")); kprintColor(CLR_RST);
            kprintColor(CLR_RED); kprint(tel["temp"].as<int>()); kprint(F("°C")); kprintColor(CLR_RST); kprint(F(" | "));
            kprintColor(CLR_GRN); kprint(tel["util"].as<int>()); kprint(F("% Util")); kprintColor(CLR_RST); kprint(F(" | "));
            kprintColor(CLR_BLU); kprint(tel["mem"].as<int>()); kprint(F("MB")); kprintColor(CLR_RST); kprint(F(" | "));
            kprintColor(CLR_YLW); kprint(tel["pwr"].as<float>()); kprint(F("W")); kprintColor(CLR_RST); kprint(F(" | "));
            kprintColor(CLR_MAG); kprint(tel["clk"].as<int>()); kprintln(F("MHz")); kprintColor(CLR_RST);
        }
        if (res.containsKey("kernel") && strcmp(res["kernel"], "render_3d") == 0) {
            int w = res.containsKey("width") ? res["width"].as<int>() : 0;
            int h = res.containsKey("height") ? res["height"].as<int>() : 0;
            
            if (w <= 32 && res["data"].is<JsonArray>()) { 
                JsonArray data = res["data"];
                int total = data.size();
                int side = sqrt(total);
                kprintln(F("--- GPU 3D RENDER ---"));
                for (int i = 0; i < total; i++) {
                    float val = data[i];
                    if (val > 0) kprint(F("#")); else kprint(F("."));
                    if ((i + 1) % side == 0) kprintln();
                }
            kprintln(F("-------------------------------"));
            } else {
                kprint(F("[UniAccel] High-Res Render Complete ("));
                if (w > 0) {
                    kprint(w); kprint(F("x")); kprint(h);
                } else {
                    kprint(F("Unknown Res"));
                }
                kprintln(F(") - Skipping ASCII Draw"));
            }
        } else if (res.containsKey("cmd") && strcmp(res["cmd"], "gpu_bench") == 0) {
            JsonObject data = res["data"];
            kprintColor(CLR_GRN);
            kprintln(F("\n[Benchmark Results]"));
            kprintColor(CLR_RST);
            kprint(F(" - Memory Bandwidth: ")); kprintColor(CLR_YLW); kprint(data["bandwidth_gbs"].as<float>()); kprintln(F(" GB/s")); kprintColor(CLR_RST);
            kprint(F(" - Compute Throughput: ")); kprintColor(CLR_YLW); kprint(data["compute_gflops"].as<float>()); kprintln(F(" GFLOPS")); kprintColor(CLR_RST);
            kprint(F(" - Shared Memory Acc: ")); kprintColor(CLR_YLW); kprint(data["shm_lat_ms"].as<float>()); kprintln(F(" ms")); kprintColor(CLR_RST);
            kprint(F(" - Atomic Ops Speed: ")); kprintColor(CLR_YLW); kprint(data["atomic_ms"].as<float>()); kprintln(F(" ms")); kprintColor(CLR_RST);
            kprint(F(" - Kernel Launch Lat: ")); kprintColor(CLR_YLW); kprint(data["launch_lat_us"].as<float>()); kprintln(F(" us")); kprintColor(CLR_RST);
            kprintln(F("--------------------------------------"));
        } else if ((res.containsKey("cmd") && strcmp(res["cmd"], "gpu_encrypt") == 0) || 
                   (res.containsKey("kernel") && strcmp(res["kernel"], "encrypt") == 0)) {
            JsonArray data = res["data"];
            kprint(F("[UniAccel] GPU Cipher: "));
            for (size_t i = 0; i < data.size(); i++) {
                int v = data[i];
                if (v < 16) kprint(F("0"));
                kprint(v, HEX);
            }
            kprintln();
            kprint(F("[UniAccel] Latency: ")); kprint(res["compute_ms"].as<float>()); kprintln(F("ms"));
        } else {
            kprint(F("[UniAccel] Result: "));
            String output;
            if (res["data"].is<JsonArray>() || res["data"].is<JsonObject>()) {
                serializeJson(res["data"], output);
            } else {
                output = res["data"].as<String>();
            }
            kprintln(output.c_str());
        }
        if (res.containsKey("compute_ms")) {
            unsigned long rtt = millis() - accelStartTime;
            kprint(F("[Stats] Compute: ")); kprint(res["compute_ms"].as<float>());
            kprint(F("ms | RTT: ")); kprint(rtt); kprintln(F("ms"));
        }
    } else {
        kprintColor(CLR_RED);
        kprint(F("[UniAccel] GPU Error: "));
        kprintln(res["message"].as<const char*>());
        kprintColor(CLR_RST);
    }
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            if (accelStopRequested) break; 
            accelConnected = false;
            accelRetryCount++;
            {
              String m = "[UniAccel] Disconnected from GPU Host (" + String(accelRetryCount) + "/3)";
              kprintlnLog(m);
            }
            if (accelRetryCount >= 3) {
                accelStopRequested = true;
                webSocket.setReconnectInterval(0);
                webSocket.disconnect();
                kprintlnLog(F("[UniAccel] Connection failed 3 times. Stopping auto-reconnect."));
            }
            break;
        case WStype_CONNECTED:
            accelConnected = true;
            accelRetryCount = 0; 
            addDmesg(F("UniAccel: Connected"));
            kprint(F("\n")); kprintColor(CLR_GRN);
            kprintln(F("[UniAccel] Connected to GPU Host Successfully"));
            kprintColor(CLR_RST);
            break;
        case WStype_TEXT:
        case WStype_BIN:
            onGpuResponse(payload, length);
            break;
        case WStype_ERROR:
            kprintln(F("\n[UniAccel] WebSocket Error"));
            break;
    }
}

void accelExec(const char* kernel, JsonArray data) {
    if (!accelConnected) return;
    JsonDocument doc;
    doc["cmd"] = "gpu_exec";
    doc["kernel"] = kernel;
    doc["data"] = data;
    
    uint8_t buffer[256];
    size_t len = serializeMsgPack(doc, buffer, sizeof(buffer));
    for(size_t i=0; i<len; i++) buffer[i] ^= XOR_KEY;
    
    accelStartTime = millis();
    webSocket.sendBIN(buffer, len);
}
