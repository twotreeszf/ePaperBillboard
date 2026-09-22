#pragma once

#include <LittleFS.h>
#include <stddef.h>

#define TT_FILE_BUF_SIZE       256
#define TT_LITTLEFS_MOUNT      "/littlefs"
#define TT_FILE_FULL_PATH_MAX  64

File tt_file_open(fs::FS& fs, const char* path, const char* mode);
File tt_file_open(const char* path, const char* mode);
File tt_file_create(fs::FS& fs, const char* path);
File tt_file_create(const char* path);
bool tt_file_open_to(File* out, const char* path, const char* mode);
void tt_file_remove(fs::FS& fs, const char* path);
void tt_file_remove(const char* path);
bool tt_file_exists(fs::FS& fs, const char* path);
bool tt_file_exists(const char* path);
