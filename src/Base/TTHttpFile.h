#pragma once

#include <WebServer.h>

bool ttHttpWwwPath(const char* path);
bool ttHttpSendFile(WebServer& server, const char* path, const char* contentType = nullptr);
