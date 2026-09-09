#include "TTPreference.h"
#include "ErrorCheck.h"
#include "Logger.h"

void TTPreference::ensureMutex() {
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
        if (_mutex == nullptr) {
            LOG_E("Preference: mutex create failed");
        }
    }
}

bool TTPreference::begin() {
    ensureMutex();
    Lock lock(_mutex);
    _dirty = false;
    ERR_CHECK_RET(storage.begin());
    ERR_CHECK_RET(_load());
    return true;
}

bool TTPreference::_load() {
    _doc.clear();
    ERR_CHECK_RET(storage.loadConfig(_doc));
    _loaded = true;
    _dirty = false;
    return true;
}

bool TTPreference::_save() {
    if (!_dirty) {
        return true;
    }
    
    ERR_CHECK_RET(storage.saveConfig(_doc));
    _dirty = false;
    return true;
}

bool TTPreference::remove(const char* key) {
    ensureMutex();
    Lock lock(_mutex);
    if (!_loaded) {
        ERR_CHECK_RET(_load());
    }
    
    if (_doc[key].isNull()) {
        return true;
    }
    
    _doc.remove(key);
    _dirty = true;
    return true;
}

bool TTPreference::clear() {
    ensureMutex();
    Lock lock(_mutex);
    _doc.clear();
    _loaded = true;
    _dirty = true;
    return true;
}

bool TTPreference::sync() {
    ensureMutex();
    Lock lock(_mutex);
    return _save();
}
