#include "TTStorage.h"
#include "Logger.h"
#include "ErrorCheck.h"

bool TTStorage::begin()
{
    if (!_fs.begin(true, TT_CONFIG_FS_BASE, TT_CONFIG_FS_MAX_OPEN, TT_CONFIG_FS_LABEL)) {
        LOG_E("Config FS mount failed partition=%s", TT_CONFIG_FS_LABEL);
        return false;
    }
    LOG_I("Config FS mounted partition=%s total=%u KB used=%u KB",
          TT_CONFIG_FS_LABEL,
          (uint32_t)_fs.totalBytes() / 1024,
          (uint32_t)_fs.usedBytes() / 1024);

    _initialized = true;
    return true;
}

bool TTStorage::saveConfig(const JsonDocument& config, const char* filename)
{
    if (!_initialized) return false;

    File file = _fs.open(filename, "w");
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
    LOG_I("Config saved to: %s (partition=%s)", filename, TT_CONFIG_FS_LABEL);
    return true;
}

bool TTStorage::loadConfig(JsonDocument& config, const char* filename)
{
    if (!_initialized) return false;
    if (!_fs.exists(filename)) {
        LOG_I("Config file not found: %s", filename);
        return true;
    }

    File file = _fs.open(filename, "r");
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

    LOG_I("Config loaded from: %s (partition=%s)", filename, TT_CONFIG_FS_LABEL);
    return true;
}

bool TTStorage::removeFile(const char* filename)
{
    if (!_initialized) return false;
    if (!_fs.exists(filename)) return true;

    if (!_fs.remove(filename)) {
        LOG_E("Failed to remove file: %s", filename);
        return false;
    }

    LOG_I("File removed: %s (partition=%s)", filename, TT_CONFIG_FS_LABEL);
    return true;
}
