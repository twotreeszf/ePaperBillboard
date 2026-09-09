#pragma once

#include "TTStorage.h"
#include "ErrorCheck.h"
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define CAPACITY 1024 * 4

class TTPreference {
public:
    bool begin();
    
    // Get config value, use default value if key doesn't exist
    template<typename T>
    bool get(const char* key, T& outValue, const T& defaultValue);
    
    // Set config value
    template<typename T>
    bool set(const char* key, T value);
    
    // Remove a config item
    bool remove(const char* key);
    
    // Clear all config items
    bool clear();
    
    // Save changes to storage if needed
    bool sync();

private:
    class Lock {
    public:
        explicit Lock(SemaphoreHandle_t mutex) : _mutex(mutex) {
            if (_mutex != nullptr) {
                xSemaphoreTake(_mutex, portMAX_DELAY);
            }
        }
        ~Lock() {
            if (_mutex != nullptr) {
                xSemaphoreGive(_mutex);
            }
        }
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
    private:
        SemaphoreHandle_t _mutex;
    };

    void ensureMutex();
    bool _load();
    bool _save();

    TTStorage storage;
    JsonDocument _doc;
    SemaphoreHandle_t _mutex = nullptr;
    bool _loaded = false;
    bool _dirty = false;
};

template<typename T>
bool TTPreference::get(const char* key, T& outValue, const T& defaultValue) {
    ensureMutex();
    Lock lock(_mutex);
    if (!_loaded) {
        ERR_CHECK_RET(_load());
    }
    
    if (!_doc[key].template is<T>()) {
        outValue = defaultValue;
        return true;
    }
    
    outValue = _doc[key].as<T>();
    return true;
}

template<typename T>
bool TTPreference::set(const char* key, T value) {
    ensureMutex();
    Lock lock(_mutex);
    if (!_loaded) {
        ERR_CHECK_RET(_load());
    }
    
    _doc[key] = value;
    _dirty = true;
    return true;
}
