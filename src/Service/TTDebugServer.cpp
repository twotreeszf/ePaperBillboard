#include "TTDebugServer.h"

#include "../Base/ITTScreenPage.h"
#include "../Base/Logger.h"
#include "../Base/TTInstance.h"
#include "../Base/TTScreenshotDisplay.h"
#include "../Tasks/TTUITask.h"
#include <WiFi.h>
#include <cstdio>
#include <cstring>

namespace {

const char* kDebugPage = R"html(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>调试</title>
<style>
body { font-family: -apple-system, sans-serif; margin: 16px; }
button { margin: 4px; padding: 10px 14px; font-size: 16px; }
img { display: block; margin-top: 12px; background: #eee; max-width: 100%; image-rendering: pixelated; }
#msg { color: #333; min-height: 1.2em; word-break: break-all; }
.tip { color: #666; font-size: 13px; }
</style>
</head>
<body>
<h2>调试</h2>
<p class="tip">GET/POST /key?key=up|down|center&amp;gesture=click|double|long<br>GET /shot</p>
<p id="msg"></p>
<div id="keys"></div>
<button id="shot" type="button">截图</button>
<img id="screen" alt="screen">
<script>
const keys = [["up","上"],["down","下"],["center","中"]];
const gestures = [["click","单击"],["double","双击"],["long","长按"]];
const box = document.getElementById("keys");
const msg = document.getElementById("msg");
keys.forEach(function(key) {
  gestures.forEach(function(gesture) {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = key[1] + gesture[1];
    button.onclick = function() {
      fetch("/key?key=" + key[0] + "&gesture=" + gesture[0])
        .then(function(response) { return response.text(); })
        .then(function(text) { msg.textContent = text; })
        .catch(function() { msg.textContent = "请求失败"; });
    };
    box.appendChild(button);
  });
  box.appendChild(document.createElement("br"));
});
document.getElementById("shot").onclick = function() {
  document.getElementById("screen").src = "/shot?t=" + Date.now();
};
</script>
</body>
</html>
)html";

struct TTDebugBody {
    WiFiClient client;
    bool ok;
};

bool debugWrite(void* ctx, const uint8_t* data, size_t len) {
    TTDebugBody* body = static_cast<TTDebugBody*>(ctx);
    size_t sent = 0;
    int spins = 0;
    while (sent < len) {
        const int n = body->client.write(data + sent, len - sent);
        if (n > 0) {
            sent += (size_t)n;
            spins = 0;
            continue;
        }
        if (!body->client.connected() || ++spins > 40) {
            body->ok = false;
            LOG_E("Debug: shot write stalled");
            return false;
        }
        delay(10);
    }
    return true;
}

bool sameText(const String& text, const char* expected) {
    return text.equalsIgnoreCase(expected);
}

}

void TTDebugServer::begin() {
    if (_running) {
        return;
    }
    if (!_routesReady) {
        _server.on("/", HTTP_GET, [this]() { handleRoot(); });
        _server.on("/key", HTTP_GET, [this]() { handleKey(); });
        _server.on("/key", HTTP_POST, [this]() { handleKey(); });
        _server.on("/shot", HTTP_GET, [this]() { handleShot(); });
        _routesReady = true;
    }
    _server.begin();
    _running = true;
    LOG_I("Debug: server started port=%u", (unsigned)TT_DEBUG_HTTP_PORT);
}

void TTDebugServer::stop() {
    if (!_running) {
        return;
    }
    _server.stop();
    _running = false;
    LOG_I("Debug: server stopped");
}

void TTDebugServer::handleClient() {
    if (_running) {
        _server.handleClient();
    }
}

void TTDebugServer::handleRoot() {
    LOG_I("Debug: page from %s", _server.client().remoteIP().toString().c_str());
    _server.send(200, "text/html; charset=utf-8", kDebugPage);
}

bool TTDebugServer::takeKey(uint8_t& key, const char*& name) const {
    const String text = _server.arg("key");
    if (sameText(text, "up") || sameText(text, "right")) {
        key = TT_KEY_RIGHT;
        name = "up";
        return true;
    }
    if (sameText(text, "down") || sameText(text, "left")) {
        key = TT_KEY_LEFT;
        name = "down";
        return true;
    }
    if (sameText(text, "center")) {
        key = TT_KEY_CENTER;
        name = "center";
        return true;
    }
    return false;
}

bool TTDebugServer::takeGesture(uint8_t& gesture, const char*& name) const {
    const String text = _server.arg("gesture");
    if (sameText(text, "click")) {
        gesture = TT_KEY_CLICK;
        name = "click";
        return true;
    }
    if (sameText(text, "double") || sameText(text, "double_click")) {
        gesture = TT_KEY_DOUBLE_CLICK;
        name = "double";
        return true;
    }
    if (sameText(text, "long") || sameText(text, "long_press")) {
        gesture = TT_KEY_LONG_PRESS;
        name = "long";
        return true;
    }
    return false;
}

void TTDebugServer::handleKey() {
    uint8_t key = 0;
    uint8_t gesture = 0;
    const char* keyName = "";
    const char* gestureName = "";
    if (!takeKey(key, keyName) || !takeGesture(gesture, gestureName)) {
        LOG_W("Debug: bad key from %s key=%s gesture=%s",
              _server.client().remoteIP().toString().c_str(),
              _server.arg("key").c_str(), _server.arg("gesture").c_str());
        _server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad key\"}");
        return;
    }
    LOG_I("Debug: key %s %s from %s", keyName, gestureName,
          _server.client().remoteIP().toString().c_str());
    TTInstanceOf<TTUITask>().requestKeyGestureAsync(key, gesture);
    char body[80];
    snprintf(body, sizeof(body), "{\"ok\":true,\"key\":\"%s\",\"gesture\":\"%s\"}", keyName, gestureName);
    _server.send(200, "application/json", body);
}

void TTDebugServer::handleShot() {
    LOG_I("Debug: shot request from %s", _server.client().remoteIP().toString().c_str());
    if (_shotDone == nullptr) {
        _shotDone = xSemaphoreCreateBinary();
    }
    if (_shotDone == nullptr) {
        LOG_E("Debug: shot semaphore failed");
        _server.send(500, "text/plain; charset=utf-8", "截图失败");
        return;
    }
    _shotResult = (int)TT_SHOT_FAILED;
    TTInstanceOf<TTUITask>().requestRenderShot(_shotDone, &_shotResult);
    xSemaphoreTake(_shotDone, portMAX_DELAY);
    if (_shotResult != (int)TT_SHOT_OK) {
        LOG_W("Debug: shot failed result=%d", _shotResult);
        _server.send(503, "text/plain; charset=utf-8", "截图失败");
        return;
    }

    LOG_I("Debug: shot send %u bytes", (unsigned)TT_SHOT_FILE_BYTES);
    _server.sendHeader("Cache-Control", "no-store");
    _server.setContentLength(TT_SHOT_FILE_BYTES);
    _server.send(200, "image/bmp", "");
    TTDebugBody body;
    body.client = _server.client();
    body.ok = true;
    auto& shots = TTInstanceOf<TTScreenshotDisplay>();
    const bool wrote = shots.writeFrameBmp(debugWrite, &body);
    shots.releaseFrame();
    if (!wrote || !body.ok) {
        LOG_E("Debug: shot send failed");
    }
}
