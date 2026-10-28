#pragma once

#include <WebServer.h>
#include <freertos/semphr.h>

#define TT_DEBUG_HTTP_PORT   8080
#define TT_DEBUG_RETRY_MS    15000
#define PREF_DEV_DEBUG       "dev_debug"

class TTDebugServer {
public:
    TTDebugServer() : _server(TT_DEBUG_HTTP_PORT) {}

    void begin();
    void stop();
    void handleClient();
    bool isRunning() const { return _running; }

private:
    void handleRoot();
    void handleKey();
    void handleShot();
    bool takeKey(uint8_t& key, const char*& name) const;
    bool takeGesture(uint8_t& gesture, const char*& name) const;

    WebServer _server;
    SemaphoreHandle_t _shotDone = nullptr;
    int _shotResult = 0;
    bool _routesReady = false;
    bool _running = false;
};
