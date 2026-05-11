#include "../include/auth.h"
#include <EEPROM.h>



bool serialAuthenticated = false;
bool telnetAuthenticated = false;
uint8_t loginFailCount = 0;
bool isLockedOut = false;
unsigned long lastLoginAttempt = 0;
unsigned long loginCooldown = 0;
char whitelistIP[16] = "";

bool isIpAllowed(IPAddress ip) {
    if (strlen(whitelistIP) == 0) return true;
    return ip.toString() == String(whitelistIP);
}

void hashPass(const char *input, char *output) {
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

#endif
}

bool secureEquals(const char *a, const char *b, size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) {
    diff |= ((uint8_t)a[i]) ^ ((uint8_t)b[i]);
  }
  return diff == 0;
}

bool checkWebAuth(String pass, IPAddress remoteIp) {
  if (!isIpAllowed(remoteIp)) return false;
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
    uint8_t shift = loginFailCount > 6 ? 6 : loginFailCount;
    loginCooldown = 1000UL << shift;
    if (loginCooldown > 60000) loginCooldown = 60000;
    EEPROM.put(EEPROM_FAIL_COUNT_ADDR, (uint8_t)loginFailCount);
    EEPROM.commit();
    return false;
  }
}
