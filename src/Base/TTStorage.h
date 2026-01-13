#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#define DEFAULT_CONFIG_FILE "/config.json"

class TTStorage {
public:
    bool begin();
    bool saveConfig(const JsonDocument& config, const char* filename = DEFAULT_CONFIG_FILE);    
    bool loadConfig(JsonDocument& config, const char* filename = DEFAULT_CONFIG_FILE);
    bool removeFile(const char* filename);

private:
    bool _initialized = false;
};
