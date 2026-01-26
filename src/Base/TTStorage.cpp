#include "TTStorage.h"
#include "Logger.h"
#include "ErrorCheck.h"
#include <LittleFS.h>

bool TTStorage::begin()
{
    // Mount LittleFS
    if (!LittleFS.begin()) {
        LOG_E("LittleFS mount failed");
        return false;
    }
    LOG_I("LittleFS mounted successfully");
    
    // Get storage info
    LOG_I("Storage: %u KB total, %u KB used", (uint32_t)LittleFS.totalBytes() / 1024, (uint32_t)LittleFS.usedBytes() / 1024);

    _initialized = true;
    return true;
}

bool TTStorage::saveConfig(const JsonDocument& config, const char* filename)
{
    if (!_initialized) return false;
    
    File file = LittleFS.open(filename, "w");
    if (!file) {
        LOG_E("Failed to open file for writing: %s", filename);
        return false;
    }
    
    if (serializeJson(config, file) == 0) {
        LOG_E("Failed to write to file: %s", filename);
        file.close();
        return false;
    }
        
    file.close();
    LOG_I("Config saved to: %s", filename);
    return true;
}

bool TTStorage::loadConfig(JsonDocument& config, const char* filename)
{
    if (!_initialized) return false;
    if (!LittleFS.exists(filename)) {
        LOG_I("Config file not found: %s", filename);
        return true;
    }
    
    File file = LittleFS.open(filename, "r");
    if (!file) {
        LOG_E("Failed to open file for reading: %s", filename);
        return false;
    }
    
    DeserializationError error = deserializeJson(config, file);
    if (error) {
        LOG_E("Failed to parse config: %s", error.c_str());
        file.close();
        return false;
    }
    file.close();
        
    LOG_I("Config loaded from: %s", filename);
    return true;
}

bool TTStorage::removeFile(const char* filename)
{
    if (!_initialized) return false;
    if (!LittleFS.exists(filename)) return true;
    
    if (!LittleFS.remove(filename)) {
        LOG_E("Failed to remove file: %s", filename);
        return false;
    }
    
    LOG_I("File removed: %s", filename);
    return true;
}
