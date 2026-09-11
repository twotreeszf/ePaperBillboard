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

bool TTPreference::_kvPrepare(const char* list, bool create) {
    JsonVariantConst existing = _doc.as<JsonObjectConst>()[list];
    if (existing.is<JsonObjectConst>()) {
        return true;
    }

    const bool asString = existing.is<const char*>() || existing.is<String>();
    if (!asString && !create) {
        return false;
    }

    JsonDocument parsed;
    if (asString) {
        const String raw = existing.as<String>();
        if (!raw.isEmpty()) {
            const DeserializationError err = deserializeJson(parsed, raw);
            if (err) {
                LOG_W("Preference: kv %s json bad err=%s", list, err.c_str());
                parsed.clear();
            }
        }
    }

    _doc.remove(list);
    JsonObject obj = _doc[list].to<JsonObject>();
    for (JsonPair kv : parsed.as<JsonObject>()) {
        obj[kv.key()] = kv.value();
    }
    _dirty = true;
    if (asString) {
        LOG_I("Preference: kv %s migrated from string count=%u",
              list, (unsigned)obj.size());
    }
    return true;
}

bool TTPreference::removeKv(const char* list, const char* key) {
    ensureMutex();
    Lock lock(_mutex);
    if (!_loaded) {
        ERR_CHECK_RET(_load());
    }
    if (list == nullptr || list[0] == '\0' || key == nullptr || key[0] == '\0'
        || !_kvPrepare(list, false)) {
        return true;
    }
    if (_doc[list].as<JsonObjectConst>()[key].isNull()) {
        return true;
    }
    _doc[list].as<JsonObject>().remove(key);
    _dirty = true;
    return true;
}

bool TTPreference::clearKv(const char* list) {
    ensureMutex();
    Lock lock(_mutex);
    if (!_loaded) {
        ERR_CHECK_RET(_load());
    }
    if (list == nullptr || list[0] == '\0') {
        return true;
    }
    if (_doc.as<JsonObjectConst>()[list].isNull()) {
        return true;
    }
    _doc.remove(list);
    _dirty = true;
    return true;
}

bool TTPreference::hasKv(const char* list, const char* key) {
    ensureMutex();
    Lock lock(_mutex);
    if (!_loaded && !_load()) {
        return false;
    }
    if (list == nullptr || list[0] == '\0' || key == nullptr || key[0] == '\0'
        || !_kvPrepare(list, false)) {
        return false;
    }
    return !_doc[list].as<JsonObjectConst>()[key].isNull();
}

size_t TTPreference::kvSize(const char* list) {
    ensureMutex();
    Lock lock(_mutex);
    if (!_loaded && !_load()) {
        return 0;
    }
    if (list == nullptr || list[0] == '\0' || !_kvPrepare(list, false)) {
        return 0;
    }
    return _doc[list].as<JsonObject>().size();
}

bool TTPreference::kvKeys(const char* list, std::vector<String>& keys) {
    ensureMutex();
    Lock lock(_mutex);
    keys.clear();
    if (!_loaded) {
        ERR_CHECK_RET(_load());
    }
    if (list == nullptr || list[0] == '\0' || !_kvPrepare(list, false)) {
        return true;
    }
    for (JsonPairConst kv : _doc[list].as<JsonObjectConst>()) {
        keys.push_back(String(kv.key().c_str()));
    }
    return true;
}
