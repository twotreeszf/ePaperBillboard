#pragma once

#include "TTStorage.h"
#include "ErrorCheck.h"
#include <ArduinoJson.h>

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
    TTStorage storage;
    JsonDocument _doc;
    bool _loaded = false;
    bool _dirty = false;
    
    bool _load();
    bool _save();
};

// Template function implementations
template<typename T>
bool TTPreference::get(const char* key, T& outValue, const T& defaultValue) {
    if (!_loaded) {
        ERR_CHECK_RET(_load());
    }
    
    if (!_doc.containsKey(key)) {
        outValue = defaultValue;
        return true;
    }
    
    outValue = _doc[key].as<T>();
    return true;
}

template<typename T>
bool TTPreference::set(const char* key, T value) {
    if (!_loaded) {
        ERR_CHECK_RET(_load());
    }
    
    _doc[key] = value;
    _dirty = true;
    return true;
}
