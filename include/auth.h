#ifndef AUTH_H
#define AUTH_H

#include "common.h"
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#endif

extern bool serialAuthenticated;
extern bool telnetAuthenticated;
extern uint8_t loginFailCount;
extern bool isLockedOut;
extern char whitelistIP[16];

void hashPass(const char *input, char *output, const uint8_t *salt);
void generateNewSalt(uint8_t *salt);
bool secureEquals(const char *a, const char *b, size_t len);
bool checkWebAuth(String pass, IPAddress remoteIp);
bool isIpAllowed(IPAddress ip);

#endif
