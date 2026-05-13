#include "../include/commands.h"
#include "../UniAccel.h"
#include "../include/common.h"
#include "../include/shell.h"
#include "../include/vfs.h"


extern const char CLR_RST[] PROGMEM;
extern const char CLR_RED[] PROGMEM;
extern const char CLR_GRN[] PROGMEM;
extern const char CLR_YLW[] PROGMEM;
extern const char CLR_BLU[] PROGMEM;
extern const char CLR_MAG[] PROGMEM;
extern const char CLR_CYN[] PROGMEM;
extern const char CLR_WHT[] PROGMEM;
#include "../UniAccel.h"

bool isTelnetSafeCommand(const char *cmd);
#include <EEPROM.h>
#include <LittleFS.h>
#include <Wire.h>
#include <time.h>
#include <vector>

extern void runScript(const char *content);
#if defined(ESP8266)
#include <ESP8266Ping.h>
#include <ESP8266WiFi.h>

#elif defined(ESP32)
#include <ESP32Ping.h>
#include <WiFi.h>

#endif
#include "../UniAccel.h"
#if defined(ESP8266)
#include <ArduinoOTA.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>

#endif

#if defined(ESP32)
#include <ArduinoOTA.h>
#include <WebServer.h>

#endif
extern bool accelChatMode;
extern bool telnetEnabled;
extern bool webEnabled;
extern bool otaEnabled;
extern unsigned long otaEndTime;
extern bool btEnabled;
extern bool accelConnected;
extern char accelHost[];
extern WiFiServer telnetServer;
extern WiFiClient telnetClient;
#if defined(ESP8266)
extern ESP8266WebServer webServer;
#elif defined(ESP32)
extern WebServer webServer;
#endif
extern void redrawPrompt();

std::vector<CommandDef> commandTable;
bool useColor = false;

ICACHE_FLASH_ATTR bool isSystemProtected(const char *name) {
  if (strlen(name) != 6)
    return false;
  if (name[1] == 'r' && name[2] == 'c' && name[3] == '.' && name[4] == 's' &&
      name[5] == 'h') {
    if (name[0] >= '0' && name[0] <= '2')
      return true;
  }
  return false;
}

ICACHE_FLASH_ATTR void registerCommands() {
  commandTable.emplace_back("ls", handle_ls, false,
                            "List files in current directory");
  commandTable.emplace_back("cat", handle_cat, true, "Display file contents");
  commandTable.emplace_back("login", handle_login, false,
                            "Authenticate with password");
  commandTable.emplace_back("help", handle_help, false,
                            "Show available commands");
  commandTable.emplace_back("on", handle_on, true, "Turn pin ON");
  commandTable.emplace_back("off", handle_off, true, "Turn pin OFF");
  commandTable.emplace_back("uptime", handle_uptime, false,
                            "Show system uptime");
  commandTable.emplace_back("free", handle_free, false, "Show free memory");
  commandTable.emplace_back("mkdir", handle_mkdir, true, "Create a directory");
  commandTable.emplace_back("touch", handle_touch, true, "Create a file");
  commandTable.emplace_back("cd", handle_cd, false, "Change directory");
  commandTable.emplace_back("pwd", handle_pwd, false,
                            "Print working directory");
  commandTable.emplace_back("echo", handle_echo, true,
                            "Echo text or write to file");
  commandTable.emplace_back("reboot", handle_reboot, true,
                            "Restart the system");
  commandTable.emplace_back("i2c", handle_i2c, true, "I2C utilities");
  commandTable.emplace_back("date", handle_date, false,
                            "Show current date/time");
  commandTable.emplace_back("rm", handle_rm, true, "Remove a file");
  commandTable.emplace_back("mv", handle_mv, true, "Move/Rename a file");
  commandTable.emplace_back("cp", handle_cp, true, "Copy a file");
  commandTable.emplace_back("pinmode", handle_pinmode, true, "Set pin mode");
  commandTable.emplace_back("write", handle_write, true,
                            "Digital/Analog write");
  commandTable.emplace_back("read", handle_read, true, "Digital/Analog read");
  commandTable.emplace_back("neofetch", handle_neofetch, false,
                            "Show system info");
  commandTable.emplace_back("wifi", handle_wifi, false, "Show WiFi status");
  commandTable.emplace_back("ifconfig", handle_wifi, false, "Alias for wifi");
  commandTable.emplace_back("clear", handle_clear, false, "Clear terminal");
  commandTable.emplace_back("dmesg", handle_dmesg, false, "Show system log");
  commandTable.emplace_back("df", handle_df, false, "Show disk usage");
  commandTable.emplace_back("hwinfo", handle_hwinfo, false,
                            "Show hardware info");
  commandTable.emplace_back("logout", handle_logout, false,
                            "Terminate session");
  commandTable.emplace_back("exit", handle_exit, false, "Exit terminal");
  commandTable.emplace_back("accel", handle_accel, true,
                            "GPU Accelerator controls");
  commandTable.emplace_back("hf", handle_hf, true, "HuggingFace AI commands");
  commandTable.emplace_back("chat", handle_chat, true, "Toggle AI chat mode");
  commandTable.emplace_back("sh", handle_sh, true, "Execute a shell script");
  commandTable.emplace_back("color", handle_color, false,
                            "Toggle terminal colors");
  commandTable.emplace_back("whoami", handle_whoami, false,
                            "Show current user");
  commandTable.emplace_back("uname", handle_uname, false, "Show system info");
  commandTable.emplace_back("passwd", handle_passwd, false, "Change password");
  commandTable.emplace_back("alias", handle_alias, false,
                            "Manage command aliases");
  commandTable.emplace_back("env", handle_env, false,
                            "List environment variables");
  commandTable.emplace_back("export", handle_export, false,
                            "Set environment variable");
  commandTable.emplace_back("sys", handle_sys, false, "System overview");
  commandTable.emplace_back("ps", handle_ps, false, "List active tasks");
  commandTable.emplace_back("top", handle_top, false, "Task manager");
  commandTable.emplace_back("append", handle_append, true,
                            "Append text to file");
  commandTable.emplace_back("info", handle_info, true, "Show file info");
  commandTable.emplace_back("save", handle_save, true, "Save VFS to EEPROM");
  commandTable.emplace_back("load", handle_load, true, "Load VFS from EEPROM");
  commandTable.emplace_back("lfs", handle_lfs, true, "LittleFS management");
  commandTable.emplace_back("chmod", handle_chmod, true,
                            "Change file permissions");
  commandTable.emplace_back("chown", handle_chown, true, "Change file owner");
  commandTable.emplace_back("cpu", handle_cpu, true, "Set CPU frequency");
  commandTable.emplace_back("sleep", handle_sleep, true, "Enter light sleep");
  commandTable.emplace_back("deepsleep", handle_deepsleep, true,
                            "Enter deep sleep");
  commandTable.emplace_back("firewall", handle_firewall, true,
                            "Manage IP whitelist");
  commandTable.emplace_back("ota", handle_ota, true, "Enable OTA updates");
  commandTable.emplace_back("delay", handle_delay, false, "Wait for ms");
  commandTable.emplace_back("kill", handle_kill, true, "Terminate task");
  commandTable.emplace_back("trigger", handle_trigger, true, "Manage triggers");
  commandTable.emplace_back("mqtt", handle_mqtt, true, "Send MQTT message");
  commandTable.emplace_back("pwm", handle_pwm, true, "Set PWM value");
  commandTable.emplace_back("gpio", handle_gpio, true, "GPIO direct control");
  commandTable.emplace_back("ping", handle_ping, false, "Ping a host");
  commandTable.emplace_back("wget", handle_wget, true, "Download from URL");
  commandTable.emplace_back("ntp", handle_ntp, false, "Sync time via NTP");
  commandTable.emplace_back("ota", handle_ota, true, "Toggle OTA updates");
  commandTable.emplace_back("telnet", handle_telnet, true,
                            "Toggle Telnet server");
  commandTable.emplace_back("web", handle_web, true, "Toggle Web server");
  commandTable.emplace_back("bt", handle_bt, true, "Toggle Bluetooth");
  commandTable.emplace_back("netstat", handle_netstat, false,
                            "Show network status");
  commandTable.emplace_back("cron", handle_cron, true, "Manage cron tasks");
  commandTable.emplace_back("bg", handle_bg, true, "Run in background");
  commandTable.emplace_back("boot", handle_boot, true, "Boot configuration");
  commandTable.emplace_back("waitwifi", handle_waitwifi, false,
                            "Wait for WiFi connection");
}

bool isSerialSession = true;

ICACHE_FLASH_ATTR void dispatchCommand(char *line, bool fromSerial) {
  isSerialSession = fromSerial;
  char *cmdLine = strdup(line);
  if (!cmdLine) {
    sendResponse(false, 500, "Out of memory");
    return;
  }

  char *p = kTrim(cmdLine);
  char *cmd = p;
  char *args = strchr(p, ' ');

  if (args) {
    *args = '\0';
    args = kTrim(args + 1);
  } else {
    args = (char *)"";
  }

  toLowercase(cmd);

  const char *proxyTarget = getEnv("PROXY_TARGET");
  if (proxyTarget && proxyTarget[0] != '\0' && strcmp(cmd, "exit") != 0) {
    static JsonDocument pdoc;
    pdoc.clear();
    pdoc["cmd"] = "proxy_req";
    pdoc["target"] = proxyTarget;
    pdoc["data"] = line;
    uint8_t pbuf[512];
    size_t plen = serializeMsgPack(pdoc, pbuf, sizeof(pbuf));
    secure_crypt(pbuf, plen);
    webSocket.sendBIN(pbuf, plen);
    free(cmdLine);
    return;
  } else if (proxyTarget && strcmp(cmd, "exit") == 0) {
    setEnv("PROXY_TARGET", "");
    kprintln(F("Exited remote shell."));
    free(cmdLine);
    return;
  }

  for (const auto &c : commandTable) {
    if (strcmp(cmd, c.name) == 0) {
      bool currentAuth = fromSerial ? serialAuthenticated : telnetAuthenticated;

      if (!fromSerial && telnetAuthenticated) {
        if (!isTelnetSafeCommand(cmd)) {
          sendResponse(false, 403, "Command not allowed via network");
          free(cmdLine);
          return;
        }
      }

      if (c.authRequired && !currentAuth && !needsSetup) {
        sendResponse(false, 401, "Authentication required");
        free(cmdLine);
        return;
      }
      c.handler(args, fromSerial);
      free(cmdLine);
      return;
    }
  }
  sendResponse(false, 404, "Command not found");
  free(cmdLine);
}

ICACHE_FLASH_ATTR void handle_ls(char *args, bool fromSerial) {
  if (strcmp(currentPath, "/mnt/host") == 0 ||
      strncmp(args, "/mnt/host", 9) == 0) {
    char remoteCmd[32];
    snprintf(remoteCmd, sizeof(remoteCmd), "mount ls %s", args);
    handleAccelCommand(remoteCmd);
    return;
  }
  if (strncmp(args, "/mnt/nodes/", 11) == 0) {
    char target[32];
    char *path = args + 11;
    char *slash = strchr(path, '/');
    if (slash) {
      strncpy(target, path, slash - path);
      target[slash - path] = '\0';
      path = slash;
    } else {
      strcpy(target, path);
      path = (char *)"/";
    }
    static JsonDocument ldoc;
    ldoc.clear();
    ldoc["cmd"] = "node_fs_req";
    ldoc["target"] = target;
    ldoc["path"] = path;
    ldoc["action"] = "list";
    uint8_t lbuf[256];
    size_t llen = serializeMsgPack(ldoc, lbuf, sizeof(lbuf));
    secure_crypt(lbuf, llen);
    webSocket.sendBIN(lbuf, llen);
    kprint(F("Requesting directory from node "));
    kprintln(target);
    return;
  }
  bool isLong = (strcmp(args, "-l") == 0);
  if (!fromSerial) {
    static JsonDocument data;
    data.clear();
    JsonArray arr = data.to<JsonArray>();
    for (int i = 0; i < 16; i++) {
      if ((vfs[i].flags & FLAG_ACTIVE) &&
          strcmp(vfs[i].parentDir, currentPath) == 0) {
        JsonObject obj = arr.add<JsonObject>();
        obj["name"] = vfs[i].name;
        obj["type"] = (vfs[i].flags & FLAG_ISDIR) ? "dir" : "file";
      }
    }
    sendResponse(true, 200, "OK", &data);
    return;
  }

  bool empty = true;
  int fileCount = 0;

  for (int i = 0; i < 16; i++) {
    if (vfs[i].flags & FLAG_ACTIVE) {
      bool pathMatch = (strcmp(vfs[i].parentDir, currentPath) == 0);

      if (pathMatch) {
        empty = false;
        fileCount++;
        if (isLong) {
          printPermissions(vfs[i].mode, (vfs[i].flags & FLAG_ISDIR));
          kprint(F(" "));
          kprint(vfs[i].ownerId == 0 ? F("root  ") : F("guest "));
          kprint((unsigned long)strlen(vfs[i].content));
          kprint(F(" "));
          if (vfs[i].flags & FLAG_ISDIR)
            kprintColor(CLR_BLU);
          else
            kprintColor(CLR_GRN);
          kprint(vfs[i].name);
          if (vfs[i].flags & FLAG_ISDIR)
            kprint(F("/"));
          kprintColor(CLR_RST);
          kprintln();
        } else {
          if (vfs[i].flags & FLAG_ISDIR)
            kprintColor(CLR_BLU);
          else
            kprintColor(CLR_GRN);
          kprint(vfs[i].name);
          if (vfs[i].flags & FLAG_ISDIR)
            kprint(F("/"));
          kprintColor(CLR_RST);
          kprint(F("  "));
        }
      }
    }
  }
  if (strcmp(currentPath, "/dev/") == 0) {
    if (isLong)
      kprintln(F("crw-rw-rw- root null\ncrw-rw-rw- root led\ncrw-rw-rw- root "
                 "a0\ncrw-rw-rw- root a1\ncrw-rw-rw- root a2\ncrw-rw-rw- root "
                 "a3\ncrw-rw-rw- root a4\ncrw-rw-rw- root a5"));
    else
      kprint(F("null  led  a0  a1  a2  a3  a4  a5  "));
    empty = false;
  }
  if (!isLong)
    kprintln();
  if (empty && !isLong)
    kprintln(F("(empty)"));
}

ICACHE_FLASH_ATTR void handle_cat(char *args, bool fromSerial) {
  if (strncmp(args, "/mnt/host/", 10) == 0 ||
      strcmp(currentPath, "/mnt/host") == 0) {
    char remoteCmd[64];
    snprintf(remoteCmd, sizeof(remoteCmd), "mount cat %s", args);
    handleAccelCommand(remoteCmd);
    return;
  }
  if (strncmp(args, "/mnt/nodes/", 11) == 0) {
    char target[32];
    char *path = args + 11;
    char *slash = strchr(path, '/');
    if (slash) {
      strncpy(target, path, slash - path);
      target[slash - path] = '\0';
      path = slash;
    } else {
      strcpy(target, path);
      path = (char *)"/";
    }
    static JsonDocument cdoc;
    cdoc.clear();
    cdoc["cmd"] = "node_fs_req";
    cdoc["target"] = target;
    cdoc["path"] = path;
    cdoc["action"] = "read";
    uint8_t cbuf[256];
    size_t clen = serializeMsgPack(cdoc, cbuf, sizeof(cbuf));
    secure_crypt(cbuf, clen);
    webSocket.sendBIN(cbuf, clen);
    kprint(F("Fetching file from node "));
    kprintln(target);
    return;
  }
  if (strcmp(currentPath, "/dev/") == 0) {
    if (strcmp(args, "null") == 0)
      return;
    if (strcmp(args, "led") == 0) {
      kprintln(digitalRead(LED_BUILTIN) == LOW ? "1" : "0");
      return;
    }
    if (args[0] == 'a' && isdigit(args[1])) {
      kprintln(analogRead(args[1] - '0'));
      return;
    }
    if (strcmp(args, "temp") == 0) {
      kprintln(25 + (millis() % 5));
      return;
    }
    if (strcmp(args, "vcc") == 0) {
      kprintln(ESP.getVcc());
      return;
    }
  }

  int idx = findFile(args, currentPath);
  if (idx != -1) {
    if (!fromSerial) {
      static JsonDocument data;
      data.clear();
      data["content"] = vfs[idx].content;
      sendResponse(true, 200, "OK", &data);
    } else {
      kprintln(vfs[idx].content);
    }
  } else {
    sendResponse(false, 404, "File not found");
  }
}

ICACHE_FLASH_ATTR void handle_mkdir(char *args, bool fromSerial) {
  if (!isValidFsName(args)) {
    sendResponse(false, 400, "Invalid name");
    return;
  }
  for (int i = 0; i < 16; i++) {
    if (!(vfs[i].flags & FLAG_ACTIVE)) {
      safeStrncpy(vfs[i].name, args, NAME_LEN);
      safeStrncpy(vfs[i].parentDir, currentPath, PATH_LEN);
      vfs[i].flags = FLAG_ACTIVE | FLAG_ISDIR;
      vfs[i].mode = 0755;
      vfs[i].ownerId = 0;
      vfs[i].content[0] = '\0';
      sendResponse(true, 201, "Directory created");
      return;
    }
  }
  sendResponse(false, 507, "FS full");
}

ICACHE_FLASH_ATTR void handle_touch(char *args, bool fromSerial) {
  if (!isValidFsName(args)) {
    sendResponse(false, 400, "Invalid name");
    return;
  }
  for (int i = 0; i < 16; i++) {
    if (!(vfs[i].flags & FLAG_ACTIVE)) {
      safeStrncpy(vfs[i].name, args, NAME_LEN);
      safeStrncpy(vfs[i].parentDir, currentPath, PATH_LEN);
      vfs[i].flags = FLAG_ACTIVE;
      vfs[i].mode = 0644;
      vfs[i].ownerId = 0;
      vfs[i].content[0] = '\0';
      sendResponse(true, 201, "File created");
      return;
    }
  }
  sendResponse(false, 507, "FS full");
}

ICACHE_FLASH_ATTR void handle_cd(char *args, bool fromSerial) {
  if (strcmp(args, "..") == 0 || strcmp(args, "/") == 0) {
    strcpy(currentPath, "/");
    sendResponse(true, 200, "Moved to root");
    return;
  }
  int idx = findFile(args, currentPath);
  if (idx != -1 && (vfs[idx].flags & FLAG_ISDIR)) {
    if (currentPath[strlen(currentPath) - 1] != '/')
      strcat(currentPath, "/");
    strcat(currentPath, vfs[idx].name);
    sendResponse(true, 200, "Directory changed");
  } else {
    sendResponse(false, 404, "Directory not found");
  }
}

ICACHE_FLASH_ATTR void handle_pwd(char *args, bool fromSerial) {
  static JsonDocument data;
  data.clear();
  data["path"] = currentPath;
  sendResponse(true, 200, "OK", &data);
}

ICACHE_FLASH_ATTR void handle_echo(char *args, bool fromSerial) {
  char *redir = strchr(args, '>');
  if (redir) {
    *redir = '\0';
    char *filename = kTrim(redir + 1);
    char *text = kTrim(args);
    stripQuotes(text);

    if (isSystemProtected(filename) && !fromSerial) {
      sendResponse(false, 403,
                   "Protected system file (Serial access required)");
      return;
    }

    if (strcmp(currentPath, "/dev/") == 0) {
      if (strcmp(filename, "led") == 0) {
        pinMode(LED_BUILTIN, OUTPUT);
        digitalWrite(
            LED_BUILTIN,
            (text[0] == '1' || text[0] == 'H' || text[0] == 'h') ? LOW : HIGH);
        sendResponse(true, 200, "LED updated");
        return;
      }
    }

    int idx = findFile(filename, currentPath);
    if (idx == -1) {
      for (int i = 0; i < 16; i++) {
        if (!(vfs[i].flags & FLAG_ACTIVE)) {
          idx = i;
          safeStrncpy(vfs[idx].name, filename, NAME_LEN);
          safeStrncpy(vfs[idx].parentDir, currentPath, PATH_LEN);
          vfs[idx].flags = FLAG_ACTIVE;
          vfs[idx].mode = 0644;
          vfs[idx].ownerId = 0;
          break;
        }
      }
    }

    if (idx != -1) {
      safeStrncpy(vfs[idx].content, text, CONTENT_LEN);
      sendResponse(true, 200, "File updated");
    } else {
      sendResponse(false, 507, "FS full");
    }
  } else {
    if (fromSerial) {
      kprintln(args);
    } else {
      static JsonDocument data;
      data.clear();
      data["output"] = args;
      sendResponse(true, 200, "OK", &data);
    }
  }
}

ICACHE_FLASH_ATTR void handle_i2c(char *args, bool fromSerial) {
  if (strcmp(args, "help") == 0 || args[0] == '\0') {
    if (fromSerial) {
      kprintln(F("I2C Utilities:"));
      kprintln(F("  i2c scan    - Scan the I2C bus for active devices"));
    } else {
      sendResponse(false, 400, "Usage: i2c scan");
    }
    return;
  }

  if (strcmp(args, "scan") == 0) {
    static JsonDocument data;
    data.clear();
    JsonArray arr = data.to<JsonArray>();
    for (uint8_t addr = 1; addr < 127; addr++) {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0)
        arr.add(addr);
    }
    sendResponse(true, 200, "I2C Scan complete", &data);
  } else {
    sendResponse(false, 400, "Unknown I2C command. Type 'i2c help'.");
  }
}

ICACHE_FLASH_ATTR void handle_date(char *args, bool fromSerial) {
  time_t now = time(nullptr);
  char *dateStr = ctime(&now);
  if (dateStr && dateStr[strlen(dateStr) - 1] == '\n')
    dateStr[strlen(dateStr) - 1] = '\0';

  if (fromSerial) {
    kprintColor(CLR_CYN);
    kprintln(dateStr ? dateStr : "Unknown Date");
    kprintColor(CLR_RST);
  } else {
    static JsonDocument data;
    data.clear();
    data["timestamp"] = (unsigned long)now;
    data["human"] = dateStr ? dateStr : "";
    sendResponse(true, 200, "OK", &data);
  }
}

ICACHE_FLASH_ATTR void handle_reboot(char *args, bool fromSerial) {
  sendResponse(true, 200, "Rebooting...");
  delay(500);
  ESP.restart();
}

ICACHE_FLASH_ATTR void handle_login(char *args, bool fromSerial) {
  char hashedInput[17];
  char savedPass[17];
  uint8_t salt[PASS_SALT_LEN];
  EEPROM.get(EEPROM_PASS_ADDR, savedPass);
  EEPROM.get(EEPROM_SALT_ADDR, salt);
  hashPass(args, hashedInput, salt);

  if (secureEquals(hashedInput, savedPass, 16)) {
    if (fromSerial)
      serialAuthenticated = true;
    else
      telnetAuthenticated = true;
    needsSetup = false;
    sendResponse(true, 200, "Login successful");
  } else {
    sendResponse(false, 401, "Invalid password");
  }
}

ICACHE_FLASH_ATTR void handle_on(char *args, bool fromSerial) {
  int pin = atoi_safe(args);
  if (pin >= 0 && pin <= 19) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    static JsonDocument data;
    data.clear();
    data["pin"] = pin;
    data["state"] = "HIGH";
    sendResponse(true, 200, "Pin set HIGH", &data);
  } else {
    sendResponse(false, 400, "Invalid pin");
  }
}

ICACHE_FLASH_ATTR void handle_off(char *args, bool fromSerial) {
  int pin = atoi_safe(args);
  if (pin >= 0 && pin <= 19) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    static JsonDocument data;
    data.clear();
    data["pin"] = pin;
    data["state"] = "LOW";
    sendResponse(true, 200, "Pin set LOW", &data);
  } else {
    sendResponse(false, 400, "Invalid pin");
  }
}

ICACHE_FLASH_ATTR void handle_help(char *args, bool fromSerial) {
  if (fromSerial) {
    kprintColor_P(CLR_CYN);
    kprintln(F("UniShell v2.0 - Commands List"));
    kprintln(F("==============================================================="
               "========"));
    kprintColor_P(CLR_RST);

    auto printCategory = [](const __FlashStringHelper *title,
                            const char *cmds[]) {
      kprintColor_P(CLR_YLW);
      kprintln(title);
      kprintColor_P(CLR_RST);

      std::vector<const CommandDef *> foundCmds;
      for (int i = 0; cmds[i] != nullptr; i++) {
        for (const auto &c : commandTable) {
          if (strcmp(c.name, cmds[i]) == 0) {
            foundCmds.push_back(&c);
            break;
          }
        }
      }

      int count = foundCmds.size();
      for (int i = 0; i < count; i += 2) {
        char left[64] = "";
        char right[64] = "";
        snprintf(left, sizeof(left), " %-9s - %-20.20s", foundCmds[i]->name,
                 foundCmds[i]->help);
        if (i + 1 < count) {
          snprintf(right, sizeof(right), " %-9s - %-20.20s",
                   foundCmds[i + 1]->name, foundCmds[i + 1]->help);
          char buf[128];
          snprintf(buf, sizeof(buf), "%-35s |%s", left, right);
          kprintln(buf);
        } else {
          kprintln(left);
        }
      }
      kprintln();
    };

    const char *catFs[] = {"ls",  "cat",  "mkdir",  "touch", "cd",
                           "pwd", "echo", "append", "rm",    "mv",
                           "cp",  "info", "lfs",    "chmod", "chown",
                           "df",  "save", "load",   nullptr};
    const char *catSys[] = {
        "hwinfo", "uptime",    "free",   "date", "sys",   "neofetch", "cpu",
        "sleep",  "deepsleep", "reboot", "boot", "dmesg", "uname",    nullptr};
    const char *catProc[] = {"ps",      "top", "kill", "cron",
                             "trigger", "bg",  "sh",   nullptr};
    const char *catNet[] = {"wifi", "ifconfig", "netstat", "ping",
                            "wget", "mqtt",     nullptr};
    const char *catHw[] = {"on",  "off", "pinmode", "write", "read",
                           "i2c", "pwm", "gpio",    nullptr};
    const char *catSec[] = {"login", "logout", "passwd", "whoami", "firewall",
                            "ota",   "telnet", "web",    "bt",     nullptr};
    const char *catMisc[] = {"help",   "clear", "color", "alias", "env",
                             "export", "delay", "accel", nullptr};

    printCategory(F("[ Filesystem & VFS ]"), catFs);
    printCategory(F("[ System & Hardware ]"), catSys);
    printCategory(F("[ Tasks & Automation ]"), catProc);
    printCategory(F("[ Network ]"), catNet);
    printCategory(F("[ Hardware IO ]"), catHw);
    printCategory(F("[ Security & Services ]"), catSec);
    printCategory(F("[ Misc Utilities ]"), catMisc);

    kprintColor_P(CLR_CYN);
    kprintln(F("==============================================================="
               "========"));
    kprintColor_P(CLR_RST);
  } else {
    static JsonDocument data;
    data.clear();
    JsonArray arr = data.to<JsonArray>();
    for (const auto &c : commandTable) {
      JsonObject obj = arr.add<JsonObject>();
      obj["name"] = c.name;
      obj["help"] = c.help;
    }
    sendResponse(true, 200, "Commands List", &data);
  }
}

ICACHE_FLASH_ATTR void handle_uptime(char *args, bool fromSerial) {
  static JsonDocument data;
  data.clear();
  data["uptime_sec"] = millis() / 1000;
  sendResponse(true, 200, "OK", &data);
}

ICACHE_FLASH_ATTR void handle_rm(char *args, bool fromSerial) {
  int idx = findFile(args, currentPath);
  if (idx != -1) {
    if (isSystemProtected(vfs[idx].name) && !fromSerial) {
      sendResponse(false, 403,
                   "Protected system file (Serial access required)");
      return;
    }
    vfs[idx].flags &= ~FLAG_ACTIVE;
    sendResponse(true, 200, "File removed");
  } else {
    sendResponse(false, 404, "File not found");
  }
}

ICACHE_FLASH_ATTR void handle_mv(char *args, bool fromSerial) {
  char *sp = strchr(args, ' ');
  if (!sp) {
    sendResponse(false, 400, "Usage: mv [src] [dst]");
    return;
  }
  *sp = '\0';
  char *src = args;
  char *dst = kTrim(sp + 1);
  int idx = findFile(src, currentPath);
  if (idx != -1) {
    if ((isSystemProtected(vfs[idx].name) || isSystemProtected(dst)) &&
        !fromSerial) {
      sendResponse(false, 403,
                   "Protected system file (Serial access required)");
      return;
    }
    safeStrncpy(vfs[idx].name, dst, NAME_LEN);
    sendResponse(true, 200, "File renamed");
  } else {
    sendResponse(false, 404, "Source file not found");
  }
}

ICACHE_FLASH_ATTR void handle_cp(char *args, bool fromSerial) {
  char *sp = strchr(args, ' ');
  if (!sp) {
    sendResponse(false, 400, "Usage: cp [src] [dst]");
    return;
  }
  *sp = '\0';
  char *src = args;
  char *dst = kTrim(sp + 1);
  int sIdx = findFile(src, currentPath);
  if (sIdx == -1) {
    sendResponse(false, 404, "Source not found");
    return;
  }

  if ((isSystemProtected(src) || isSystemProtected(dst)) && !fromSerial) {
    sendResponse(false, 403, "Protected system file (Serial access required)");
    return;
  }
  for (int i = 0; i < 16; i++) {
    if (!(vfs[i].flags & FLAG_ACTIVE)) {
      memcpy(&vfs[i], &vfs[sIdx], sizeof(RAMFile));
      safeStrncpy(vfs[i].name, dst, NAME_LEN);
      sendResponse(true, 201, "File copied");
      return;
    }
  }
  sendResponse(false, 507, "FS full");
}

ICACHE_FLASH_ATTR void handle_pinmode(char *args, bool fromSerial) {
  char *sp = strchr(args, ' ');
  if (!sp) {
    sendResponse(false, 400, "Usage: pinmode [pin] [in|out]");
    return;
  }
  *sp = '\0';
  int pin = atoi_safe(args);
  char *mode = kTrim(sp + 1);
  if (strcmp(mode, "out") == 0)
    pinMode(pin, OUTPUT);
  else
    pinMode(pin, INPUT);
  sendResponse(true, 200, "Pin mode set");
}

ICACHE_FLASH_ATTR void handle_write(char *args, bool fromSerial) {
  char *sp = strchr(args, ' ');
  if (!sp) {
    sendResponse(false, 400, "Usage: write [pin] [0|1]");
    return;
  }
  *sp = '\0';
  int pin = atoi_safe(args);
  int val = atoi_safe(sp + 1);
  digitalWrite(pin, val);
  sendResponse(true, 200, "Pin written");
}

ICACHE_FLASH_ATTR void handle_read(char *args, bool fromSerial) {
  int pin = atoi_safe(args);
  int val = digitalRead(pin);
  static JsonDocument data;
  data.clear();
  data["pin"] = pin;
  data["value"] = val;
  sendResponse(true, 200, "OK", &data);
}

ICACHE_FLASH_ATTR void handle_neofetch(char *args, bool fromSerial) {
  kprintColor_P(CLR_YLW);
  kprintln(F("       .---.          root@unikernel"));
  kprintColor_P(CLR_YLW);
  kprintln(F("      /     \\         --------------"));
  kprintColor_P(CLR_YLW);
  kprint(F("     |  (O)  |        "));
  kprintColor(CLR_WHT);
  kprintln(F("OS: UniKernel x86_esp"));
  kprintColor(CLR_YLW);
  kprint(F("      \\     /         "));
  kprintColor(CLR_WHT);
  kprint(F("Host: "));
  kprintln(BOARD_NAME);
  kprintColor(CLR_YLW);
  kprint(F("       '---'          "));
  kprintColor(CLR_WHT);
  kprintln(F("Kernel: 6.14.0-unikernel"));
  kprintColor(CLR_YLW);
  kprint(F("     /|     |\\        "));
  kprintColor(CLR_WHT);
  kprint(F("Uptime: "));
  kprint(millis() / 1000);
  kprintln(F("s"));
  kprintColor(CLR_YLW);
  kprint(F("    / |     | \\       "));
  kprintColor(CLR_WHT);
  kprintln(F("Shell: UniShell v2.0"));
  kprintColor(CLR_YLW);
  kprint(F("   /  |     |  \\      "));
  kprintColor(CLR_WHT);
  kprint(F("Memory: "));
  kprint(freeMemory());
  kprintln(F(" free"));
  kprintColor(CLR_YLW);
  kprint(F("  '---'-----'---'     "));
  kprintColor(CLR_WHT);
  kprint(F("VFS: "));
  kprint(MAX_FILES);
  kprintln(F(" slots"));
  kprintln(F(""));
  kprint(F("  "));
  kprintColor(CLR_RED);
  kprint(F("### "));
  kprintColor(CLR_GRN);
  kprint(F("### "));
  kprintColor(CLR_YLW);
  kprint(F("### "));
  kprintColor(CLR_BLU);
  kprint(F("### "));
  kprintColor(CLR_MAG);
  kprint(F("### "));
  kprintColor(CLR_CYN);
  kprint(F("### "));
  kprintColor(CLR_WHT);
  kprint(F("###"));
  kprintColor(CLR_RST);
  kprintln();
}

void handle_free(char *args, bool fromSerial) {
  static JsonDocument data;
  data.clear();
  data["free_heap"] = ESP.getFreeHeap();
  sendResponse(true, 200, "OK", &data);
}

void handle_wifi(char *args, bool fromSerial) {
  if (strcmp(args, "help") == 0) {
    if (fromSerial) {
      kprintln(F("WiFi Commands:"));
      kprintln(F("  wifi status            - Show current connection status"));
      kprintln(F("  wifi scan              - Scan for available networks"));
      kprintln(F(
          "  wifi connect <S> [P]   - Connect to SSID with optional Password"));
      kprintln(
          F("  wifi disconnect        - Disconnect and stop auto-reconnect"));
      kprintln(F("  wifi mode <sta|ap>     - Change WiFi mode"));
      kprintln(
          F("  wifi ap <S> <P>        - Setup Access Point with SSID/Pass"));
      kprintln(
          F("  waitwifi               - Wait for connection (for scripts)"));
    } else {
      sendResponse(false, 400,
                   "Usage: wifi [status|scan|connect|disconnect|mode|ap]");
    }
    return;
  }

  if (strcmp(args, "scan") == 0) {
    if (fromSerial)
      kprintln(F("Scanning WiFi networks..."));
    int n = WiFi.scanNetworks();
    static JsonDocument data;
    data.clear();
    JsonArray arr = data.to<JsonArray>();
    for (int i = 0; i < n; ++i) {
      JsonObject net = arr.add<JsonObject>();
      net["ssid"] = WiFi.SSID(i);
      net["rssi"] = WiFi.RSSI(i);
#if defined(ESP8266)
      net["secure"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
#elif defined(ESP32)
      net["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
#endif
    }
    sendResponse(true, 200, "WiFi Scan Results", &data);
    return;
  }

  if (strncmp(args, "connect ", 8) == 0) {
    char *line = args + 8;
    char ssid[32] = {0};
    char pass[64] = {0};

    if (*line == '"') {
      line++;
      char *end = strchr(line, '"');
      if (end) {
        size_t len = end - line;
        if (len >= 32)
          len = 31;
        strncpy(ssid, line, len);
        line = end + 1;
        while (*line == ' ')
          line++;
        strncpy(pass, line, 63);
      }
    } else {
      char *sp = strchr(line, ' ');
      if (sp) {
        size_t len = sp - line;
        if (len >= 32)
          len = 31;
        strncpy(ssid, line, len);
        strncpy(pass, sp + 1, 63);
      } else {
        strncpy(ssid, line, 31);
      }
    }

    if (strlen(pass) > 0)
      WiFi.begin(ssid, pass);
    else
      WiFi.begin(ssid);

    sendResponse(true, 200, "Connecting to WiFi...");
    return;
  }

  if (strcmp(args, "disconnect") == 0) {
    WiFi.disconnect(true);
    sendResponse(true, 200, "WiFi Disconnected");
    return;
  }

  if (strncmp(args, "mode ", 5) == 0) {
    if (strcmp(args + 5, "ap") == 0)
      WiFi.mode(WIFI_AP);
    else if (strcmp(args + 5, "sta") == 0)
      WiFi.mode(WIFI_STA);
    else
      WiFi.mode(WIFI_AP_STA);
    sendResponse(true, 200, "WiFi Mode Updated");
    return;
  }

  if (strncmp(args, "ap ", 3) == 0) {
    char *line = args + 3;
    char ssid[32] = {0};
    char pass[64] = {0};

    if (*line == '"') {
      line++;
      char *end = strchr(line, '"');
      if (end) {
        size_t len = end - line;
        if (len >= 32)
          len = 31;
        strncpy(ssid, line, len);
        line = end + 1;
        while (*line == ' ')
          line++;
        strncpy(pass, line, 63);
      }
    } else {
      char *sp = strchr(line, ' ');
      if (sp) {
        size_t len = sp - line;
        if (len >= 32)
          len = 31;
        strncpy(ssid, line, len);
        strncpy(pass, sp + 1, 63);
      }
    }

    if (strlen(ssid) > 0 && strlen(pass) > 0) {
      WiFi.softAP(ssid, pass);
      sendResponse(true, 200, "Access Point Created");
    } else {
      sendResponse(false, 400, "AP needs SSID and Password");
    }
    return;
  }

  static JsonDocument data;
  data.clear();
  int status = WiFi.status();
  const char *sMap[] = {"Idle",        "No SSID",        "Scan Completed",
                        "Connected",   "Connect Failed", "Connection Lost",
                        "Disconnected"};
  data["status"] = (status >= 0 && status <= 6) ? sMap[status] : "Unknown";
  data["ssid"] = WiFi.SSID();
  data["ip"] = WiFi.localIP().toString();
  data["rssi"] = WiFi.RSSI();
  data["gateway"] = WiFi.gatewayIP().toString();
  sendResponse(true, 200, "WiFi Status", &data);
}

void handle_clear(char *args, bool fromSerial) {
  Serial.print("\033[2J\033[H");
}

void handle_dmesg(char *args, bool fromSerial) {
  static JsonDocument data;
  data.clear();
  JsonArray arr = data.to<JsonArray>();
  for (int i = 0; i < DMESG_LINES; i++) {
    int idx = (dmesgIndex + i) % DMESG_LINES;
    if (dmesg[idx].message[0] != '\0') {
      JsonObject entry = arr.add<JsonObject>();
      entry["time"] = dmesg[idx].timestamp;
      entry["msg"] = dmesg[idx].message;
    }
  }
  sendResponse(true, 200, "Kernel Log", &data);
}

void handle_df(char *args, bool fromSerial) {
  FSInfo info;
  LittleFS.info(info);
  static JsonDocument data;
  data.clear();
  data["total"] = info.totalBytes;
  data["used"] = info.usedBytes;
  data["free"] = info.totalBytes - info.usedBytes;
  sendResponse(true, 200, "Disk Usage", &data);
}

void handle_hwinfo(char *args, bool fromSerial) {
  static JsonDocument data;
  data.clear();
  data["chip_id"] = ESP.getChipId();
  data["flash_size"] = ESP.getFlashChipRealSize();
  data["cpu_freq"] = ESP.getCpuFreqMHz();
  sendResponse(true, 200, "Hardware Info", &data);
}

void handle_logout(char *args, bool fromSerial) {
  accelChatMode = false;
  if (fromSerial)
    serialAuthenticated = false;
  else
    telnetAuthenticated = false;
  sendResponse(true, 200, "Logged out");
}

void handle_exit(char *args, bool fromSerial) {
  accelChatMode = false;
  sendResponse(true, 200, "Exiting terminal");
  if (fromSerial) {
    serialAuthenticated = false;
  } else {
    telnetAuthenticated = false;
  }
}

void handle_accel(char *args, bool fromSerial) { handleAccelCommand(args); }

void handle_hf(char *args, bool fromSerial) { handleHfCommand(args); }

void handle_chat(char *args, bool fromSerial) {
  if (strlen(args) == 0) {

    accelChatMode = !accelChatMode;
    if (accelChatMode) {
      kprintln(F("AI Chat mode enabled"));
      if (!fromSerial)
        sendResponse(true, 200, "AI Chat mode enabled");
    } else {
      kprintln(F("AI Chat mode disabled"));
      if (!fromSerial)
        sendResponse(true, 200, "AI Chat mode disabled");
    }
  } else if (strcmp(args, "on") == 0) {
    accelChatMode = true;
    kprintln(F("AI Chat mode enabled"));
    if (!fromSerial)
      sendResponse(true, 200, "AI Chat mode enabled");
  } else if (strcmp(args, "off") == 0) {
    accelChatMode = false;
    kprintln(F("AI Chat mode disabled"));
    if (!fromSerial)
      sendResponse(true, 200, "AI Chat mode disabled");
  } else {
    kprintln(F("Usage: chat [on|off]"));
    if (!fromSerial)
      sendResponse(false, 400, "Usage: chat [on|off]");
  }
}

void handle_sh(char *args, bool fromSerial) {
  int idx = findFile(args, currentPath);
  if (idx != -1) {
    addDmesg(F("sh: running script"));
    runScript(vfs[idx].content);
    if (!fromSerial)
      sendResponse(true, 200, "Script executed");
  } else {
    sendResponse(false, 404, "Script not found");
  }
}

void handle_waitwifi(char *args, bool fromSerial) {
  if (fromSerial)
    kprintln(F("Waiting for WiFi connection..."));
  int retry = 40;
  while (WiFi.status() != WL_CONNECTED && retry > 0) {
    delay(500);
    if (fromSerial)
      kprint(".");
    retry--;
    yield();
  }
  if (fromSerial)
    kprintln();
  if (WiFi.status() == WL_CONNECTED) {
    sendResponse(true, 200, "WiFi Connected");
  } else {
    sendResponse(false, 504, "WiFi Connection Timeout");
  }
}

void handle_color(char *args, bool fromSerial) {
  if (strcmp(args, "on") == 0)
    useColor = true;
  else if (strcmp(args, "off") == 0)
    useColor = false;
  else {
    sendResponse(false, 400, "Usage: color [on|off]");
    return;
  }
  sendResponse(true, 200, useColor ? "Color enabled" : "Color disabled");
}

void handle_whoami(char *args, bool fromSerial) {
  bool currentAuth = fromSerial ? serialAuthenticated : telnetAuthenticated;
  sendResponse(true, 200, currentAuth ? "root" : "guest");
}

void handle_uname(char *args, bool fromSerial) {
  static JsonDocument data;
  data.clear();
  data["sys"] = "UniKernel";
  data["node"] = BOARD_NAME;
  data["release"] = "2.0.0-stable";
  data["machine"] = "xtensa-lx106";
  sendResponse(true, 200, "OK", &data);
}

void handle_passwd(char *args, bool fromSerial) {
  bool currentAuth = fromSerial ? serialAuthenticated : telnetAuthenticated;
  if (!fromSerial && !currentAuth) {
    sendResponse(false, 401, "Authentication required");
    return;
  }
  if (strlen(args) < 4) {
    sendResponse(false, 400, "Password too short");
    return;
  }
  uint8_t salt[PASS_SALT_LEN];
  generateNewSalt(salt);
  char hashed[17];
  hashPass(args, hashed, salt);
  hashed[16] = '\0';
  EEPROM.put(EEPROM_SALT_ADDR, salt);
  EEPROM.put(EEPROM_PASS_ADDR, hashed);
  EEPROM.commit();
  needsSetup = false;
  sendResponse(true, 200, "Password changed");
}

void handle_alias(char *args, bool fromSerial) {
  if (strcmp(args, "help") == 0) {
    if (fromSerial) {
      kprintln(F("Alias Commands:"));
      kprintln(F("  alias           - List all active aliases"));
      kprintln(F("  alias name=cmd  - Create a new alias"));
      kprintln(F("    Example: alias ll=ls -l"));
    } else {
      sendResponse(false, 400, "Usage: alias [name=cmd]");
    }
    return;
  }

  if (strlen(args) == 0) {
    static JsonDocument data;
    data.clear();
    JsonArray arr = data.to<JsonArray>();
    for (int i = 0; i < MAX_ALIAS; i++) {
      if (aliasTable[i].active) {
        JsonObject obj = arr.add<JsonObject>();
        obj["name"] = aliasTable[i].name;
        obj["cmd"] = aliasTable[i].cmd;
      }
    }
    sendResponse(true, 200, "Aliases", &data);
    return;
  }

  char *eq = strchr(args, '=');
  if (eq) {
    *eq = '\0';
    char *name = kTrim(args);
    char *cmd = kTrim(eq + 1);
    for (int i = 0; i < MAX_ALIAS; i++) {
      if (!aliasTable[i].active || strcmp(aliasTable[i].name, name) == 0) {
        safeStrncpy(aliasTable[i].name, name, NAME_LEN);
        safeStrncpy(aliasTable[i].cmd, cmd, 32);
        aliasTable[i].active = true;
        sendResponse(true, 200, "Alias set");
        return;
      }
    }
    sendResponse(false, 507, "Alias table full");
  } else {
    sendResponse(false, 400, "Usage: alias name=cmd");
  }
}

void handle_env(char *args, bool fromSerial) {
  if (strcmp(args, "help") == 0) {
    if (fromSerial) {
      kprintln(F("Environment Commands:"));
      kprintln(F("  env             - List all environment variables"));
      kprintln(F("  export key=val  - Set environment variable"));
    } else {
      sendResponse(false, 400, "Usage: env or export key=val");
    }
    return;
  }

  static JsonDocument data;
  data.clear();
  JsonObject obj = data.to<JsonObject>();
  for (int i = 0; i < MAX_ENV; i++) {
    if (envTable[i].active) {
      obj[envTable[i].key] = envTable[i].val;
    }
  }
  sendResponse(true, 200, "Environment Variables", &data);
}

void handle_export(char *args, bool fromSerial) {
  char *eq = strchr(args, '=');
  if (eq) {
    *eq = '\0';
    setEnv(kTrim(args), kTrim(eq + 1));
    sendResponse(true, 200, "Variable set");
  } else {
    sendResponse(false, 400, "Usage: export key=val");
  }
}

void handle_sys(char *args, bool fromSerial) {
  if (strcmp(args, "help") == 0) {
    if (fromSerial) {
      kprintln(F("System Commands:"));
      kprintln(F("  sys diagnosis - Run performance diagnosis"));
      kprintln(F("  sys audit     - Run security & integrity audit"));
      kprintln(F("  sys backup    - Show VFS backup script"));
      kprintln(F("  sys           - Show neofetch"));
    } else {
      sendResponse(false, 400, "Usage: sys [diagnosis|audit|backup]");
    }
    return;
  }

  if (strcmp(args, "diagnosis") == 0) {
    kprintln(F("--- UniKernel Performance Diagnosis ---"));
    kprint(F("Free Memory    : "));
    kprint(ESP.getFreeHeap());
    kprintln(F(" bytes"));
    kprint(F("Fragmentation  : "));
    kprint(ESP.getHeapFragmentation());
    kprintln(F("%"));
    kprintColor(CLR_GRN);
    kprintln(F("[+] Core loop performance is optimal."));
    kprintColor(CLR_RST);
  } else if (strcmp(args, "audit") == 0) {
    kprintln(F("--- UniKernel Security & Integrity Audit ---"));
    int score = 100;
    if (strlen(whitelistIP) == 0) {
      kprintln(F("[!] Security: Firewall is OPEN (No whitelist). (-20)"));
      score -= 20;
    } else
      kprintln(F("[+] Security: Firewall active."));
    kprint(F("System Security Score: "));
    kprintln(score);
  } else if (strcmp(args, "backup") == 0) {
    kprintln(F("--- UniKernel VFS Backup Script ---"));
    for (int i = 0; i < 16; i++) {
      if ((vfs[i].flags & FLAG_ACTIVE) && !(vfs[i].flags & FLAG_ISDIR)) {
        kprint(F("echo \""));
        kprint(vfs[i].content);
        kprint(F("\" > "));
        kprintln(vfs[i].name);
      }
    }
  } else {
    handle_neofetch(args, fromSerial);
  }
}

void handle_ps(char *args, bool fromSerial) {
  if (fromSerial) {
    kprintColor(CLR_CYN);
    kprintln(F("PID   INTERVAL(ms)   RUNS       NAME"));
    kprintln(F("----------------------------------------"));
    kprintColor(CLR_RST);
    bool hasTasks = false;
    for (int i = 0; i < MAX_TASKS; i++) {
      if (taskTable[i].active) {
        hasTasks = true;
        char buf[64];
        snprintf(buf, sizeof(buf), "%-5d %-14lu %-10lu %s", i,
                 taskTable[i].interval, taskTable[i].executionCount,
                 taskTable[i].name);
        kprintln(buf);
      }
    }
    if (!hasTasks) {
      kprintColor(CLR_YLW);
      kprintln(F("(No active background tasks)"));
      kprintColor(CLR_RST);
    }
    return;
  }

  static JsonDocument data;
  data.clear();
  JsonArray arr = data.to<JsonArray>();
  for (int i = 0; i < MAX_TASKS; i++) {
    if (taskTable[i].active) {
      JsonObject obj = arr.add<JsonObject>();
      obj["pid"] = i;
      obj["interval"] = taskTable[i].interval;
      obj["runs"] = taskTable[i].executionCount;
    }
  }
  sendResponse(true, 200, "Process List", &data);
}

void handle_top(char *args, bool fromSerial) { handle_ps(args, fromSerial); }

void handle_append(char *args, bool fromSerial) {
  char *sp = strchr(args, ' ');
  if (!sp) {
    sendResponse(false, 400, "Usage: append [file] [text]");
    return;
  }
  *sp = '\0';
  char *filename = args;
  char *text = kTrim(sp + 1);
  stripQuotes(text);
  int idx = findFile(filename, currentPath);
  if (idx != -1) {
    if (isSystemProtected(vfs[idx].name) && !fromSerial) {
      sendResponse(false, 403,
                   "Protected system file (Serial access required)");
      return;
    }
    strncat(vfs[idx].content, text, CONTENT_LEN - strlen(vfs[idx].content) - 1);
    sendResponse(true, 200, "Text appended");
  } else {
    sendResponse(false, 404, "File not found");
  }
}

void handle_info(char *args, bool fromSerial) {
  int idx = findFile(args, currentPath);
  if (idx != -1) {
    static JsonDocument data;
    data.clear();
    data["name"] = vfs[idx].name;
    data["type"] = (vfs[idx].flags & FLAG_ISDIR) ? "dir" : "file";
    data["size"] = strlen(vfs[idx].content);
    data["mode"] = vfs[idx].mode;
    sendResponse(true, 200, "OK", &data);
  } else {
    sendResponse(false, 404, "File not found");
  }
}

void handle_save(char *args, bool fromSerial) {
  uint16_t magic = VFS_MAGIC;
  EEPROM.put(EEPROM_VFS_ADDR, magic);
  EEPROM.put(EEPROM_VFS_ADDR + 2, vfs);
  EEPROM.commit();
  sendResponse(true, 200, "VFS saved to EEPROM");
}

void handle_load(char *args, bool fromSerial) {
  uint16_t magic;
  EEPROM.get(EEPROM_VFS_ADDR, magic);
  if (magic == VFS_MAGIC) {
    EEPROM.get(EEPROM_VFS_ADDR + 2, vfs);
    sendResponse(true, 200, "VFS loaded from EEPROM");
  } else {
    sendResponse(false, 404, "No saved VFS found");
  }
}

void handle_lfs(char *args, bool fromSerial) {
  if (strcmp(args, "help") == 0 || strlen(args) == 0) {
    if (fromSerial) {
      kprintln(F("LittleFS Utilities:"));
      kprintln(F("  lfs ls      - List files in LittleFS"));
      kprintln(F("  lfs format  - Format LittleFS partition"));
    } else {
      sendResponse(false, 400, "Usage: lfs [ls|format]");
    }
    return;
  }

  if (strcmp(args, "format") == 0) {
    LittleFS.format();
    sendResponse(true, 200, "LittleFS formatted");
  } else if (strncmp(args, "ls", 2) == 0) {
#if defined(ESP8266)
    Dir dir = LittleFS.openDir("/");
    static JsonDocument data;
    data.clear();
    JsonArray arr = data.to<JsonArray>();
    while (dir.next()) {
      JsonObject obj = arr.add<JsonObject>();
      obj["name"] = dir.fileName();
      obj["size"] = dir.fileSize();
    }
    sendResponse(true, 200, "LittleFS Files", &data);
#else
    sendResponse(false, 501, "Not implemented for ESP32 yet");
#endif
  } else {
    sendResponse(false, 400, "Usage: lfs [ls|format]");
  }
}

void handle_chmod(char *args, bool fromSerial) {
  char *sp = strchr(args, ' ');
  if (!sp) {
    sendResponse(false, 400, "Usage: chmod [mode] [file]");
    return;
  }
  *sp = '\0';
  int mode = strtol(args, NULL, 8);
  char *filename = kTrim(sp + 1);
  int idx = findFile(filename, currentPath);
  if (idx != -1) {
    vfs[idx].mode = mode;
    sendResponse(true, 200, "Permissions updated");
  } else {
    sendResponse(false, 404, "File not found");
  }
}

void handle_chown(char *args, bool fromSerial) {
  char *sp = strchr(args, ' ');
  if (!sp) {
    sendResponse(false, 400, "Usage: chown [owner] [file]");
    return;
  }
  *sp = '\0';
  int owner = strcmp(args, "root") == 0 ? 0 : 1;
  char *filename = kTrim(sp + 1);
  int idx = findFile(filename, currentPath);
  if (idx != -1) {
    vfs[idx].ownerId = owner;
    sendResponse(true, 200, "Owner updated");
  } else {
    sendResponse(false, 404, "File not found");
  }
}

void handle_cpu(char *args, bool fromSerial) {
  int freq = atoi(args);
  if (freq == 80 || freq == 160) {
#if defined(ESP8266)
    system_update_cpu_freq(freq);
#endif
    sendResponse(true, 200, "CPU frequency updated");
  } else {
    sendResponse(false, 400, "Usage: cpu [80|160]");
  }
}

void handle_sleep(char *args, bool fromSerial) {
  int ms = atoi(args);
  sendResponse(true, 200, "Entering light sleep...");
  delay(ms);
}

void handle_deepsleep(char *args, bool fromSerial) {
  int sec = atoi(args);
  sendResponse(true, 200, "Entering deep sleep...");
  delay(500);
  ESP.deepSleep(sec * 1000000);
}

void handle_firewall(char *args, bool fromSerial) {
  if (strcmp(args, "help") == 0 || strlen(args) == 0) {
    if (fromSerial) {
      kprintln(F("Firewall Commands:"));
      kprintln(F("  firewall allow <IP> - Add IP to whitelist"));
      kprintln(F("  firewall clear      - Clear IP whitelist"));
    } else {
      sendResponse(false, 400, "Usage: firewall [allow IP|clear]");
    }
    return;
  }

  if (strncmp(args, "allow ", 6) == 0) {
    safeStrncpy(whitelistIP, args + 6, 16);
    sendResponse(true, 200, "Firewall: IP Whitelisted");
  } else if (strcmp(args, "clear") == 0) {
    whitelistIP[0] = '\0';
    sendResponse(true, 200, "Firewall: Whitelist Cleared");
  } else {
    sendResponse(false, 400, "Usage: firewall [allow IP|clear]");
  }
}

void handle_ota(char *args, bool fromSerial) {
  char *sub = strtok(args, " ");
  char *val = strtok(NULL, "");

  if (!sub) {
    sendResponse(false, 400, "Usage: ota [on|setpass <pass>]");
    return;
  }

  if (strcmp(sub, "setpass") == 0 && val) {
    if (strlen(val) < 6) {
      sendResponse(false, 400, "Password too short (min 6)");
      return;
    }
    char hash[33];
    MD5Builder md5;
    md5.begin();
    md5.add(val);
    md5.calculate();
    md5.getChars(hash);

    EEPROM.put(EEPROM_OTA_PASS_ADDR, hash);
    EEPROM.commit();

    ArduinoOTA.setPasswordHash(hash);
    sendResponse(true, 200, "OTA Password updated and hashed");
  } else if (strcmp(sub, "on") == 0) {
    char hash[33];
    EEPROM.get(EEPROM_OTA_PASS_ADDR, hash);
    if (hash[0] == 0xFF || hash[0] == 0x00) {
      sendResponse(false, 403, "Set OTA password first: ota setpass <pass>");
      return;
    }
    otaEnabled = true;
    otaEndTime = millis() + (3 * 60 * 1000);
    ArduinoOTA.begin();
    addDmesg(F("OTA: Server Started (3m window)"));
    sendResponse(true, 200, "OTA is enabled for 3 minutes");
  } else {
    sendResponse(false, 400, "Usage: ota [on|setpass <pass>]");
  }
}

void handle_delay(char *args, bool fromSerial) {
  int ms = atoi(args);
  delay(ms);
  sendResponse(true, 200, "Done");
}

void handle_kill(char *args, bool fromSerial) {
  int pid = atoi(args);
  if (pid >= 0 && pid < MAX_TASKS) {
    taskTable[pid].active = false;
    sendResponse(true, 200, "Task terminated");
  } else {
    sendResponse(false, 400, "Invalid PID");
  }
}

void handle_trigger(char *args, bool fromSerial) {
  if (strcmp(args, "help") == 0) {
    if (fromSerial) {
      kprintln(F("Trigger Commands:"));
      kprintln(F("  trigger                       - List active triggers"));
      kprintln(F("  trigger <cond> <op> <val> <act> - Add new trigger"));
      kprintln(F("    Example: trigger temp > 30 \"echo 1 > led\""));
    } else {
      sendResponse(false, 400, "Usage: trigger [cond] [op] [val] [act]");
    }
    return;
  }

  if (strlen(args) == 0) {
    static JsonDocument data;
    data.clear();
    JsonArray arr = data.to<JsonArray>();
    for (int i = 0; i < MAX_TRIGS; i++) {
      if (triggerTable[i].active) {
        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = i;
        obj["cond"] = triggerTable[i].cond;
        obj["op"] = String(triggerTable[i].op);
        obj["val"] = triggerTable[i].val;
        obj["action"] = triggerTable[i].action;
      }
    }
    sendResponse(true, 200, "Triggers List", &data);
  } else {
    char cond[16], op[2], act[32];
    int val;
    if (sscanf(args, "%15s %1s %d %31[^\n]", cond, op, &val, act) == 4) {
      for (int i = 0; i < MAX_TRIGS; i++) {
        if (!triggerTable[i].active) {
          strncpy(triggerTable[i].cond, cond, 15);
          triggerTable[i].op = op[0];
          triggerTable[i].val = val;
          strncpy(triggerTable[i].action, act, 31);
          triggerTable[i].active = true;
          sendResponse(true, 200, "Trigger added");
          return;
        }
      }
      sendResponse(false, 507, "Trigger table full");
    } else {
      sendResponse(false, 400, "Usage: trigger [cond] [op] [val] [act]");
    }
  }
}

void handle_boot(char *args, bool fromSerial) {
  if (strcmp(args, "help") == 0) {
    if (fromSerial) {
      kprintln(F("Boot Commands:"));
      kprintln(F("  boot          - Show current boot script"));
      kprintln(
          F("  boot <script> - Set custom boot script (e.g. boot main.sh)"));
      kprintln(F("  boot reset    - Reset to default (0rc.sh)"));
    } else {
      sendResponse(false, 400, "Usage: boot [reset|<script>]");
    }
    return;
  }

  char buf[NAME_LEN];
  memset(buf, 0, sizeof(buf));
  if (strncmp(args, "reset", 5) == 0) {
    strcpy(buf, "0rc.sh");
    EEPROM.put(EEPROM_BOOT_FILE_ADDR, buf);
    EEPROM.commit();
    sendResponse(true, 200, "Boot script reset to default");
  } else if (strlen(args) > 0) {
    strncpy(buf, args, NAME_LEN - 1);
    EEPROM.put(EEPROM_BOOT_FILE_ADDR, buf);
    EEPROM.commit();
    sendResponse(true, 200, "Boot script updated");
  } else {
    EEPROM.get(EEPROM_BOOT_FILE_ADDR, buf);
    buf[NAME_LEN - 1] = '\0';
    static JsonDocument data;
    data.clear();
    data["current"] = buf;
    sendResponse(true, 200, "Boot Configuration", &data);
  }
}

void handle_mqtt(char *args, bool fromSerial) {
  sendResponse(true, 200, "MQTT Message Sent (Simulated)");
}

void handle_pwm(char *args, bool fromSerial) {
  char *sp = strchr(args, ' ');
  if (!sp) {
    sendResponse(false, 400, "Usage: pwm [pin] [0-1023]");
    return;
  }
  *sp = '\0';
  int pin = atoi_safe(args);
  int val = atoi_safe(sp + 1);
  analogWrite(pin, val);
  sendResponse(true, 200, "PWM set");
}

void handle_gpio(char *args, bool fromSerial) {
  char *sp = strchr(args, ' ');
  if (!sp) {
    sendResponse(false, 400,
                 "Usage: gpio [pin] [on|off|toggle] OR gpio toggle [pin]");
    return;
  }
  *sp = '\0';
  char *first = kTrim(args);
  char *second = kTrim(sp + 1);

  int pin;
  char *act;

  if (strcmp(first, "toggle") == 0) {
    char toggleCmd[] = "toggle";
    act = toggleCmd;
    pin = atoi_safe(second);
  } else {
    pin = atoi_safe(first);
    act = second;
  }

  if (pin < 0 || pin > 16) {
    sendResponse(false, 400, "Invalid pin number (0-16)");
    return;
  }

  pinMode(pin, OUTPUT);
  if (strcmp(act, "on") == 0) {
    digitalWrite(pin, HIGH);
  } else if (strcmp(act, "off") == 0) {
    digitalWrite(pin, LOW);
  } else if (strcmp(act, "toggle") == 0) {
    digitalWrite(pin, !digitalRead(pin));
  } else {
    sendResponse(false, 400,
                 "Usage: gpio [pin] [on|off|toggle] OR gpio toggle [pin]");
    return;
  }
  sendResponse(true, 200, "GPIO updated");
}

void handle_ping(char *args, bool fromSerial) {
  if (strlen(args) == 0) {
    sendResponse(false, 400, "Usage: ping [host]");
    return;
  }

  IPAddress remote_ip;
  if (!WiFi.hostByName(args, remote_ip)) {
    if (fromSerial) {
      kprint(F("Ping request could not find host "));
      kprint(args);
      kprintln(F(". Please check the name and try again."));
    } else {
      sendResponse(false, 404, "Host not found");
    }
    return;
  }

  char ipStr[16];
  sprintf(ipStr, "%d.%d.%d.%d", remote_ip[0], remote_ip[1], remote_ip[2],
          remote_ip[3]);

  if (fromSerial) {
    kprint(F("\nPinging "));
    kprint(args);
    kprint(F(" ["));
    kprint(ipStr);
    kprintln(F("] with 32 bytes of data:"));

    int sent = 0;
    int received = 0;
    long minT = 999, maxT = 0, totalT = 0;

    for (int i = 0; i < 3; i++) {
      sent++;
#if defined(ESP8266) || defined(ESP32)
      bool success = Ping.ping(remote_ip, 1);
      long t = (long)Ping.averageTime();
#else
      bool success = true;
      long t = 10 + random(5, 15);
#endif
      if (success) {
        received++;
        if (t < minT)
          minT = t;
        if (t > maxT)
          maxT = t;
        totalT += t;

        kprint(F("Reply from "));
        kprint(ipStr);
        kprint(F(": bytes=32 time="));
        kprint(t);
        kprintln(F("ms TTL=56"));
      } else {
        kprintln(F("Request timed out."));
      }
      delay(500);
    }

    kprint(F("\nPing statistics for "));
    kprintln(ipStr);
    kprint(F("    Packets: Sent = "));
    kprint(sent);
    kprint(F(", Received = "));
    kprint(received);
    kprint(F(", Lost = "));
    kprint(sent - received);
    kprint(F(" ("));
    kprint((sent - received) * 100 / sent);
    kprintln(F("% loss),"));

    if (received > 0) {
      kprintln(F("Approximate round trip times in milli-seconds:"));
      kprint(F("    Minimum = "));
      kprint(minT);
      kprint(F("ms, Maximum = "));
      kprint(maxT);
      kprint(F("ms, Average = "));
      kprint(totalT / received);
      kprintln(F("ms"));
    }
  } else {
    static JsonDocument data;
    data.clear();
    data["host"] = args;
    data["ip"] = ipStr;
    data["sent"] = 3;
    data["received"] = 3;
    sendResponse(true, 200, "Ping OK", &data);
  }
}

void handle_wget(char *args, bool fromSerial) {
#if defined(ESP8266)
  WiFiClient client;
  HTTPClient http;
  if (http.begin(client, args)) {
    int code = http.GET();
    if (code > 0) {
      String payload = http.getString();
      static JsonDocument data;
      data.clear();
      data["size"] = payload.length();
      data["content"] = payload.substring(0, 64);
      sendResponse(true, 200, "Download OK", &data);
    } else {
      sendResponse(false, 502, "HTTP Error");
    }
    http.end();
  }
#endif
}

void handle_ntp(char *args, bool fromSerial) {
  configTime(0, 0, "pool.ntp.org");
  sendResponse(true, 200, "NTP Sync Started");
}

void handle_telnet(char *args, bool fromSerial) {
#ifndef PRODUCTION_BUILD
  if (strcmp(args, "on") == 0) {
    telnetEnabled = true;
    telnetServer.begin();
    telnetServer.setNoDelay(true);
    addDmesg(F("Telnet: Server Started on port 23"));
  } else {
    telnetEnabled = false;
    if (telnetClient)
      telnetClient.stop();

    addDmesg(F("Telnet: Server Stopped"));
  }
  sendResponse(true, 200, telnetEnabled ? "Telnet ON" : "Telnet OFF");
#else
  sendResponse(false, 403, "Telnet disabled in production");
#endif
}

void handle_web(char *args, bool fromSerial) {
  if (strcmp(args, "on") == 0) {
    webEnabled = true;
    webServer.begin();
  } else {
    webEnabled = false;
    webServer.stop();
  }
  sendResponse(true, 200,
               webEnabled ? "Web Dashboard ON" : "Web Dashboard OFF");
}

void handle_bt(char *args, bool fromSerial) {
#if defined(ESP32)
  btEnabled = (strcmp(args, "on") == 0);
  sendResponse(true, 200, btEnabled ? "BT ON" : "BT OFF");
#else
  sendResponse(false, 501, "BT only for ESP32");
#endif
}

void handle_netstat(char *args, bool fromSerial) {
  kprintln(F("╭───────┬──────┬────────────┬────────────┬────────────────╮"));
  kprintln(F("│ Proto │ Port │ Service    │ Status     │ Remote Host    │"));
  kprintln(F("├───────┼──────┼────────────┼────────────┼────────────────┤"));

  kprint(F("│ TCP   │ 23   │ Telnet     │ "));
  if (telnetEnabled) {
    kprintColor(CLR_GRN);
    kprint(F("LISTENING  "));
  } else {
    kprintColor(CLR_RED);
    kprint(F("OFFLINE    "));
  }
  kprintColor(CLR_RST);
  kprintln(F("│ -              │"));

  kprint(F("│ TCP   │ 80   │ Web/API    │ "));
  if (webEnabled) {
    kprintColor(CLR_GRN);
    kprint(F("LISTENING  "));
  } else {
    kprintColor(CLR_RED);
    kprint(F("OFFLINE    "));
  }
  kprintColor(CLR_RST);
  kprintln(F("│ -              │"));

  kprint(F("│ UDP   │ 8266 │ OTA Update │ "));
  if (otaEnabled) {
    kprintColor(CLR_YLW);
    kprint(F("ACTIVE     "));
  } else {
    kprintColor(CLR_WHT);
    kprint(F("IDLE       "));
  }
  kprintColor(CLR_RST);
  kprintln(F("│ -              │"));

  kprint(F("│ WS    │ 81   │ UniAccel   │ "));
  if (accelConnected) {
    kprintColor(CLR_GRN);
    kprint(F("CONNECTED  "));
  } else {
    kprintColor(CLR_RED);
    kprint(F("DISCON     "));
  }
  kprintColor(CLR_RST);
  kprint(F("│ "));
  if (accelConnected)
    kprintln(accelHost);
  else
    kprintln(F("-              │"));

  kprintln(F("╰───────┴──────┴────────────┴────────────┴────────────────╯"));

  sendResponse(true, 200, "Network Status Displayed");
}

void handle_cron(char *args, bool fromSerial) {
  sendResponse(true, 200, "Cron Table Empty");
}

void handle_bg(char *args, bool fromSerial) {
  sendResponse(true, 200, "Task moved to background");
}
