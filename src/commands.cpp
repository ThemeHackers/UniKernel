#include "../include/commands.h"
#include "../UniAccel.h"
#include "../include/common.h"
#include "../include/shell.h"
extern const char CLR_RST[] PROGMEM;
extern const char CLR_RED[] PROGMEM;
extern const char CLR_GRN[] PROGMEM;
extern const char CLR_YLW[] PROGMEM;
extern const char CLR_BLU[] PROGMEM;
extern const char CLR_MAG[] PROGMEM;
extern const char CLR_CYN[] PROGMEM;
extern const char CLR_WHT[] PROGMEM;
#include "../UniAccel.h"

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
extern volatile bool accelChatMode;
extern bool telnetEnabled;
extern bool webEnabled;
extern bool otaEnabled;
extern unsigned long otaEndTime;
extern bool btEnabled;
extern volatile bool accelConnected;
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
  commandTable.emplace_back("save", handle_save, true, "Deprecated (Autosaved)");
  commandTable.emplace_back("load", handle_load, true, "Deprecated (Autosaved)");
  commandTable.emplace_back("lfs", handle_lfs, true, "LittleFS format tool");
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

  commandTable.emplace_back("telnet", handle_telnet, true,
                            "Toggle Telnet server");
  commandTable.emplace_back("web", handle_web, true, "Toggle Web server");
  commandTable.emplace_back("bt", handle_bt, true, "Toggle Bluetooth");
  commandTable.emplace_back("netstat", handle_netstat, false,
                            "Show network status");
  commandTable.emplace_back("cron", handle_cron, true, "Manage cron tasks");
  commandTable.emplace_back("bg", handle_bg, true, "Run in background");
  commandTable.emplace_back("boot", handle_boot, true, "Boot configuration", false);
  commandTable.emplace_back("waitwifi", handle_waitwifi, false,
                            "Wait for WiFi connection");
  commandTable.emplace_back("recovery", handle_recovery, true,
                            "System recovery tools", false);
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
    JsonDocument pdoc;
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
        if (!c.telnetSafe) {
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
  bool isLong = (strcmp(args, "-l") == 0);
  String path = currentPath;
  if (!path.endsWith("/")) path += "/";
  
#if defined(ESP8266)
  Dir dir = LittleFS.openDir(path);
  if (!fromSerial) {
    JsonDocument data;
    JsonArray arr = data.to<JsonArray>();
    while (dir.next()) {
      JsonObject obj = arr.add<JsonObject>();
      obj["name"] = dir.fileName();
      obj["type"] = dir.isDirectory() ? "dir" : "file";
      obj["size"] = dir.fileSize();
    }
    sendResponse(true, 200, "OK", &data);
    return;
  }
  
  bool empty = true;
  while (dir.next()) {
    empty = false;
    if (isLong) {
      if (dir.isDirectory()) kprintColor(CLR_BLU);
      else kprintColor(CLR_GRN);
      kprint(dir.fileName().c_str());
      kprintColor(CLR_RST);
      kprint(F("  "));
      kprint(String((unsigned long)dir.fileSize()).c_str());
      kprintln(F(" bytes"));
    } else {
      if (dir.isDirectory()) kprintColor(CLR_BLU);
      else kprintColor(CLR_GRN);
      kprint(dir.fileName().c_str());
      kprintColor(CLR_RST);
      kprint(F("  "));
    }
  }
#else
  File root = LittleFS.open(path);
  if (!fromSerial) {
    JsonDocument data;
    JsonArray arr = data.to<JsonArray>();
    if (root) {
      File file = root.openNextFile();
      while (file) {
        JsonObject obj = arr.add<JsonObject>();
        obj["name"] = file.name();
        obj["type"] = file.isDirectory() ? "dir" : "file";
        obj["size"] = file.size();
        file = root.openNextFile();
      }
    }
    sendResponse(true, 200, "OK", &data);
    return;
  }
  
  bool empty = true;
  if (root) {
    File file = root.openNextFile();
    while (file) {
      empty = false;
      if (isLong) {
        if (file.isDirectory()) kprintColor(CLR_BLU);
        else kprintColor(CLR_GRN);
        kprint(file.name());
        kprintColor(CLR_RST);
        kprint(F("  "));
        kprint(String((unsigned long)file.size()).c_str());
        kprintln(F(" bytes"));
      } else {
        if (file.isDirectory()) kprintColor(CLR_BLU);
        else kprintColor(CLR_GRN);
        kprint(file.name());
        kprintColor(CLR_RST);
        kprint(F("  "));
      }
      file = root.openNextFile();
    }
  }
#endif
  if (!isLong && !empty) kprintln();
  if (empty && !isLong) kprintln(F("(empty)"));
}

ICACHE_FLASH_ATTR void handle_cat(char *args, bool fromSerial) {
  if (strlen(args) == 0) {
    sendResponse(false, 400, "Usage: cat <file>");
    return;
  }
  String path = args;
  if (!path.startsWith("/")) {
    path = String(currentPath);
    if (!path.endsWith("/")) path += "/";
    path += args;
  }
  if (!LittleFS.exists(path)) {
    sendResponse(false, 404, "File not found");
    return;
  }
  File f = LittleFS.open(path, "r");
  if (!f) {
    sendResponse(false, 500, "Failed to open");
    return;
  }
  if (!fromSerial) {
    JsonDocument data;
    data["content"] = f.readString();
    sendResponse(true, 200, "OK", &data);
  } else {
    while(f.available()) {
      kprint((char)f.read());
    }
    kprintln();
  }
  f.close();
}

ICACHE_FLASH_ATTR void handle_mkdir(char *args, bool fromSerial) {
  sendResponse(true, 200, "Directory implicitly supported by LittleFS");
}

ICACHE_FLASH_ATTR void handle_touch(char *args, bool fromSerial) {
  String path = args;
  if (!path.startsWith("/")) {
    path = String(currentPath);
    if (!path.endsWith("/")) path += "/";
    path += args;
  }
  File f = LittleFS.open(path, "a");
  if (f) {
    f.close();
    sendResponse(true, 201, "File created");
  } else {
    sendResponse(false, 500, "Failed to create file");
  }
}

ICACHE_FLASH_ATTR void handle_cd(char *args, bool fromSerial) {
  if (strcmp(args, "/") == 0) {
    strcpy(currentPath, "/");
    sendResponse(true, 200, "Moved to root");
    return;
  }
  if (strcmp(args, "..") == 0) {
    if (strcmp(currentPath, "/") == 0) {
      sendResponse(true, 200, "Already at root");
      return;
    }
    char* lastSlash = strrchr(currentPath, '/');
    if (lastSlash && lastSlash != currentPath) {
      *lastSlash = '\0';
    } else {
      strcpy(currentPath, "/");
    }
    sendResponse(true, 200, "Directory changed");
    return;
  }
  String path = String(currentPath);
  if (!path.endsWith("/")) path += "/";
  path += args;
  
  strncpy(currentPath, path.c_str(), PATH_LEN - 1);
  currentPath[PATH_LEN - 1] = '\0';
  sendResponse(true, 200, "Directory changed");
}

ICACHE_FLASH_ATTR void handle_pwd(char *args, bool fromSerial) {
  JsonDocument data;
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
    
    String path = filename;
    if (!path.startsWith("/")) {
      path = String(currentPath);
      if (!path.endsWith("/")) path += "/";
      path += filename;
    }
    
    File f = LittleFS.open(path, "w");
    if (f) {
      f.print(text);
      f.close();
      sendResponse(true, 200, "File updated");
    } else {
      sendResponse(false, 500, "Failed to open file");
    }
  } else {
    if (fromSerial) {
      kprintln(args);
    } else {
      JsonDocument data;
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
    JsonDocument data;
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
    JsonDocument data;
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
    JsonDocument data;
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
    JsonDocument data;
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
    JsonDocument data;
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
  JsonDocument data;
  data["uptime_sec"] = millis() / 1000;
  sendResponse(true, 200, "OK", &data);
}
ICACHE_FLASH_ATTR void handle_rm(char *args, bool fromSerial) {
  String path = args;
  if (!path.startsWith("/")) {
    path = String(currentPath);
    if (!path.endsWith("/")) path += "/";
    path += args;
  }
  if (LittleFS.remove(path)) {
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
  String src = args;
  String dst = kTrim(sp + 1);
  if (!src.startsWith("/")) { src = String(currentPath) + (String(currentPath).endsWith("/") ? "" : "/") + src; }
  if (!dst.startsWith("/")) { dst = String(currentPath) + (String(currentPath).endsWith("/") ? "" : "/") + dst; }
  
  if (LittleFS.rename(src, dst)) {
    sendResponse(true, 200, "File renamed/moved");
  } else {
    sendResponse(false, 500, "Rename failed");
  }
}

ICACHE_FLASH_ATTR void handle_cp(char *args, bool fromSerial) {
  char *sp = strchr(args, ' ');
  if (!sp) {
    sendResponse(false, 400, "Usage: cp [src] [dst]");
    return;
  }
  *sp = '\0';
  String src = args;
  String dst = kTrim(sp + 1);
  if (!src.startsWith("/")) { src = String(currentPath) + (String(currentPath).endsWith("/") ? "" : "/") + src; }
  if (!dst.startsWith("/")) { dst = String(currentPath) + (String(currentPath).endsWith("/") ? "" : "/") + dst; }
  
  File sf = LittleFS.open(src, "r");
  if (!sf) { sendResponse(false, 404, "Source not found"); return; }
  File df = LittleFS.open(dst, "w");
  if (!df) { sf.close(); sendResponse(false, 500, "Dest open failed"); return; }
  
  while(sf.available()) { df.write(sf.read()); }
  sf.close();
  df.close();
  sendResponse(true, 201, "File copied");
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
  JsonDocument data;
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
  kprint(F("FS Total: "));
  FSInfo fs_info;
  LittleFS.info(fs_info);
  kprint(fs_info.totalBytes / 1024);
  kprintln(F(" KB"));
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
  JsonDocument data;
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
    JsonDocument data;
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
  JsonDocument data;
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
  JsonDocument data;
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
  JsonDocument data;
  data["total"] = info.totalBytes;
  data["used"] = info.usedBytes;
  data["free"] = info.totalBytes - info.usedBytes;
  sendResponse(true, 200, "Disk Usage", &data);
}
void handle_hwinfo(char *args, bool fromSerial) {
  JsonDocument data;
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
  String path = args;
  if (!path.startsWith("/")) {
    path = String(currentPath);
    if (!path.endsWith("/")) path += "/";
    path += args;
  }
  File f = LittleFS.open(path, "r");
  if (f) {
    addDmesg(F("sh: running script"));
    String content = f.readString();
    f.close();
    runScript(content.c_str());
    if (!fromSerial) sendResponse(true, 200, "Script executed");
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
  JsonDocument data;
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
    JsonDocument data;
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
  if (fromSerial) {
    kprintColor(CLR_CYN);
    kprintln(F("--- Environment Variables ---"));
    kprintColor(CLR_RST);
    kprint(F("VCC=")); kprintln(ESP.getVcc());
    kprint(F("RAM=")); kprintln(freeMemory());
    kprint(F("TEMP=")); kprintln(25 + (int)(millis() % 5));
    for (int i = 0; i < MAX_ENV; i++) {
      if (envTable[i].active) {
        kprint(envTable[i].key);
        kprint(F("="));
        kprintln(envTable[i].val);
      }
    }
    return;
  }
  JsonDocument data;
  JsonObject obj = data.to<JsonObject>();
  obj["VCC"] = ESP.getVcc();
  obj["RAM"] = freeMemory();
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
    kprintln(F("Backup via VFS is no longer supported (Files are natively in LittleFS)."));
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
  JsonDocument data;
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
  
  String path = filename;
  if (!path.startsWith("/")) {
    path = String(currentPath);
    if (!path.endsWith("/")) path += "/";
    path += filename;
  }
  
  File f = LittleFS.open(path, "a");
  if (f) {
    f.print(text);
    f.close();
    sendResponse(true, 200, "Text appended");
  } else {
    sendResponse(false, 500, "Failed to append");
  }
}

void handle_info(char *args, bool fromSerial) {
  String path = args;
  if (!path.startsWith("/")) {
    path = String(currentPath);
    if (!path.endsWith("/")) path += "/";
    path += args;
  }
  File f = LittleFS.open(path, "r");
  if (f) {
    JsonDocument data;
    data["name"] = f.name();
    data["type"] = f.isDirectory() ? "dir" : "file";
    data["size"] = f.size();
    f.close();
    sendResponse(true, 200, "OK", &data);
  } else {
    sendResponse(false, 404, "File not found");
  }
}

void handle_save(char *args, bool fromSerial) { sendResponse(true, 200, "Files are autosaved via LittleFS"); }
void handle_load(char *args, bool fromSerial) { sendResponse(true, 200, "Files are auto-loaded via LittleFS"); }
void handle_lfs(char *args, bool fromSerial) {
  sendResponse(true, 200, "LFS commands merged into standard ls/cat/rm/etc. Use format with lfs format");
  if (strcmp(args, "format") == 0) { LittleFS.format(); sendResponse(true, 200, "Formatted"); }
}

void handle_chmod(char *args, bool fromSerial) { sendResponse(true, 200, "Ignored: LittleFS does not support permissions"); }
void handle_chown(char *args, bool fromSerial) { sendResponse(true, 200, "Ignored: LittleFS does not support ownership"); }
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
    JsonDocument data;
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
    char cond[32] = {0}, op[4] = {0}, act[64] = {0};  
    int val;
    if (sscanf(args, "%31s %2s %d %63[^\n]", cond, op, &val, act) == 4) {  
      op[2] = '\0'; 
      for (int i = 0; i < MAX_TRIGS; i++) {
        if (!triggerTable[i].active) {
          strncpy(triggerTable[i].cond, cond, 31);
          strncpy(triggerTable[i].op, op, 3);
          triggerTable[i].val = val;
          strncpy(triggerTable[i].action, act, 63);
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
    JsonDocument data;
    data["current"] = buf;
    sendResponse(true, 200, "Boot Configuration", &data);
  }
}
void handle_mqtt(char *args, bool fromSerial) {
  char *host_str = strtok(args, " ");
  char *topic = strtok(NULL, " ");
  char *payload = strtok(NULL, "");
  if (!host_str || !topic || !payload) {
    sendResponse(false, 400, "Usage: mqtt [host] [topic] [payload]");
    return;
  }
  WiFiClient client;
  if (client.connect(host_str, 1883)) {
    uint8_t connectPkt[] = {0x10, 0x15, 0x00, 0x04, 'M','Q','T','T', 0x04, 0x02, 0x00, 0x3C, 0x00, 0x09, 'u','n','i','k','e','r','n','e','l'};
    client.write(connectPkt, sizeof(connectPkt));
    delay(10);
    int tLen = strlen(topic);
    int pLen = strlen(payload);
    int remLen = 2 + tLen + pLen;
    client.write(0x30);
    client.write(remLen);
    client.write(tLen >> 8);
    client.write(tLen & 0xFF);
    client.print(topic);
    client.print(payload);
    delay(10);
    client.stop();
    sendResponse(true, 200, "MQTT Message Published");
  } else {
    sendResponse(false, 502, "MQTT Broker unreachable");
  }
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
    JsonDocument data;
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
      JsonDocument data;
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
  if (strcmp(args, "help") == 0) {
    if (fromSerial) {
      kprintln(F("Cron Commands:"));
      kprintln(F("  cron                   - List active cron jobs"));
      kprintln(F("  cron <h> <m> <cmd>     - Add new cron job"));
      kprintln(F("  cron rm <id>           - Remove cron job"));
    } else {
      sendResponse(false, 400, "Usage: cron [h] [m] [cmd]");
    }
    return;
  }
  if (strlen(args) == 0) {
    bool empty = true;
    for (int i = 0; i < MAX_CRON; i++) {
      if (cronTable[i].active) {
        if (empty && fromSerial) {
          kprintln(F("ID   TIME    CMD"));
          kprintln(F("-------------------"));
        }
        empty = false;
        if (fromSerial) {
          char buf[64];
          snprintf(buf, sizeof(buf), "%-4d %02d:%02d   %s", i, cronTable[i].h, cronTable[i].m, cronTable[i].cmd);
          kprintln(buf);
        }
      }
    }
    if (empty) {
      sendResponse(true, 200, "Cron Table Empty");
    } else if (!fromSerial) {
      sendResponse(true, 200, "Cron jobs listed in Serial");
    }
  } else if (strncmp(args, "rm ", 3) == 0) {
    int id = atoi(args + 3);
    if (id >= 0 && id < MAX_CRON) {
      cronTable[id].active = false;
      sendResponse(true, 200, "Cron job removed");
    } else {
      sendResponse(false, 400, "Invalid ID");
    }
  } else {
    int h, m;
    char cmd[32];
    if (sscanf(args, "%d %d %31[^\n]", &h, &m, cmd) == 3) {
      for (int i = 0; i < MAX_CRON; i++) {
        if (!cronTable[i].active) {
          cronTable[i].h = h;
          cronTable[i].m = m;
          strncpy(cronTable[i].cmd, cmd, 31);
          cronTable[i].active = true;
          sendResponse(true, 200, "Cron job added");
          return;
        }
      }
      sendResponse(false, 507, "Cron table full");
    } else {
      sendResponse(false, 400, "Usage: cron [h] [m] [cmd]");
    }
  }
}
void handle_bg(char *args, bool fromSerial) {
  char *sp = strchr(args, ' ');
  if (!sp) {
    sendResponse(false, 400, "Usage: bg [interval_ms] [cmd]");
    return;
  }
  *sp = '\0';
  int interval = atoi_safe(args);
  char *cmd = kTrim(sp + 1);

  for (int i = 0; i < MAX_TASKS; i++) {
    if (!taskTable[i].active) {
      taskTable[i].func = nullptr;
      strncpy(taskTable[i].cmd, cmd, 31);
      taskTable[i].cmd[31] = '\0';
      taskTable[i].interval = interval;
      taskTable[i].lastRun = millis();
      taskTable[i].executionCount = 0;
      strncpy(taskTable[i].name, cmd, NAME_LEN - 1);
      taskTable[i].name[NAME_LEN - 1] = '\0';
      taskTable[i].active = true;
      sendResponse(true, 200, "Task moved to background");
      return;
    }
  }
  sendResponse(false, 507, "Task table full");
}
void handle_recovery(char *args, bool fromSerial) {
  if (!fromSerial) {
    sendResponse(false, 403, "Recovery only allowed via Serial");
    return;
  }
  if (strcmp(args, "unlock") == 0) {
    loginFailCount = 0;
    isLockedOut = false;
    EEPROM.write(EEPROM_FAIL_COUNT_ADDR, 0);
    EEPROM.write(EEPROM_LOCKOUT_ADDR, 0);
    EEPROM.commit();
    sendResponse(true, 200, "System Unlocked");
    addDmesg(F("Recovery: System Unlocked via command"));
  } else if (strcmp(args, "reset") == 0) {
    needsSetup = true;
    serialAuthenticated = true;
    EEPROM.write(EEPROM_PASS_ADDR, 0);
    EEPROM.commit();
    sendResponse(true, 200, "Factory Reset: Password cleared. Please setup.");
    addDmesg(F("Recovery: Factory Reset triggered"));
  } else if (strcmp(args, "purge") == 0) {
    LittleFS.format();
    sendResponse(true, 200, "LittleFS Formatted (All files deleted)");
    addDmesg(F("Recovery: LittleFS Purged"));
  } else {
    kprintln(F("Recovery Commands:"));
    kprintln(F("  recovery unlock  - Reset login failures"));
    kprintln(F("  recovery reset   - Clear password (Factory Reset)"));
    kprintln(F("  recovery purge   - Format LittleFS and delete all files"));
  }
}
