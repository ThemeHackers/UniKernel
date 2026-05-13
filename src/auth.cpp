#include "../include/auth.h"
#include <EEPROM.h>
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include <mbedtls/sha256.h>
#endif



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

ICACHE_FLASH_ATTR void hashPass(const char *input, char *output, const uint8_t *salt) {
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  uint8_t currentHash[32];
  br_sha256_context ctx;
  

  br_sha256_init(&ctx);
  br_sha256_update(&ctx, salt, PASS_SALT_LEN);
  br_sha256_update(&ctx, input, strlen(input));
  br_sha256_out(&ctx, currentHash);
  

  for (int i = 1; i < 1000; i++) {
    br_sha256_init(&ctx);
    br_sha256_update(&ctx, currentHash, 32);
    br_sha256_out(&ctx, currentHash);
    if (i % 100 == 0) yield();
  }
  
  for (int i = 0; i < 16; i++) {
    output[i] = currentHash[i];
  }
  output[16] = '\0';
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
  uint8_t currentHash[32];
  mbedtls_sha256_context ctx;
  
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts_ret(&ctx, 0);
  mbedtls_sha256_update_ret(&ctx, salt, PASS_SALT_LEN);
  mbedtls_sha256_update_ret(&ctx, (const unsigned char*)input, strlen(input));
  mbedtls_sha256_finish_ret(&ctx, currentHash);
  
  for (int i = 1; i < 1000; i++) {
    mbedtls_sha256_starts_ret(&ctx, 0);
    mbedtls_sha256_update_ret(&ctx, currentHash, 32);
    mbedtls_sha256_finish_ret(&ctx, currentHash);
    if (i % 100 == 0) yield();
  }
  mbedtls_sha256_free(&ctx);
  
  for (int i = 0; i < 16; i++) {
    output[i] = currentHash[i];
  }
  output[16] = '\0';
#endif
}

ICACHE_FLASH_ATTR void generateNewSalt(uint8_t *salt) {
    for (int i = 0; i < PASS_SALT_LEN; i++) {
        salt[i] = (uint8_t)os_random() % 256;
    }
}

ICACHE_FLASH_ATTR bool secureEquals(const char *a, const char *b, size_t len) {
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
  uint8_t salt[PASS_SALT_LEN];
  EEPROM.get(EEPROM_PASS_ADDR, savedPass);
  EEPROM.get(EEPROM_SALT_ADDR, salt);
  hashPass(pass.c_str(), hashedInput, salt);

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
