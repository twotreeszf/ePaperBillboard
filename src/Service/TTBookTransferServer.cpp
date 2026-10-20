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
    }
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
body { font-family: -apple-system, sans-serif; margin: 0; padding: 16px; background: #f4f4f4; min-height: 100vh; box-sizing: border-box; }
.mask { display: none; position: fixed; inset: 8px; border: 3px dashed #111; border-radius: 12px; background: rgba(255,255,255,.88); z-index: 2; align-items: center; justify-content: center; font-size: 20px; pointer-events: none; }
body.drag .mask { display: flex; }
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
<div class="mask">松开以上传</div>
<div class="box">
<h2>上传电子书</h2>
<p class="tip">把 TXT、Markdown（.md）或 EPUB 拖到页面任意位置，或点选后按上传。EPUB 会在浏览器里转成 Markdown，图片转成整页线稿后跟着正文上传，大书需要几秒。同名文件会被覆盖。TXT 和 Markdown 请使用 UTF-8 编码。上传完成前，请不要离开设备上的传书页面。</p>
<input type="file" id="file" multiple accept=".txt,.md,.epub,text/plain,text/markdown,application/epub+zip">
<button id="go" onclick="start()">上传</button>
<ul id="jobs"></ul>
<h3>书架（<span id="count">-</span> 本）</h3>
<p class="space" id="space"></p>
<ul id="books"></ul>
</div>
<script>
(function (root) {
  const NAME_MAX = 128;
  const HEADING_MAX_CHARS = 80;
  const HEADING_SCALE = 1.05;
  const ROOT_FONT_PX = 16;
  const PAGE_W = 400;
  const PAGE_H = 282;
  const IMAGE_MAX = 300;
  const EDGE_LIMIT = 80;
  const IMAGE_MARK = "\u0002";
  const RASTER_MAX = 1600;

  let styleRules = [];
  let pendingImages = [];
  let chapterHref = "";
  let declaredCache = new WeakMap();
  let scaleCache = new WeakMap();

  function tagName(node) {
    const name = node && node.localName ? node.localName : "";
    const cut = name.lastIndexOf(":");
    return cut >= 0 ? name.slice(cut + 1) : name;
  }

  const BLOCK = {
    address: 1, article: 1, aside: 1, blockquote: 1, div: 1, figcaption: 1, figure: 1,
    footer: 1, h1: 1, h2: 1, h3: 1, h4: 1, h5: 1, h6: 1, header: 1, hr: 1, li: 1,
    ol: 1, p: 1, pre: 1, section: 1, table: 1, ul: 1
  };

  function u16(u8, offset) {
    return u8[offset] | (u8[offset + 1] << 8);
  }

  function u32(u8, offset) {
    return (u8[offset] | (u8[offset + 1] << 8) | (u8[offset + 2] << 16) | (u8[offset + 3] << 24)) >>> 0;
  }

  function findEocd(u8) {
    const min = Math.max(0, u8.length - 22 - 65535);
    for (let i = u8.length - 22; i >= min; i--) {
      if (u8[i] === 0x50 && u8[i + 1] === 0x4b && u8[i + 2] === 0x05 && u8[i + 3] === 0x06) {
        return i;
      }
    }
    throw new Error("不是 EPUB 文件");
  }

  function listZip(u8) {
    const eocd = findEocd(u8);
    if (u16(u8, eocd + 10) === 0xffff || u32(u8, eocd + 16) === 0xffffffff) {
      throw new Error("这个 EPUB 太大，暂不支持");
    }
    const count = u16(u8, eocd + 10);
    let pos = u32(u8, eocd + 16);
    const entries = new Map();
    for (let i = 0; i < count; i++) {
      if (u32(u8, pos) !== 0x02014b50) {
        throw new Error("EPUB 目录损坏");
      }
      const method = u16(u8, pos + 10);
      const compSize = u32(u8, pos + 20);
      const nameLen = u16(u8, pos + 28);
      const extraLen = u16(u8, pos + 30);
      const commentLen = u16(u8, pos + 32);
      const local = u32(u8, pos + 42);
      const name = new TextDecoder("utf-8").decode(u8.subarray(pos + 46, pos + 46 + nameLen)).replace(/\\/g, "/");
      entries.set(name, { method: method, compSize: compSize, local: local });
      pos += 46 + nameLen + extraLen + commentLen;
    }
    return entries;
  }

  async function inflateRaw(bytes) {
    if (typeof DecompressionStream !== "function") {
      throw new Error("当前浏览器无法解压 EPUB，请换用 Safari 或 Chrome");
    }
    const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream("deflate-raw"));
    return new Uint8Array(await new Response(stream).arrayBuffer());
  }

  async function readEntry(u8, entry) {
    const nameLen = u16(u8, entry.local + 26);
    const extraLen = u16(u8, entry.local + 28);
    const start = entry.local + 30 + nameLen + extraLen;
    const data = u8.subarray(start, start + entry.compSize);
    if (entry.method === 0) {
      return data;
    }
    if (entry.method === 8) {
      return inflateRaw(data);
    }
    throw new Error("EPUB 使用了不支持的压缩方式");
  }

  function decodeText(bytes) {
    const head = new TextDecoder("utf-8", { fatal: false }).decode(bytes.subarray(0, 240));
    const marked = head.match(/encoding\s*=\s*["']([^"']+)["']/i) || head.match(/charset\s*=\s*["']?([^"';\s]+)/i);
    const name = marked ? marked[1].toLowerCase() : "utf-8";
    const label = name === "gbk" || name === "gb2312" ? "gb18030" : name;
    try {
      return new TextDecoder(label).decode(bytes);
    } catch (e) {
      return new TextDecoder("utf-8").decode(bytes);
    }
  }

  function parseDocument(text, html) {
    const doc = new DOMParser().parseFromString(text, html ? "text/html" : "text/xml");
    const root = doc.documentElement;
    if (!root || tagName(root) === "parsererror") {
      throw new Error("EPUB 里有无法解析的文档");
    }
    return doc;
  }

  function elements(node, name) {
    const found = [];
    const walk = function (current) {
      const kids = current.children || [];
      for (let i = 0; i < kids.length; i++) {
        if (tagName(kids[i]) === name) {
          found.push(kids[i]);
        }
        walk(kids[i]);
      }
    };
    walk(node);
    return found;
  }

  function lookup(entries, path) {
    if (entries.has(path)) {
      return entries.get(path);
    }
    const lower = path.toLowerCase();
    for (const key of entries.keys()) {
      if (key.toLowerCase() === lower) {
        return entries.get(key);
      }
    }
    return null;
  }

  function joinPath(dir, href) {
    const clean = String(href || "").split("#")[0].split("?")[0];
    const parts = (dir ? dir + "/" + clean : clean).split("/");
    const out = [];
    for (let i = 0; i < parts.length; i++) {
      let part = parts[i];
      try {
        part = decodeURIComponent(part);
      } catch (e) {
        part = parts[i];
      }
      if (!part || part === ".") {
        continue;
      }
      if (part === "..") {
        out.pop();
      } else {
        out.push(part);
      }
    }
    return out.join("/");
  }

  function dirName(path) {
    const cut = path.lastIndexOf("/");
    return cut >= 0 ? path.slice(0, cut) : "";
  }

  function stripAtRules(text) {
    const out = [];
    let i = 0;
    while (i < text.length) {
      if (text.charAt(i) === "@") {
        const semi = text.indexOf(";", i);
        const brace = text.indexOf("{", i);
        if (brace < 0 || (semi >= 0 && semi < brace)) {
          i = semi < 0 ? text.length : semi + 1;
          continue;
        }
        let depth = 0;
        let j = brace;
        for (; j < text.length; j++) {
          if (text.charAt(j) === "{") {
            depth++;
          } else if (text.charAt(j) === "}" && --depth === 0) {
            j++;
            break;
          }
        }
        i = j;
        continue;
      }
      out.push(text.charAt(i));
      i++;
    }
    return out.join("");
  }

  function parseFontShorthand(value) {
    const result = {};
    const tokens = value.split(/\s+/);
    for (let i = 0; i < tokens.length; i++) {
      let token = tokens[i];
      const slash = token.indexOf("/");
      if (slash >= 0) {
        token = token.slice(0, slash);
      }
      if (!token) {
        continue;
      }
      if (token === "italic" || token === "oblique") {
        result.style = token;
      } else if (token === "bold" || token === "bolder" || /^[1-9]00$/.test(token)) {
        result.weight = token;
      } else if (/^([0-9.]+)(em|rem|px|%)$/.test(token) || /^(xx-small|x-small|small|medium|large|x-large|xx-large|xxx-large|larger|smaller)$/.test(token)) {
        result.size = token;
      }
    }
    return result;
  }

  function parseDeclarations(body) {
    const style = {};
    const parts = body.split(";");
    for (let i = 0; i < parts.length; i++) {
      const cut = parts[i].indexOf(":");
      if (cut < 0) {
        continue;
      }
      const key = parts[i].slice(0, cut).trim().toLowerCase();
      const val = parts[i].slice(cut + 1).trim().toLowerCase().replace(/\s*!important\s*$/, "");
      if (key === "font") {
        const parsed = parseFontShorthand(val);
        if (parsed.size) {
          style["font-size"] = parsed.size;
        }
        if (parsed.weight) {
          style["font-weight"] = parsed.weight;
        }
        if (parsed.style) {
          style["font-style"] = parsed.style;
        }
        continue;
      }
      if (key === "font-size" || key === "font-weight" || key === "font-style" || key === "text-decoration" || key === "text-decoration-line") {
        style[key] = val;
      }
    }
    return style;
  }

  function parseCss(text) {
    const rules = [];
    const src = stripAtRules(String(text || "").replace(/\/\*[\s\S]*?\*\//g, ""));
    const re = /([^{}]+)\{([^{}]*)\}/g;
    let match;
    while ((match = re.exec(src))) {
      const decls = parseDeclarations(match[2]);
      if (!decls["font-size"] && !decls["font-weight"] && !decls["font-style"] && !decls["text-decoration"] && !decls["text-decoration-line"]) {
        continue;
      }
      const selectors = match[1].split(",");
      for (let i = 0; i < selectors.length; i++) {
        const sel = selectors[i].trim().toLowerCase();
        if (!sel || /[\s>+~\[]|:/.test(sel)) {
          continue;
        }
        rules.push({ sel: sel, decls: decls });
      }
    }
    return rules;
  }

  function classSet(el) {
    const raw = el.getAttribute ? el.getAttribute("class") || "" : "";
    const set = {};
    const names = raw.split(/\s+/);
    for (let i = 0; i < names.length; i++) {
      if (names[i]) {
        set[names[i].toLowerCase()] = 1;
      }
    }
    return set;
  }

  function selectorMatches(sel, el) {
    let body = sel;
    if (body.charAt(0) !== ".") {
      const dot = body.indexOf(".");
      const want = dot >= 0 ? body.slice(0, dot) : body;
      if (want !== tagName(el)) {
        return false;
      }
      body = dot >= 0 ? body.slice(dot) : "";
    }
    if (!body) {
      return true;
    }
    const classes = classSet(el);
    const parts = body.split(".");
    let seen = 0;
    for (let i = 0; i < parts.length; i++) {
      if (!parts[i]) {
        continue;
      }
      seen++;
      if (!classes[parts[i]]) {
        return false;
      }
    }
    return seen > 0;
  }

  function declaredStyle(el) {
    if (declaredCache.has(el)) {
      return declaredCache.get(el);
    }
    const style = {};
    for (let i = 0; i < styleRules.length; i++) {
      if (selectorMatches(styleRules[i].sel, el)) {
        const decls = styleRules[i].decls;
        for (const key in decls) {
          style[key] = decls[key];
        }
      }
    }
    const inline = parseDeclarations(el.getAttribute ? el.getAttribute("style") || "" : "");
    for (const key in inline) {
      style[key] = inline[key];
    }
    declaredCache.set(el, style);
    return style;
  }

  function applySize(value, parentScale) {
    const keywords = {
      "xx-small": 0.6, "x-small": 0.75, small: 0.85, medium: 1,
      large: 1.2, "x-large": 1.5, "xx-large": 2, "xxx-large": 3
    };
    const v = String(value || "").trim();
    const sized = /^([0-9.]+)(em|rem|px|%)$/.exec(v);
    if (sized) {
      const n = parseFloat(sized[1]);
      if (sized[2] === "em" || sized[2] === "%") {
        return parentScale * (sized[2] === "%" ? n / 100 : n);
      }
      if (sized[2] === "rem") {
        return n;
      }
      return n / ROOT_FONT_PX;
    }
    if (keywords[v]) {
      return keywords[v];
    }
    if (v === "larger") {
      return parentScale * 1.2;
    }
    if (v === "smaller") {
      return parentScale / 1.2;
    }
    return parentScale;
  }

  function scaleOf(el) {
    if (!el || el.nodeType !== 1) {
      return 1;
    }
    if (scaleCache.has(el)) {
      return scaleCache.get(el);
    }
    const parent = el.parentElement;
    const base = parent ? scaleOf(parent) : 1;
    const size = declaredStyle(el)["font-size"];
    const scale = size && size !== "inherit" ? applySize(size, base) : base;
    scaleCache.set(el, scale);
    return scale;
  }

  function weightOn(own, parent) {
    const weight = own["font-weight"];
    if (!weight || weight === "inherit") {
      return parent;
    }
    if (weight === "normal" || weight === "lighter") {
      return false;
    }
    if (weight === "bold" || weight === "bolder") {
      return true;
    }
    return parseInt(weight, 10) >= 600;
  }

  function italicOn(own, parent) {
    const style = own["font-style"];
    if (!style || style === "inherit") {
      return parent;
    }
    if (style === "normal") {
      return false;
    }
    return style === "italic" || style === "oblique";
  }

  function decorationOf(own) {
    return own["text-decoration-line"] || own["text-decoration"] || "";
  }

  function decoOn(own, parent, name) {
    const raw = decorationOf(own);
    if (!raw || raw === "inherit") {
      return parent;
    }
    if (raw.indexOf("none") >= 0) {
      return false;
    }
    return raw.indexOf(name) >= 0;
  }

  function textScale(el) {
    let min = 0;
    const walk = function (node) {
      if (node.nodeType === 3) {
        if (!String(node.nodeValue || "").trim()) {
          return;
        }
        const scale = scaleOf(node.parentElement || el);
        min = min === 0 ? scale : Math.min(min, scale);
        return;
      }
      if (node.nodeType !== 1) {
        return;
      }
      const tag = tagName(node);
      if (tag === "script" || tag === "style" || tag === "rt" || tag === "rp") {
        return;
      }
      const kids = node.childNodes;
      for (let i = 0; i < kids.length; i++) {
        walk(kids[i]);
      }
    };
    walk(el);
    return min;
  }

  function implicitHeading(el) {
    const tag = tagName(el);
    if (tag === "blockquote" || tag === "pre" || tag === "table" || tag === "ul" || tag === "ol" || tag === "li") {
      return 0;
    }
    if (hasBlockChild(el) || elements(el, "img").length || elements(el, "image").length) {
      return 0;
    }
    const text = String(el.textContent || "").replace(/\s+/g, " ").trim();
    if (!text || text.length > HEADING_MAX_CHARS) {
      return 0;
    }
    const scale = textScale(el);
    if (scale >= 1.7) {
      return 1;
    }
    if (scale >= 1.4) {
      return 2;
    }
    if (scale >= 1.2) {
      return 3;
    }
    if (scale > HEADING_SCALE) {
      return 4;
    }
    return 0;
  }

  async function rulesFor(u8, entries, doc, href) {
    const chunks = [];
    const styles = elements(doc, "style");
    for (let i = 0; i < styles.length; i++) {
      chunks.push(styles[i].textContent || "");
    }
    const links = elements(doc, "link");
    for (let i = 0; i < links.length; i++) {
      const rel = (links[i].getAttribute("rel") || "").toLowerCase();
      const type = (links[i].getAttribute("type") || "").toLowerCase();
      if (rel.indexOf("stylesheet") < 0 && type.indexOf("css") < 0) {
        continue;
      }
      const path = joinPath(dirName(href), links[i].getAttribute("href") || "");
      const item = lookup(entries, path);
      if (!item) {
        continue;
      }
      chunks.push(decodeText(await readEntry(u8, item)));
    }
    return parseCss(chunks.join("\n"));
  }

  function sourceName(filename) {
    return String(filename || "").replace(/^.*[\\/]/, "").replace(/\.epub$/i, "");
  }

  function fitName(title) {
    let name = String(title || "").replace(/[\\/:*?"<>|\u0000-\u001f]/g, " ").replace(/\s+/g, " ").trim();
    if (!name || name === ".") {
      name = "未命名";
    }
    const ext = ".md";
    const encoded = new TextEncoder().encode(name);
    const max = NAME_MAX - ext.length;
    if (encoded.length <= max) {
      return name + ext;
    }
    let end = max;
    while (end > 0 && (encoded[end - 1] & 0xc0) === 0x80) {
      end--;
    }
    return new TextDecoder().decode(encoded.subarray(0, end)).replace(/\s+$/, "") + ext;
  }

  function inlineNodes(nodes, ctx) {
    const parent = ctx || { bold: false, italic: false, under: false, strike: false };
    let text = "";
    for (let i = 0; i < nodes.length; i++) {
      text += inlineNode(nodes[i], parent);
    }
    return text;
  }

  function inlineNode(node, ctx) {
    const parent = ctx || { bold: false, italic: false, under: false, strike: false };
    if (node.nodeType === 3) {
      return node.nodeValue.replace(/[ \t\r\n\f]+/g, " ");
    }
    if (node.nodeType !== 1) {
      return "";
    }
    const tag = tagName(node);
    if (tag === "script" || tag === "style" || tag === "rt" || tag === "rp") {
      return "";
    }
    if (tag === "br") {
      return "\n";
    }
    if (tag === "img" || tag === "image") {
      return imageInline(node.getAttribute("src") || node.getAttribute("href") || "");
    }
    const own = declaredStyle(node);
    const next = {
      bold: tag === "strong" || tag === "b" || weightOn(own, parent.bold),
      italic: tag === "em" || tag === "i" || italicOn(own, parent.italic),
      under: tag === "u" || decoOn(own, parent.under, "underline"),
      strike: tag === "del" || tag === "s" || tag === "strike" || decoOn(own, parent.strike, "line-through")
    };
    if (tag === "code") {
      const body = inlineNodes(node.childNodes, next).replace(/`/g, "").trim();
      return body ? "`" + body + "`" : "";
    }
    let text = inlineNodes(node.childNodes, next);
    if (tag === "a") {
      const href = node.getAttribute("href") || "";
      const body = text.trim();
      if (!body) {
        return "";
      }
      if (!href || href.charAt(0) === "#") {
        return body;
      }
      return "[" + body + "](" + href.replace(/\)/g, "%29") + ")";
    }
    if (next.strike && !parent.strike) {
      text = wrapMark(text, "~~");
    }
    if (next.italic && !parent.italic) {
      text = wrapMark(text, "*");
    }
    if (next.bold && !parent.bold) {
      text = wrapMark(text, "**");
    }
    if (next.under && !parent.under) {
      const body = text.trim();
      text = body ? "<u>" + body + "</u>" : "";
    }
    return text;
  }

  function wrapMark(text, mark) {
    const body = text.trim();
    if (!body) {
      return "";
    }
    return mark + body + mark;
  }

  function escapeLine(line) {
    if (/^(#{1,6}[ \t]|>|[-*+][ \t]|\d+[.)][ \t])/.test(line) || /^([-*_])(\s*\1){2,}$/.test(line)) {
      return "\\" + line;
    }
    return line;
  }

  function markedText(el, options) {
    const own = declaredStyle(el);
    const ctx = {
      bold: !!(options && options.bold) || weightOn(own, false),
      italic: italicOn(own, false),
      under: decoOn(own, false, "underline"),
      strike: decoOn(own, false, "line-through")
    };
    let text = inlineNodes(el.childNodes, ctx);
    text = text.replace(/[ \t]*\n[ \t]*/g, options && options.flatten ? " " : "\n").replace(/ {2,}/g, " ").trim();
    if (ctx.strike && text) {
      text = "~~" + text + "~~";
    }
    if (ctx.italic && text) {
      text = "*" + text + "*";
    }
    if (!(options && options.skipBold) && ctx.bold && text) {
      text = "**" + text + "**";
    }
    if (ctx.under && text) {
      text = "<u>" + text + "</u>";
    }
    return text;
  }

  function paragraphText(el) {
    return markedText(el);
  }

  function renderParagraph(el) {
    const level = implicitHeading(el);
    if (!level) {
      return emitParagraph(paragraphText(el));
    }
    const text = markedText(el, { bold: true, skipBold: true, flatten: true });
    return text ? "#".repeat(level) + " " + escapeLine(text) + "\n\n" : "";
  }

  function emitParagraph(text) {
    const parts = text.split("\n").map(function (line) { return line.trim(); }).filter(Boolean);
    if (!parts.length) {
      return "";
    }
    return parts.map(escapeLine).join("\n\n") + "\n\n";
  }

  function renderChildren(el) {
    let out = "";
    const nodes = el.childNodes;
    for (let i = 0; i < nodes.length; i++) {
      const node = nodes[i];
      if (node.nodeType === 3) {
        const text = node.nodeValue.replace(/\s+/g, " ").trim();
        if (text) {
          out += emitParagraph(text);
        }
      } else if (node.nodeType === 1) {
        out += renderBlock(node);
      }
    }
    return out;
  }

  function renderList(el, ordered, depth) {
    const pad = "  ".repeat(Math.min(depth, 3));
    let index = 1;
    let out = "";
    const kids = el.children || [];
    for (let i = 0; i < kids.length; i++) {
      if (tagName(kids[i]) !== "li") {
        continue;
      }
      const marker = taskMarker(kids[i]) || (ordered ? String(index++) + ". " : "- ");
      const body = renderChildren(kids[i]).trim().replace(/\n{2,}/g, "\n");
      const lines = body ? body.split("\n") : [""];
      out += pad + marker + lines[0] + "\n";
      for (let n = 1; n < lines.length; n++) {
        out += lines[n] ? pad + "  " + lines[n] + "\n" : "\n";
      }
    }
    return out ? out + "\n" : "";
  }

  function taskMarker(li) {
    const kids = li.children || [];
    for (let i = 0; i < kids.length; i++) {
      if (tagName(kids[i]) !== "input") {
        continue;
      }
      const type = (kids[i].getAttribute("type") || "").toLowerCase();
      if (type !== "checkbox") {
        return "";
      }
      const checked = kids[i].hasAttribute("checked") || kids[i].getAttribute("checked") === "checked";
      return checked ? "- [x] " : "- [ ] ";
    }
    return "";
  }

  function renderTable(el) {
    const rows = elements(el, "tr").map(function (tr) {
      const cells = [];
      const kids = tr.children || [];
      for (let i = 0; i < kids.length; i++) {
        if (tagName(kids[i]) === "td" || tagName(kids[i]) === "th") {
          cells.push(paragraphText(kids[i]).replace(/\n/g, " ").replace(/\|/g, "\\|"));
        }
      }
      return cells;
    }).filter(function (row) { return row.length > 0; });
    if (!rows.length) {
      return "";
    }
    const cols = rows.reduce(function (max, row) { return Math.max(max, row.length); }, 0);
    const line = function (row) {
      const copy = row.slice();
      while (copy.length < cols) {
        copy.push("");
      }
      return "| " + copy.join(" | ") + " |";
    };
    let out = line(rows[0]) + "\n| " + Array(cols).fill("---").join(" | ") + " |\n";
    for (let i = 1; i < rows.length; i++) {
      out += line(rows[i]) + "\n";
    }
    return out + "\n";
  }

  function renderBlock(el) {
    const tag = tagName(el);
    if (!tag || tag === "script" || tag === "style" || tag === "head" || tag === "rt" || tag === "rp") {
      return "";
    }
    if (tag === "hr") {
      return "---\n\n";
    }
    if (/^h[1-6]$/.test(tag)) {
      const text = paragraphText(el).replace(/\n/g, " ");
      return text ? "#".repeat(Number(tag.charAt(1))) + " " + escapeLine(text) + "\n\n" : "";
    }
    if (tag === "p" || tag === "figcaption") {
      return renderParagraph(el);
    }
    if (tag === "pre") {
      const text = (el.textContent || "").replace(/\n$/, "");
      if (!text.trim()) {
        return "";
      }
      const fence = text.indexOf("```") >= 0 ? "~~~~" : "```";
      return fence + "\n" + text + "\n" + fence + "\n\n";
    }
    if (tag === "blockquote") {
      const inner = renderChildren(el).trim();
      if (!inner) {
        return "";
      }
      return inner.split("\n").map(function (line) {
        return line.trim() ? "> " + line : "";
      }).join("\n") + "\n\n";
    }
    if (tag === "ul" || tag === "ol") {
      return renderList(el, tag === "ol", 0);
    }
    if (tag === "table") {
      return renderTable(el);
    }
    if (tag === "li") {
      return renderList({ children: [el] }, false, 0);
    }
    if (tag === "img" || tag === "image") {
      return imageBlock(joinPath(dirName(chapterHref), el.getAttribute("src") || el.getAttribute("href") || ""));
    }
    if (!el.children || el.children.length === 0 || !hasBlockChild(el)) {
      return renderParagraph(el);
    }
    return renderChildren(el);
  }

  function hasBlockChild(el) {
    const kids = el.children || [];
    for (let i = 0; i < kids.length; i++) {
      if (BLOCK[tagName(kids[i])] || hasBlockChild(kids[i])) {
        return true;
      }
    }
    return false;
  }

  function tocLines(items, depth) {
    let out = "";
    for (let i = 0; i < items.length; i++) {
      out += "  ".repeat(Math.min(depth, 3)) + "- " + items[i].label + "\n";
      out += tocLines(items[i].children, depth + 1);
    }
    return out;
  }

  function readNav(doc) {
    const navs = elements(doc, "nav");
    let nav = null;
    for (let i = 0; i < navs.length; i++) {
      const type = (navs[i].getAttribute("epub:type") || "") + " " + (navs[i].getAttribute("role") || "");
      if (type.indexOf("toc") >= 0 || navs.length === 1) {
        nav = navs[i];
        break;
      }
    }
    if (!nav) {
      return [];
    }
    const lists = [];
    const kids = nav.children || [];
    for (let i = 0; i < kids.length; i++) {
      if (tagName(kids[i]) === "ol" || tagName(kids[i]) === "ul") {
        lists.push(kids[i]);
      }
    }
    return lists.length ? readNavList(lists[0]) : [];
  }

  function readNavList(list) {
    const items = [];
    const kids = list.children || [];
    for (let i = 0; i < kids.length; i++) {
      if (tagName(kids[i]) !== "li") {
        continue;
      }
      const link = elements(kids[i], "a")[0];
      const label = link ? paragraphText(link).replace(/\n/g, " ") : "";
      const href = link ? link.getAttribute("href") || "" : "";
      const nested = [];
      const inner = kids[i].children || [];
      for (let n = 0; n < inner.length; n++) {
        if (tagName(inner[n]) === "ol" || tagName(inner[n]) === "ul") {
          nested.push.apply(nested, readNavList(inner[n]));
        }
      }
      if (label) {
        items.push({ label: label, href: href, children: nested });
      }
    }
    return items;
  }

  function readNcx(doc) {
    return elements(doc, "navPoint").filter(function (point) {
      return point.parentNode && tagName(point.parentNode) !== "navPoint";
    }).map(readNcxPoint);
  }

  function readNcxPoint(point) {
    const labelNode = elements(point, "text")[0];
    const srcNode = elements(point, "content")[0];
    const children = [];
    const kids = point.children || [];
    for (let i = 0; i < kids.length; i++) {
      if (tagName(kids[i]) === "navPoint") {
        children.push(readNcxPoint(kids[i]));
      }
    }
    return {
      label: labelNode ? paragraphText(labelNode).replace(/\n/g, " ") : "",
      href: srcNode ? srcNode.getAttribute("src") || "" : "",
      children: children
    };
  }

  function noteImage(path, mime) {
    if (!path || pendingImages.length >= IMAGE_MAX) {
      return "";
    }
    for (let i = 0; i < pendingImages.length; i++) {
      if (pendingImages[i].path === path) {
        return IMAGE_MARK + i + IMAGE_MARK;
      }
    }
    const index = pendingImages.length;
    pendingImages.push({ path: path, mime: mime || "" });
    return IMAGE_MARK + index + IMAGE_MARK;
  }

  function imageInline(src) {
    if (!src) {
      return "〔图〕";
    }
    const token = noteImage(joinPath(dirName(chapterHref), src), "");
    return token ? "\n" + token + "\n" : "〔图〕";
  }

  function imageBlock(path, mime) {
    const token = noteImage(path, mime);
    return (token || "〔图〕") + "\n\n";
  }

  function guessMime(path) {
    const lower = String(path || "").toLowerCase();
    if (lower.endsWith(".png")) {
      return "image/png";
    }
    if (lower.endsWith(".gif")) {
      return "image/gif";
    }
    if (lower.endsWith(".webp")) {
      return "image/webp";
    }
    if (lower.endsWith(".svg")) {
      return "image/svg+xml";
    }
    return "image/jpeg";
  }

  function edgeMask(gray, width, height) {
    const mask = new Uint8Array(width * height);
    const limit = EDGE_LIMIT * EDGE_LIMIT;
    for (let y = 1; y < height - 1; y++) {
      for (let x = 1; x < width - 1; x++) {
        const i = y * width + x;
        const gx = -gray[i - width - 1] + gray[i - width + 1]
          - 2 * gray[i - 1] + 2 * gray[i + 1]
          - gray[i + width - 1] + gray[i + width + 1];
        const gy = -gray[i - width - 1] - 2 * gray[i - width] - gray[i - width + 1]
          + gray[i + width - 1] + 2 * gray[i + width] + gray[i + width + 1];
        if (gx * gx + gy * gy > limit) {
          mask[i] = 1;
        }
      }
    }
    return mask;
  }

  function packPage(mask, srcW, srcH) {
    const scale = Math.min(PAGE_W / srcW, PAGE_H / srcH);
    let destW = Math.round(srcW * scale);
    let destH = Math.round(srcH * scale);
    if (destW < 1) {
      destW = 1;
    }
    if (destH < 1) {
      destH = 1;
    }
    if (destW > PAGE_W) {
      destW = PAGE_W;
    }
    if (destH > PAGE_H) {
      destH = PAGE_H;
    }
    const ox = (PAGE_W - destW) >> 1;
    const oy = (PAGE_H - destH) >> 1;
    const stride = (PAGE_W + 7) >> 3;
    const data = new Uint8Array(8 + stride * PAGE_H);
    data.fill(0xff);
    data[0] = 0x54;
    data[1] = 0x54;
    data[2] = 0x49;
    data[3] = 0x31;
    data[4] = PAGE_W & 0xff;
    data[5] = (PAGE_W >> 8) & 0xff;
    data[6] = PAGE_H & 0xff;
    data[7] = (PAGE_H >> 8) & 0xff;
    for (let y = 0; y < destH; y++) {
      const y0 = Math.floor(y * srcH / destH);
      const y1 = Math.max(y0 + 1, Math.floor((y + 1) * srcH / destH));
      for (let x = 0; x < destW; x++) {
        const x0 = Math.floor(x * srcW / destW);
        const x1 = Math.max(x0 + 1, Math.floor((x + 1) * srcW / destW));
        let ink = 0;
        for (let sy = y0; sy < y1 && !ink; sy++) {
          for (let sx = x0; sx < x1; sx++) {
            if (mask[sy * srcW + sx]) {
              ink = 1;
              break;
            }
          }
        }
        if (!ink) {
          continue;
        }
        const px = ox + x;
        const py = oy + y;
        const offset = 8 + py * stride + (px >> 3);
        data[offset] &= ~(1 << (7 - (px & 7)));
      }
    }
    return data;
  }

  async function rasterOf(bytes, mime) {
    if (typeof root.decodeEpubImage === "function") {
      return root.decodeEpubImage(bytes, mime);
    }
    if (typeof createImageBitmap !== "function" || typeof document === "undefined") {
      throw new Error("当前浏览器无法处理图片");
    }
    const blob = new Blob([bytes], { type: mime || "image/jpeg" });
    const bitmap = await createImageBitmap(blob);
    let width = bitmap.width;
    let height = bitmap.height;
    const longEdge = Math.max(width, height);
    if (longEdge > RASTER_MAX) {
      const scale = RASTER_MAX / longEdge;
      width = Math.max(1, Math.round(width * scale));
      height = Math.max(1, Math.round(height * scale));
    }
    const canvas = document.createElement("canvas");
    canvas.width = width;
    canvas.height = height;
    const ctx = canvas.getContext("2d", { willReadFrequently: true });
    ctx.fillStyle = "#fff";
    ctx.fillRect(0, 0, width, height);
    ctx.drawImage(bitmap, 0, 0, width, height);
    if (bitmap.close) {
      bitmap.close();
    }
    const rgba = ctx.getImageData(0, 0, width, height).data;
    const gray = new Uint8Array(width * height);
    for (let i = 0; i < gray.length; i++) {
      const o = i * 4;
      const alpha = rgba[o + 3] / 255;
      const lum = (rgba[o] * 77 + rgba[o + 1] * 150 + rgba[o + 2] * 29) >> 8;
      gray[i] = 255 - Math.round((255 - lum) * alpha);
    }
    return { width: width, height: height, gray: gray };
  }

  async function attachImages(u8, entries, text) {
    const ready = [];
    for (let i = 0; i < pendingImages.length; i++) {
      const token = IMAGE_MARK + i + IMAGE_MARK;
      const item = pendingImages[i];
      let packed = null;
      try {
        const entry = lookup(entries, item.path);
        if (!entry) {
          throw new Error("missing");
        }
        const raster = await rasterOf(await readEntry(u8, entry), item.mime || guessMime(item.path));
        if (!raster || !raster.gray || raster.width < 2 || raster.height < 2) {
          throw new Error("empty");
        }
        packed = packPage(edgeMask(raster.gray, raster.width, raster.height), raster.width, raster.height);
      } catch (e) {
        packed = null;
      }
      if (packed) {
        const name = String(ready.length + 1).padStart(4, "0") + ".i1";
        ready.push({ name: name, bytes: packed });
        text = text.split(token).join("![](" + name + ")");
      } else {
        text = text.split(token).join("〔图〕");
      }
    }
    return { text: text, images: ready };
  }

  async function epubToMarkdown(buffer, filename) {
    pendingImages = [];
    chapterHref = "";
    const u8 = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
    const entries = listZip(u8);
    const containerEntry = lookup(entries, "META-INF/container.xml");
    if (!containerEntry) {
      throw new Error("找不到 EPUB 的目录");
    }
    const container = parseDocument(decodeText(await readEntry(u8, containerEntry)), false);
    const rootfile = elements(container, "rootfile")[0];
    const opfPath = rootfile ? rootfile.getAttribute("full-path") || "" : "";
    if (!opfPath) {
      throw new Error("找不到 EPUB 的目录");
    }
    const opfEntry = lookup(entries, opfPath);
    if (!opfEntry) {
      throw new Error("找不到 EPUB 的目录");
    }
    const opf = parseDocument(decodeText(await readEntry(u8, opfEntry)), false);
    const opfDir = dirName(opfPath);
    const titleNode = elements(opf, "title")[0];
    const title = titleNode ? paragraphText(titleNode).replace(/\n/g, " ") : "";
    const manifest = new Map();
    elements(opf, "item").forEach(function (item) {
      const id = item.getAttribute("id");
      if (!id) {
        return;
      }
      manifest.set(id, {
        href: joinPath(opfDir, item.getAttribute("href") || ""),
        type: item.getAttribute("media-type") || "",
        props: item.getAttribute("properties") || ""
      });
    });
    let navHref = "";
    let ncxHref = "";
    manifest.forEach(function (item) {
      if (item.props.split(/\s+/).indexOf("nav") >= 0) {
        navHref = item.href;
      }
      if (item.type.indexOf("dtbncx") >= 0) {
        ncxHref = item.href;
      }
    });
    const spineNode = elements(opf, "spine")[0];
    if (spineNode && !ncxHref) {
      const tocId = spineNode.getAttribute("toc");
      if (tocId && manifest.has(tocId)) {
        ncxHref = manifest.get(tocId).href;
      }
    }
    const skip = new Set();
    if (navHref) {
      skip.add(navHref);
    }
    if (ncxHref) {
      skip.add(ncxHref);
    }
    let toc = [];
    if (navHref && lookup(entries, navHref)) {
      toc = readNav(parseDocument(decodeText(await readEntry(u8, lookup(entries, navHref))), true));
    } else if (ncxHref && lookup(entries, ncxHref)) {
      toc = readNcx(parseDocument(decodeText(await readEntry(u8, lookup(entries, ncxHref))), false));
    }
    const parts = [];
    if (title) {
      parts.push("# " + escapeLine(title) + "\n\n");
    }
    const tocText = tocLines(toc, 0);
    if (tocText) {
      parts.push("## 目录\n\n" + tocText + "\n");
    }
    const refs = spineNode ? spineNode.children || [] : [];
    let chapters = 0;
    for (let i = 0; i < refs.length; i++) {
      if (tagName(refs[i]) !== "itemref" || refs[i].getAttribute("linear") === "no") {
        continue;
      }
      const item = manifest.get(refs[i].getAttribute("idref") || "");
      if (!item || skip.has(item.href) || !lookup(entries, item.href)) {
        continue;
      }
      if (!/html/i.test(item.type) && !/\.x?html?$/i.test(item.href)) {
        if (/image/i.test(item.type)) {
          parts.push(imageBlock(item.href, item.type));
        }
        continue;
      }
      chapterHref = item.href;
      const doc = parseDocument(decodeText(await readEntry(u8, lookup(entries, item.href))), true);
      styleRules = await rulesFor(u8, entries, doc, item.href);
      declaredCache = new WeakMap();
      scaleCache = new WeakMap();
      const body = elements(doc, "body")[0] || doc.documentElement;
      const chapter = body ? renderChildren(body).trim() : "";
      if (chapter) {
        parts.push(chapter + "\n\n");
        chapters++;
      }
    }
    if (!chapters) {
      throw new Error("EPUB 里没有可转换的正文");
    }
    const built = await attachImages(u8, entries, parts.join("").replace(/\n{3,}/g, "\n\n").trim() + "\n");
    return { name: fitName(sourceName(filename) || title || "未命名"), text: built.text, images: built.images };
  }

  root.epubToMarkdown = epubToMarkdown;
})(globalThis);

</script>
<script>
const $ = id => document.getElementById(id);
let freeBytes = -1;
function fmt(n) {
  if (n >= 1073741824) return (n / 1073741824).toFixed(1) + ' GB';
  return n >= 1048576 ? (n / 1048576).toFixed(1) + ' MB' : Math.ceil(n / 1024) + ' KB';
}
function removeBook(name) {
  if (!confirm('确定删除《' + name.replace(/\.(txt|md)$/i, '') + '》吗？阅读进度也会一并删除。')) return;
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
function uploadOne(file, bar, msg, bookName, imageName) {
  return new Promise(done => {
    const x = new XMLHttpRequest();
    let url = '/upload?name=' + encodeURIComponent(bookName || file.name);
    if (imageName) url += '&image=' + encodeURIComponent(imageName);
    x.open('POST', url);
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
    form.append('file', file, imageName || file.name);
    x.send(form);
  });
}
function isFileDrag(e) {
  const types = e.dataTransfer && e.dataTransfer.types;
  if (!types) return false;
  for (let i = 0; i < types.length; i++) if (types[i] === 'Files') return true;
  return false;
}
let dragDepth = 0;
window.addEventListener('dragenter', e => {
  if (!isFileDrag(e) || $('go').disabled) return;
  e.preventDefault();
  if (dragDepth++ === 0) document.body.classList.add('drag');
});
window.addEventListener('dragover', e => { if (isFileDrag(e)) e.preventDefault(); });
window.addEventListener('dragleave', e => {
  if (!isFileDrag(e) || dragDepth === 0) return;
  if (--dragDepth <= 0) { dragDepth = 0; document.body.classList.remove('drag'); }
});
window.addEventListener('drop', e => {
  if (!isFileDrag(e)) return;
  e.preventDefault();
  dragDepth = 0;
  document.body.classList.remove('drag');
  start(e.dataTransfer.files);
});
async function start(list) {
  const files = Array.from(list || $('file').files);
  if (!files.length) { alert('请先选择文件'); return; }
  if ($('go').disabled) return;
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
    let upload = f;
    if (/\.epub$/i.test(f.name)) {
      msg.textContent = '正在转换...';
      try {
        const book = await epubToMarkdown(await f.arrayBuffer(), f.name);
        upload = new File([book.text], book.name, { type: 'text/markdown' });
        upload.epubImages = book.images || [];
        li.firstChild.nodeValue = f.name + ' → ' + book.name;
        li.querySelector('.size').textContent = fmt(upload.size);
      } catch (err) {
        msg.textContent = '失败：' + (err && err.message ? err.message : '转换失败');
        continue;
      }
    } else if (!/\.(txt|md)$/i.test(f.name)) {
      msg.textContent = '跳过：只支持 TXT、Markdown 和 EPUB 文件';
      continue;
    }
    if (freeBytes >= 0 && upload.size > freeBytes) { msg.textContent = '跳过：SD 卡空间不足'; continue; }
    msg.textContent = '上传中...';
    await uploadOne(upload, bar, msg);
    if (!/^完成/.test(msg.textContent)) continue;
    if (freeBytes >= 0) freeBytes -= upload.size;
    const images = upload.epubImages || [];
    for (let n = 0; n < images.length; n++) {
      const image = images[n];
      if (freeBytes >= 0 && image.bytes.length > freeBytes) {
        msg.textContent = '正文已上传，图片空间不足';
        break;
      }
      msg.textContent = '正在上传图片 ' + (n + 1) + '/' + images.length;
      const picture = new File([image.bytes], image.name, { type: 'application/octet-stream' });
      await uploadOne(picture, bar, msg, upload.name, image.name);
      if (!/^完成/.test(msg.textContent)) {
        msg.textContent = '正文已上传，' + msg.textContent;
        break;
      }
      if (freeBytes >= 0) freeBytes -= image.bytes.length;
    }
    if (images.length && msg.textContent === '完成') msg.textContent = '完成，' + images.length + ' 张图';
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
