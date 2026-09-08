#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#define DEFAULT_CONFIG_FILE "/config.json"
#define TT_CONFIG_FS_BASE      "/cfg"
#define TT_CONFIG_FS_LABEL     "cfg"
#define TT_CONFIG_FS_MAX_OPEN  4

class TTStorage {
public:
    bool begin();
    bool saveConfig(const JsonDocument& config, const char* filename = DEFAULT_CONFIG_FILE);
    bool loadConfig(JsonDocument& config, const char* filename = DEFAULT_CONFIG_FILE);
    bool removeFile(const char* filename);

private:
    fs::LittleFSFS _fs;
    bool _initialized = false;
};
