#include "TTBookTransferServer.h"
#include "TTSdCardService.h"
#include "../Base/Logger.h"
#include "../Base/TTFile.h"
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
    if (name.length() <= TT_BOOKS_EXT_LEN
        || strcasecmp(name.c_str() + name.length() - TT_BOOKS_EXT_LEN, TT_BOOKS_EXT) != 0) {
        return TT_TRANSFER_MSG_NOT_TXT;
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

    const char* error = nameError(name);
    if (error != nullptr) {
        LOG_W("Transfer: reject name '%s': %s", rawName.c_str(), error);
        _uploadError = error;
        emit(TT_BOOK_TRANSFER_UPLOAD_FAILED, error);
        return;
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
    emit(TT_BOOK_TRANSFER_RECEIVING, nullptr);
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
    const String target = String(TT_BOOKS_DIR "/") + _uploadName;
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
    _received++;
    const uint32_t elapsed = millis() - _uploadStartMs;
    LOG_I("Transfer: saved %s %u bytes in %u ms (%u KB/s)", target.c_str(), (unsigned)_uploadBytes,
          (unsigned)elapsed, (unsigned)(elapsed > 0 ? (uint64_t)_uploadBytes * 1000 / 1024 / elapsed : 0));
    emit(TT_BOOK_TRANSFER_RECEIVED, nullptr);
}

void TTBookTransferServer::failUpload(const char* reason) {
    if (_uploadActive) {
        closeTemp();
        _uploadActive = false;
    }
    _uploadError = reason;
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
    _server.send_P(200, "text/html; charset=utf-8", R"html(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>上传电子书</title>
<style>
body { font-family: -apple-system, sans-serif; margin: 0; padding: 16px; background: #f4f4f4; }
.box { max-width: 420px; margin: 0 auto; background: #fff; padding: 20px; border-radius: 8px; }
h2 { margin: 0 0 8px; text-align: center; }
h3 { margin: 20px 0 8px; font-size: 16px; }
.tip { color: #666; font-size: 13px; margin: 8px 0; line-height: 1.5; }
input[type=file] { width: 100%; margin: 12px 0; font-size: 15px; }
button { width: 100%; padding: 12px; font-size: 16px; background: #111; color: #fff; border: 0; border-radius: 6px; }
button:disabled { background: #999; }
ul { list-style: none; padding: 0; margin: 0; }
li { padding: 8px 0; border-bottom: 1px solid #eee; font-size: 14px; word-break: break-all; }
progress { width: 100%; height: 8px; margin: 6px 0 2px; }
.msg { color: #666; font-size: 13px; }
.size { color: #999; font-size: 12px; margin-left: 6px; }
.del { width: auto; float: right; padding: 2px 10px; font-size: 13px; background: #fff; color: #c00; border: 1px solid #c00; border-radius: 4px; margin-left: 8px; }
.space { color: #666; font-size: 13px; margin: 0 0 8px; }
</style>
</head>
<body>
<div class="box">
<h2>上传电子书</h2>
<p class="tip">只支持 UTF-8 编码的 TXT 文件，同名文件会被覆盖。上传完成前，请不要离开设备上的传书页面。</p>
<input type="file" id="file" multiple accept=".txt,text/plain">
<button id="go" onclick="start()">上传</button>
<ul id="jobs"></ul>
<h3>书架（<span id="count">-</span> 本）</h3>
<p class="space" id="space"></p>
<ul id="books"></ul>
</div>
<script>
const $ = id => document.getElementById(id);
let freeBytes = -1;
function fmt(n) {
  if (n >= 1073741824) return (n / 1073741824).toFixed(1) + ' GB';
  return n >= 1048576 ? (n / 1048576).toFixed(1) + ' MB' : Math.ceil(n / 1024) + ' KB';
}
function removeBook(name) {
  if (!confirm('确定删除《' + name.replace(/\.txt$/i, '') + '》吗？阅读进度也会一并删除。')) return;
  fetch('/delete?name=' + encodeURIComponent(name), { method: 'POST' })
    .then(r => r.json())
    .then(j => { if (!j.ok) alert('删除失败：' + (j.error || '未知错误')); })
    .catch(() => alert('删除失败：网络错误'))
    .then(loadBooks);
}
function row(list, name, size) {
  const li = document.createElement('li');
  li.textContent = name;
  const s = document.createElement('span');
  s.className = 'size';
  s.textContent = fmt(size);
  li.appendChild(s);
  list.appendChild(li);
  return li;
}
function loadBooks() {
  fetch('/books').then(r => r.json()).then(j => {
    const list = $('books');
    list.innerHTML = '';
    const books = j.books || [];
    books.forEach(b => {
      const li = row(list, b.name, b.size);
      const del = document.createElement('button');
      del.className = 'del';
      del.textContent = '删除';
      del.onclick = () => removeBook(b.name);
      li.prepend(del);
    });
    $('count').textContent = books.length;
    freeBytes = typeof j.free === 'number' ? j.free : -1;
    $('space').textContent = freeBytes >= 0 ? 'SD 卡剩余 ' + fmt(freeBytes) + ' / 共 ' + fmt(j.total) : '';
  }).catch(() => {});
}
function uploadOne(file, bar, msg) {
  return new Promise(done => {
    const x = new XMLHttpRequest();
    x.open('POST', '/upload?name=' + encodeURIComponent(file.name));
    x.upload.onprogress = e => { if (e.lengthComputable) bar.value = e.loaded / e.total; };
    x.onload = () => {
      let j = {};
      try { j = JSON.parse(x.responseText); } catch (e) {}
      msg.textContent = j.ok ? '完成' : '失败：' + (j.error || ('HTTP ' + x.status));
      bar.value = j.ok ? 1 : 0;
      done();
    };
    x.onerror = () => { msg.textContent = '失败：网络错误'; done(); };
    const form = new FormData();
    form.append('file', file, file.name);
    x.send(form);
  });
}
async function start() {
  const files = Array.from($('file').files);
  if (!files.length) { alert('请先选择文件'); return; }
  $('go').disabled = true;
  const jobs = $('jobs');
  jobs.innerHTML = '';
  for (const f of files) {
    const li = row(jobs, f.name, f.size);
    const bar = document.createElement('progress');
    bar.max = 1;
    bar.value = 0;
    const msg = document.createElement('div');
    msg.className = 'msg';
    li.append(bar, msg);
    if (!/\.txt$/i.test(f.name)) { msg.textContent = '跳过：只支持 TXT 文件'; continue; }
    if (freeBytes >= 0 && f.size > freeBytes) { msg.textContent = '跳过：SD 卡空间不足'; continue; }
    msg.textContent = '上传中...';
    await uploadOne(f, bar, msg);
    if (freeBytes >= 0 && /^完成/.test(msg.textContent)) freeBytes -= f.size;
  }
  $('go').disabled = false;
  $('file').value = '';
  loadBooks();
}
loadBooks();
</script>
</body>
</html>
)html");
}
