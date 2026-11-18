#pragma once

#include <LittleFS.h>
#include <stddef.h>

#define TT_FILE_BUF_SIZE       256
#define TT_LITTLEFS_MOUNT      "/littlefs"
#define TT_FILE_FULL_PATH_MAX  64
#define TT_FS_RES_DIR          "/res"
#define TT_FS_FONT_DIR         "/fonts"
#define TT_FS_TMP_DIR          "/tmp"
#define TT_FS_WWW_DIR          "/res/www"
#define TT_WWW_NETWORK_PAGE    "/res/www/network/index.html"
#define TT_WWW_SETUP_PAGE      "/res/www/setup/index.html"
#define TT_WWW_SETUP_SHOTS     "/res/www/setup/shots.html"
#define TT_WWW_SETUP_RESULT    "/res/www/setup/result.html"
#define TT_WWW_BOOKS_PAGE      "/res/www/books/index.html"
#define TT_WWW_DEBUG_PAGE      "/res/www/debug/index.html"

File tt_file_open(fs::FS& fs, const char* path, const char* mode);
File tt_file_open(const char* path, const char* mode);
File tt_file_create(fs::FS& fs, const char* path);
File tt_file_create(const char* path);
bool tt_file_open_to(File* out, const char* path, const char* mode);
void tt_file_remove(fs::FS& fs, const char* path);
void tt_file_remove(const char* path);
bool tt_file_exists(fs::FS& fs, const char* path);
bool tt_file_exists(const char* path);
