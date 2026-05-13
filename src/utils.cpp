#include "../include/common.h"

char *kTrim(char *s) {
    if (!s) return s;
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

int kParseArgs(char *line, char **argv, int maxArgs) {
    int argc = 0;
    char *p = line;
    bool inQuote = false;

    while (*p && argc < maxArgs) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0') break;

        if (*p == '\"') {
            p++;
            argv[argc++] = p;
            while (*p && (*p != '\"')) p++;
            if (*p == '\"') {
                *p = '\0';
                p++;
            }
        } else {
            argv[argc++] = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (*p) {
                *p = '\0';
                p++;
            }
        }
    }
    return argc;
}

void stripQuotes(char *s) {
    if (!s) return;
    char *start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
        start++;
    int len = strlen(start);
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t' ||
                       start[len - 1] == '\r' || start[len - 1] == '\n')) {
        start[len - 1] = '\0';
        len--;
    }
    if (len > 0 && start[len - 1] == '\"') {
        start[len - 1] = '\0';
        len--;
    }
    if (len > 0 && start[0] == '\"') {
        memmove(s, start + 1, len);
    } else if (start != s) {
        memmove(s, start, len + 1);
    }
}

void toLowercase(char *s) {
    if (!s) return;
    for (int i = 0; s[i]; i++) {
        s[i] = tolower(s[i]);
    }
}

void safeStrncpy(char *dest, const char *src, size_t n) {
    if (n == 0) return;
    strncpy(dest, src, n - 1);
    dest[n - 1] = '\0';
}

void safeStrncat(char *dest, const char *src, size_t n) {
    size_t len = strlen(dest);
    if (len < n) {
        strncat(dest, src, n - len - 1);
    }
    dest[n - 1] = '\0';
}

int atoi_safe(const char *s) {
    if (!s || !*s) return 0;
    while (*s == ' ') s++;
    if (!isdigit(*s) && *s != '-' && *s != '+') return 0;
    return atoi(s);
}

int indexOf(const char *s, const char *target) {
    if (!s || !target) return -1;
    char *p = strstr(s, target);
    if (p) return p - s;
    return -1;
}

void sendResponse(bool ok, int code, const char *message, JsonDocument *data) {
    if (!ok) {
        kprintColor("\033[1;31m");
        kprint("[ERROR] ");
        kprintColor("\033[0m");
        kprintln(message);
    } else {
        if (strlen(message) > 0 && strcmp(message, "OK") != 0 && strcmp(message, "Commands List") != 0) {
            kprintln(message);
        }
    }

    if (data) {
        if (data->is<JsonArray>()) {
            JsonArray arr = data->as<JsonArray>();
            for (JsonVariant v : arr) {
                if (v.is<JsonObject>()) {
                    JsonObject obj = v.as<JsonObject>();
                    bool first = true;
                    for (JsonPair p : obj) {
                        if (!first) kprint("  ");
                        kprint(p.value().as<String>().c_str());
                        first = false;
                    }
                    kprintln();
                } else {
                    kprintln(v.as<String>().c_str());
                }
            }
        } else if (data->is<JsonObject>()) {
            JsonObject obj = data->as<JsonObject>();
            if (obj.containsKey("content")) {
                kprintln(obj["content"].as<const char*>());
            } else {
                for (JsonPair p : obj) {
                    kprint(p.key().c_str());
                    kprint(": ");
                    kprintln(p.value().as<String>().c_str());
                }
            }
        }
    }

    if (!isSerialSession) {
        static JsonDocument doc;
        doc.clear();
        doc["ok"] = ok;
        doc["code"] = code;
        doc["msg"] = message;
        if (data) doc["data"] = *data;
        serializeJson(doc, Serial);
        Serial.println();
    }
}

void safeConcatPath(char *base, const char *extra) {
    if (strcmp(extra, "..") == 0) {
        char *lastSlash = strrchr(base, '/');
        if (lastSlash && lastSlash != base) {
            *lastSlash = '\0';
        } else if (lastSlash == base) {
            base[1] = '\0';
        }
        return;
    }
    if (strcmp(extra, ".") == 0 || strcmp(extra, "/") == 0) return;

    size_t len = strlen(base);
    if (len >= PATH_LEN - 1) return;
    if (len > 1 && base[len - 1] != '/' && extra[0] != '/') {
        strncat(base, "/", PATH_LEN - strlen(base) - 1);
    }
    strncat(base, extra, PATH_LEN - strlen(base) - 1);
    base[PATH_LEN - 1] = '\0';
}
