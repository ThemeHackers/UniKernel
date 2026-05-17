#ifndef UNIACCEL_H
#define UNIACCEL_H
#include <Arduino.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#ifndef SESSION_KEY
static uint8_t session_key[16] = {0x5A, 0xA5, 0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
#endif
#define BUILD_PHASE_SETUP    "setup"
#define BUILD_PHASE_PREPARE  "prepare"
#define BUILD_PHASE_COMPILE  "compile"
#define BUILD_PHASE_LINK     "link"
#define BUILD_PHASE_VERIFY   "verify"
struct BuildStatus {
  float progress;       
  const char* phase;     
  const char* message;   
  bool isBuilding;      
  unsigned long startTime;
};
extern WebSocketsClient webSocket;
extern bool accelConnected;
extern bool accelStopRequested;
extern char accelHost[16];
extern int accelPort;
extern int accelRetryCount;
extern unsigned long accelStartTime;
extern bool accelAnimating;
extern bool accelChatMode;
extern char currentModelName[32];
extern bool accelModelLoaded;
extern uint16_t gpuTemp;
extern uint8_t gpuUtil;
extern uint16_t gpuMem;
extern float gpuPwr;
extern uint16_t gpuClk;
extern BuildStatus buildStatus;
extern bool showBuildProgress;
extern const char CLR_RST[] PROGMEM;
extern const char CLR_RED[] PROGMEM;
extern const char CLR_GRN[] PROGMEM;
extern const char CLR_YLW[] PROGMEM;
extern const char CLR_BLU[] PROGMEM;
extern const char CLR_MAG[] PROGMEM;
extern const char CLR_CYN[] PROGMEM;
extern const char CLR_WHT[] PROGMEM;
void initUniAccel();
void loopUniAccel();
void handleAccelCommand(char* args);
void handleHfCommand(char* args);
void onGpuResponse(uint8_t * payload, size_t length);
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
void discoverAccelHost();
void accelExec(const char* kernel, JsonArray data);
void secure_crypt(uint8_t *data, size_t len);
void kprintColor_P(const char *pgmColor);
void kprintProgmem(const char *pgmStr);
void displayBuildProgress(float progress, const char* phase, const char* message);
void displayTelemetryBox();
void hashPass(const char* input, char* output, const uint8_t* salt);
bool secureEquals(const char* a, const char* b, size_t len);
void generateNewSalt(uint8_t* salt);
#endif
