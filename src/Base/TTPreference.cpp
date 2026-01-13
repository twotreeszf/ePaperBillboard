#include "TTPreference.h"
#include "ErrorCheck.h"

bool TTPreference::begin() {
    _loaded = false;
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
    if (!_loaded) {
        ERR_CHECK_RET(_load());
    }
    
    if (!_doc.containsKey(key)) {
        return true;
    }
    
    _doc.remove(key);
    _dirty = true;
    return true;
}

bool TTPreference::clear() {
    _doc.clear();
    _loaded = true;
    _dirty = true;
    return true;
}

bool TTPreference::sync() {
    return _save();
}
