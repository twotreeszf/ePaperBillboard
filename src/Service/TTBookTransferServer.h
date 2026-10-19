#pragma once

#include <Arduino.h>
#include <FS.h>
#include <WebServer.h>
#include <atomic>
#include <functional>
#include "../Models/TTNotificationPayloads.h"

#define TT_TRANSFER_HTTP_PORT        80
#define TT_TRANSFER_TMP_NAME         "upload.part"
#define TT_TRANSFER_LOG_STEP         (512u * 1024u)
#define TT_TRANSFER_SLOW_WRITE_MS    100
#define TT_TRANSFER_INVALID_CHARS    "\\/:*?\"<>|"

#define TT_TRANSFER_MSG_BAD_NAME     "文件名无效"
#define TT_TRANSFER_MSG_BAD_TYPE     "只支持 TXT 和 Markdown 文件"
#define TT_TRANSFER_MSG_OPEN_FAILED  "无法创建文件"
#define TT_TRANSFER_MSG_WRITE_FAILED "写入 SD 卡失败"
#define TT_TRANSFER_MSG_SAVE_FAILED  "保存文件失败"
#define TT_TRANSFER_MSG_ABORTED      "上传中断"
#define TT_TRANSFER_MSG_EMPTY        "文件为空"
#define TT_TRANSFER_MSG_NO_FILE      "没有收到文件"
#define TT_TRANSFER_MSG_NOT_FOUND    "文件不存在"
#define TT_TRANSFER_MSG_DELETE_FAILED "删除失败"

class TTBookTransferServer {
public:
    using EventCallback = std::function<void(TTBookTransferState state, const char* name,
                                             uint32_t size, const char* message)>;

    TTBookTransferServer() : _server(TT_TRANSFER_HTTP_PORT) {}

    TTBookTransferError begin(EventCallback onEvent);
    void stop();
    void handleClient();
    void requestAbort() { _abort = true; }
    bool isRunning() const { return _running; }
    uint16_t receivedCount() const { return _received; }

private:
    void handleRoot();
    void handleBooks();
    void handleUpload();
    void handleUploadDone();
    void handleDelete();
    void startUpload(const String& rawName);
    void writeUpload(const uint8_t* data, size_t len);
    void finishUpload();
    void failUpload(const char* reason);
    void closeTemp();
    const char* nameError(const String& name) const;
    String tempPath() const;
    void emit(TTBookTransferState state, const char* message);

    WebServer _server;
    File _file;
    EventCallback _onEvent;
    String _uploadName;
    const char* _uploadError = nullptr;
    uint32_t _uploadBytes = 0;
    uint32_t _uploadStartMs = 0;
    uint32_t _nextLogBytes = 0;
    uint16_t _received = 0;
    bool _uploadSeen = false;
    bool _uploadActive = false;
    bool _uploadDone = false;
    bool _routesReady = false;
    bool _running = false;
    bool _sdAcquired = false;
    std::atomic<bool> _abort{false};
};
