#include "TTBookTransferServer.h"
#include "TTSdCardService.h"
#include "../Base/Logger.h"
#include "../Base/TTFile.h"
#include "../Base/TTHttpFile.h"
#include "../Base/TTInstance.h"
#include "../Base/Util.h"
#include "../Reader/TTBookStore.h"
#include "../Reader/TTReaderText.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <cstring>
#include <strings.h>

TTBookTransferError TTBookTransferServer::begin(EventCallback onEvent) {
    if (_running) {
        return TT_BOOK_TRANSFER_ERROR_NONE;
    }
    _abort = false;
    _received = 0;
    _imageStatusShown = false;
    _onEvent = std::move(onEvent);

    TTSdCardService& sd = TTInstanceOf<TTSdCardService>();
    if (sd.acquire() != TT_SD_MOUNT_OK) {
        LOG_E("Transfer: SD unavailable");
        return TT_BOOK_TRANSFER_ERROR_SD_FAILED;
    }
    _sdAcquired = true;
    if (!TTInstanceOf<TTBookStore>().ensureDirs()) {
        LOG_E("Transfer: books dir unavailable");
        sd.release();
        _sdAcquired = false;
        return TT_BOOK_TRANSFER_ERROR_SD_FAILED;
    }
    const String temp = tempPath();
    if (sd.fs().exists(temp)) {
        LOG_W("Transfer: remove stale %s", temp.c_str());
        sd.fs().remove(temp);
    }

    if (!_routesReady) {
        _server.on("/", HTTP_GET, [this]() { handleRoot(); });
        _server.on("/books", HTTP_GET, [this]() { handleBooks(); });
        _server.on("/upload", HTTP_POST, [this]() { handleUploadDone(); }, [this]() { handleUpload(); });
        _server.on("/images-done", HTTP_POST, [this]() { handleImagesDone(); });
        _server.on("/delete", HTTP_POST, [this]() { handleDelete(); });
        _server.onNotFound([this]() { _server.send(404, "text/plain", "Not found"); });
        _routesReady = true;
    }
    _server.begin();
    _running = true;
    LOG_I("Transfer: server on http://%s/ heap=%u largest=%u", WiFi.localIP().toString().c_str(),
          (unsigned)Util::heapFree(), (unsigned)Util::heapLargest());
    return TT_BOOK_TRANSFER_ERROR_NONE;
}

void TTBookTransferServer::stop() {
    if (_uploadActive) {
        LOG_W("Transfer: stop during upload %s, %u bytes dropped", _uploadName.c_str(), (unsigned)_uploadBytes);
        closeTemp();
        _uploadActive = false;
    }
    if (_running) {
        _server.stop();
        _running = false;
        LOG_I("Transfer: server stopped, %u book(s) received", (unsigned)_received);
    }
    if (_sdAcquired) {
        TTInstanceOf<TTSdCardService>().release();
        _sdAcquired = false;
    }
    _onEvent = nullptr;
    _imageStatusShown = false;
}

void TTBookTransferServer::handleClient() {
    if (_running) {
        _server.handleClient();
    }
}

String TTBookTransferServer::tempPath() const {
    return String(TT_BOOKS_CACHE_DIR "/" TT_TRANSFER_TMP_NAME);
}

void TTBookTransferServer::emit(TTBookTransferState state, const char* message) {
    if (_onEvent) {
        _onEvent(state, _uploadName.c_str(), _uploadBytes, message);
    }
}

const char* TTBookTransferServer::nameError(const String& name) const {
    if (name.isEmpty() || name.length() > TT_BOOK_TRANSFER_NAME_MAX || name[0] == '.') {
        return TT_TRANSFER_MSG_BAD_NAME;
    }
    for (size_t i = 0; i < name.length(); i++) {
        const uint8_t c = (uint8_t)name[i];
        if (c < 0x20 || strchr(TT_TRANSFER_INVALID_CHARS, c) != nullptr) {
            return TT_TRANSFER_MSG_BAD_NAME;
        }
    }
    if (tt_utf8_count_invalid((const uint8_t*)name.c_str(), name.length()) > 0) {
        return TT_TRANSFER_MSG_BAD_NAME;
    }
    if (TTBookStore::bookExtLength(name.c_str()) == 0) {
        return TT_TRANSFER_MSG_BAD_TYPE;
    }
    return nullptr;
}

void TTBookTransferServer::handleUpload() {
    HTTPUpload& upload = _server.upload();
    switch (upload.status) {
        case UPLOAD_FILE_START:
            startUpload(_server.hasArg("name") ? _server.arg("name") : upload.filename);
            break;
        case UPLOAD_FILE_WRITE:
            if (_abort) {
                failUpload(TT_TRANSFER_MSG_ABORTED);
                _server.client().stop();
                break;
            }
            writeUpload(upload.buf, upload.currentSize);
            break;
        case UPLOAD_FILE_END:
            finishUpload();
            break;
        case UPLOAD_FILE_ABORTED:
            if (_uploadActive) {
                failUpload(TT_TRANSFER_MSG_ABORTED);
            }
            break;
    }
}

void TTBookTransferServer::startUpload(const String& rawName) {
    if (_uploadActive) {
        failUpload(TT_TRANSFER_MSG_ABORTED);
    }
    _uploadSeen = true;
    _uploadDone = false;
    _uploadError = nullptr;
    _uploadBytes = 0;
    _nextLogBytes = TT_TRANSFER_LOG_STEP;

    String name = rawName;
    const int slash = name.lastIndexOf('/');
    const int backslash = name.lastIndexOf('\\');
    const int cut = slash > backslash ? slash : backslash;
    if (cut >= 0) {
        name = name.substring(cut + 1);
    }
    name.trim();
    _uploadName = name;
    _uploadTarget = "";
    _imageName = "";
    _uploadIsImage = false;
    if (_server.hasArg("image")) {
        _imageName = _server.arg("image");
        _imageName.trim();
    }

    const char* error = nameError(name);
    if (error != nullptr) {
        LOG_W("Transfer: reject name '%s': %s", rawName.c_str(), error);
        _uploadError = error;
        emit(TT_BOOK_TRANSFER_UPLOAD_FAILED, error);
        return;
    }
    if (_imageName.length() > 0) {
        if (_imageName.length() != TT_TRANSFER_IMAGE_NAME_LEN || !_imageName.endsWith(".i1")) {
            error = TT_TRANSFER_MSG_BAD_NAME;
        }
        for (uint8_t i = 0; error == nullptr && i < 4; i++) {
            if (_imageName[i] < '0' || _imageName[i] > '9') {
                error = TT_TRANSFER_MSG_BAD_NAME;
            }
        }
        if (error != nullptr) {
            LOG_W("Transfer: reject image '%s'", _imageName.c_str());
            _uploadError = error;
            emit(TT_BOOK_TRANSFER_UPLOAD_FAILED, error);
            return;
        }
        fs::FS& fs = TTInstanceOf<TTSdCardService>().fs();
        TTBookEntry book;
        book.name = name;
        File existing = fs.open(String(TT_BOOKS_DIR "/") + name);
        if (!existing || existing.isDirectory()) {
            if (existing) {
                existing.close();
            }
            LOG_W("Transfer: image book missing %s", name.c_str());
            _uploadError = TT_TRANSFER_MSG_NOT_FOUND;
            emit(TT_BOOK_TRANSFER_UPLOAD_FAILED, _uploadError);
            return;
        }
        book.size = (uint32_t)existing.size();
        existing.close();
        TTBookStore& store = TTInstanceOf<TTBookStore>();
        if (!store.ensureBookCache(book)) {
            _uploadError = TT_TRANSFER_MSG_OPEN_FAILED;
            emit(TT_BOOK_TRANSFER_UPLOAD_FAILED, _uploadError);
            return;
        }
        _uploadIsImage = true;
        _uploadTarget = store.cacheDir(book) + "/" + _imageName;
        LOG_I("Transfer: image %s for %s -> %s", _imageName.c_str(), name.c_str(), _uploadTarget.c_str());
    }
    _file = tt_file_open(TTInstanceOf<TTSdCardService>().fs(), tempPath().c_str(), "w");
    if (!_file) {
        _uploadError = TT_TRANSFER_MSG_OPEN_FAILED;
        emit(TT_BOOK_TRANSFER_UPLOAD_FAILED, _uploadError);
        return;
    }
    _uploadActive = true;
    _uploadStartMs = millis();
    LOG_I("Transfer: receiving %s from %s heap=%u largest=%u", name.c_str(),
          _server.client().remoteIP().toString().c_str(),
          (unsigned)Util::heapFree(), (unsigned)Util::heapLargest());
    if (_uploadIsImage) {
        if (!_imageStatusShown) {
            _imageStatusShown = true;
            emit(TT_BOOK_TRANSFER_RECEIVING, TT_TRANSFER_MSG_IMAGE);
        }
    } else {
        _imageStatusShown = false;
        emit(TT_BOOK_TRANSFER_RECEIVING, nullptr);
    }
}

void TTBookTransferServer::writeUpload(const uint8_t* data, size_t len) {
    if (!_uploadActive || len == 0) {
        return;
    }
    const uint32_t writeStart = millis();
    if (_file.write(data, len) != len) {
        LOG_E("Transfer: write failed at %u bytes", (unsigned)_uploadBytes);
        failUpload(TT_TRANSFER_MSG_WRITE_FAILED);
        return;
    }
    const uint32_t writeMs = millis() - writeStart;
    if (writeMs >= TT_TRANSFER_SLOW_WRITE_MS) {
        LOG_W("Transfer: slow write %u ms at %u bytes heap=%u", (unsigned)writeMs,
              (unsigned)_uploadBytes, (unsigned)Util::heapFree());
    }
    _uploadBytes += (uint32_t)len;
    if (_uploadBytes >= _nextLogBytes) {
        const uint32_t elapsed = millis() - _uploadStartMs;
        LOG_I("Transfer: %s %u KB, %u KB/s", _uploadName.c_str(), (unsigned)(_uploadBytes / 1024),
              (unsigned)(elapsed > 0 ? (uint64_t)_uploadBytes * 1000 / 1024 / elapsed : 0));
        _nextLogBytes += TT_TRANSFER_LOG_STEP;
    }
}

void TTBookTransferServer::finishUpload() {
    if (!_uploadActive) {
        return;
    }
    _file.close();
    _uploadActive = false;
    fs::FS& fs = TTInstanceOf<TTSdCardService>().fs();
    const String temp = tempPath();
    if (_uploadBytes == 0) {
        fs.remove(temp);
        _uploadError = TT_TRANSFER_MSG_EMPTY;
        LOG_W("Transfer: %s is empty", _uploadName.c_str());
        emit(TT_BOOK_TRANSFER_UPLOAD_FAILED, _uploadError);
        return;
    }
    const String target = _uploadTarget.length() > 0 ? _uploadTarget : String(TT_BOOKS_DIR "/") + _uploadName;
    if (fs.exists(target)) {
        LOG_I("Transfer: replace existing %s", target.c_str());
        fs.remove(target);
    }
    if (!fs.rename(temp, target)) {
        LOG_E("Transfer: rename %s -> %s failed", temp.c_str(), target.c_str());
        fs.remove(temp);
        _uploadError = TT_TRANSFER_MSG_SAVE_FAILED;
        emit(TT_BOOK_TRANSFER_UPLOAD_FAILED, _uploadError);
        return;
    }
    _uploadDone = true;
    if (!_uploadIsImage) {
        _received++;
        _imageStatusShown = false;
    }
    const uint32_t elapsed = millis() - _uploadStartMs;
    LOG_I("Transfer: saved %s %u bytes in %u ms (%u KB/s)", target.c_str(), (unsigned)_uploadBytes,
          (unsigned)elapsed, (unsigned)(elapsed > 0 ? (uint64_t)_uploadBytes * 1000 / 1024 / elapsed : 0));
    if (_uploadIsImage) {
        return;
    }
    emit(TT_BOOK_TRANSFER_RECEIVED, nullptr);
}

void TTBookTransferServer::failUpload(const char* reason) {
    if (_uploadActive) {
        closeTemp();
        _uploadActive = false;
    }
    _uploadError = reason;
    _imageStatusShown = false;
    LOG_W("Transfer: %s failed at %u bytes: %s, %u ms since start, heap=%u largest=%u",
          _uploadName.c_str(), (unsigned)_uploadBytes, reason, (unsigned)(millis() - _uploadStartMs),
          (unsigned)Util::heapFree(), (unsigned)Util::heapLargest());
    emit(TT_BOOK_TRANSFER_UPLOAD_FAILED, reason);
}

void TTBookTransferServer::closeTemp() {
    if (_file) {
        _file.close();
    }
    TTInstanceOf<TTSdCardService>().fs().remove(tempPath());
}

void TTBookTransferServer::handleUploadDone() {
    JsonDocument doc;
    int code = 200;
    if (!_uploadSeen) {
        doc["ok"] = false;
        doc["error"] = TT_TRANSFER_MSG_NO_FILE;
        code = 400;
    } else if (_uploadDone && _uploadError == nullptr) {
        doc["ok"] = true;
        doc["name"] = _uploadName;
        doc["size"] = _uploadBytes;
    } else {
        doc["ok"] = false;
        doc["name"] = _uploadName;
        doc["error"] = _uploadError != nullptr ? _uploadError : TT_TRANSFER_MSG_ABORTED;
        code = 400;
    }
    _uploadSeen = false;
    String body;
    serializeJson(doc, body);
    _server.send(code, "application/json; charset=utf-8", body);
}

void TTBookTransferServer::handleImagesDone() {
    const String name = _server.arg("name");
    JsonDocument doc;
    doc["name"] = name;
    int code = 200;
    const char* error = nameError(name);
    if (error == nullptr && !TTInstanceOf<TTSdCardService>().fs().exists(String(TT_BOOKS_DIR "/") + name)) {
        error = TT_TRANSFER_MSG_NOT_FOUND;
    }
    if (error == nullptr) {
        doc["ok"] = true;
        _imageStatusShown = false;
        LOG_I("Transfer: images done %s, %u book(s) received", name.c_str(), (unsigned)_received);
        if (_onEvent) {
            _onEvent(TT_BOOK_TRANSFER_RECEIVED, name.c_str(), 0, nullptr);
        }
    } else {
        doc["ok"] = false;
        doc["error"] = error;
        code = 400;
        LOG_W("Transfer: images done '%s' refused: %s", name.c_str(), error);
    }
    String body;
    serializeJson(doc, body);
    _server.send(code, "application/json; charset=utf-8", body);
}

void TTBookTransferServer::handleDelete() {
    const String name = _server.arg("name");
    JsonDocument doc;
    doc["name"] = name;
    int code = 200;
    const char* error = nameError(name);
    if (error == nullptr && !TTInstanceOf<TTSdCardService>().fs().exists(String(TT_BOOKS_DIR "/") + name)) {
        error = TT_TRANSFER_MSG_NOT_FOUND;
    }
    if (error == nullptr && !TTInstanceOf<TTBookStore>().removeBook(name)) {
        error = TT_TRANSFER_MSG_DELETE_FAILED;
    }
    if (error == nullptr) {
        doc["ok"] = true;
        LOG_I("Transfer: deleted %s from %s", name.c_str(), _server.client().remoteIP().toString().c_str());
        if (_onEvent) {
            _onEvent(TT_BOOK_TRANSFER_DELETED, name.c_str(), 0, nullptr);
        }
    } else {
        doc["ok"] = false;
        doc["error"] = error;
        code = 400;
        LOG_W("Transfer: delete '%s' refused: %s", name.c_str(), error);
    }
    String body;
    serializeJson(doc, body);
    _server.send(code, "application/json; charset=utf-8", body);
}

void TTBookTransferServer::handleBooks() {
    std::vector<TTBookEntry> books;
    TTInstanceOf<TTBookStore>().listBooks(books);
    JsonDocument doc;
    uint64_t totalBytes = 0;
    uint64_t freeBytes = 0;
    if (TTInstanceOf<TTSdCardService>().spaceInfo(totalBytes, freeBytes)) {
        doc["total"] = totalBytes;
        doc["free"] = freeBytes;
    }
    LOG_I("Transfer: list %u book(s), free %u / %u MB", (unsigned)books.size(),
          (unsigned)(freeBytes / (1024ULL * 1024ULL)), (unsigned)(totalBytes / (1024ULL * 1024ULL)));
    JsonArray list = doc["books"].to<JsonArray>();
    for (const TTBookEntry& book : books) {
        JsonObject item = list.add<JsonObject>();
        item["name"] = book.name;
        item["size"] = book.size;
    }
    String body;
    serializeJson(doc, body);
    _server.send(200, "application/json; charset=utf-8", body);
}

void TTBookTransferServer::handleRoot() {
    LOG_I("Transfer: page request from %s heap=%u largest=%u",
          _server.client().remoteIP().toString().c_str(),
          (unsigned)Util::heapFree(), (unsigned)Util::heapLargest());
    if (ttHttpSendFile(_server, TT_WWW_BOOKS_PAGE, "text/html; charset=utf-8")) {
        return;
    }
    LOG_E("Transfer: page missing %s", TT_WWW_BOOKS_PAGE);
    _server.send(503, "text/plain; charset=utf-8", "页面文件缺失");
}
