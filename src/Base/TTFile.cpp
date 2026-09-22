#include "TTFile.h"
#include "Logger.h"

#include <sys/stat.h>

File tt_file_open(fs::FS& fs, const char* path, const char* mode) {
    File file;
    if (path == nullptr || mode == nullptr) {
        return file;
    }
    file = fs.open(path, mode);
    if (!file) {
        LOG_E("File: open %s mode=%s failed", path, mode);
        return file;
    }
    if (!file.setBufferSize(TT_FILE_BUF_SIZE)) {
        LOG_W("File: setBufferSize(%u) failed %s", (unsigned)TT_FILE_BUF_SIZE, path);
    }
    return file;
}

File tt_file_open(const char* path, const char* mode) {
    return tt_file_open(LittleFS, path, mode);
}

File tt_file_create(fs::FS& fs, const char* path) {
    tt_file_remove(fs, path);
    return tt_file_open(fs, path, "w+");
}

File tt_file_create(const char* path) {
    return tt_file_create(LittleFS, path);
}

bool tt_file_open_to(File* out, const char* path, const char* mode) {
    if (out == nullptr) {
        return false;
    }
    *out = tt_file_open(path, mode);
    return (bool)(*out);
}

void tt_file_remove(fs::FS& fs, const char* path) {
    if (path == nullptr || path[0] != '/') {
        return;
    }
    char full[TT_FILE_FULL_PATH_MAX];
    const int n = snprintf(full, sizeof(full), "%s%s", TT_LITTLEFS_MOUNT, path);
    if (n <= 0 || (size_t)n >= sizeof(full)) {
        LOG_E("File: remove path too long %s", path);
        return;
    }
    struct stat st;
    if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) {
        return;
    }
    fs.remove(path);
}

void tt_file_remove(const char* path) {
    tt_file_remove(LittleFS, path);
}

bool tt_file_exists(fs::FS& fs, const char* path) {
    return path != nullptr && fs.exists(path);
}

bool tt_file_exists(const char* path) {
    return tt_file_exists(LittleFS, path);
}
