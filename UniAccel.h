#ifndef UNIACCEL_H
#define UNIACCEL_H

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

#ifndef XOR_KEY
#define XOR_KEY 0x5A
#endif

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

extern const char* CLR_RST;
extern const char* CLR_RED;
extern const char* CLR_GRN;
extern const char* CLR_YLW;
extern const char* CLR_BLU;
extern const char* CLR_MAG;
extern const char* CLR_CYN;

void initUniAccel();
void loopUniAccel();
void handleAccelCommand(char* args);
void handleHfCommand(char* args);
void onGpuResponse(uint8_t * payload, size_t length);
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
void discoverAccelHost();
void accelExec(const char* kernel, JsonArray data);

#endif
