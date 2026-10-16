#pragma once

#include <Arduino.h>
#include <FS.h>
#include <vector>

#define TT_BOOKS_DIR               "/books"
#define TT_BOOKS_CACHE_DIR         TT_BOOKS_DIR "/.cache"
#define TT_BOOKS_RECENT_FILE       TT_BOOKS_CACHE_DIR "/recent.txt"
#define TT_BOOKS_PROGRESS_FILE     "progress.bin"
#define TT_BOOKS_INDEX_PREFIX      "pages_"
#define TT_BOOKS_INDEX_SUFFIX      ".bin"
#define TT_BOOKS_EXT               ".txt"
#define TT_BOOKS_EXT_LEN           4
#define TT_BOOKS_RECENT_MAX        10
#define TT_BOOKS_MAX               200
#define TT_BOOKS_ID_TEXT_MAX       12
#define TT_BOOKS_PROGRESS_MAGIC    0x52505454u
#define TT_BOOKS_PROGRESS_VERSION  1

struct TTBookEntry {
    String name;
    uint32_t size = 0;
};

struct __attribute__((packed)) TTBookProgress {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t sourceSize;
    uint32_t offset;
};

class TTBookStore {
public:
    bool ensureDirs();
    bool ensureBookCache(const TTBookEntry& book);
    bool listBooks(std::vector<TTBookEntry>& out);
    bool removeBook(const String& name);
    void touchRecent(const String& name);
    bool loadProgress(const TTBookEntry& book, uint32_t& offset);
    bool saveProgress(const TTBookEntry& book, uint32_t offset);

    String bookPath(const TTBookEntry& book) const;
    String cacheDir(const TTBookEntry& book) const;
    String indexPath(const TTBookEntry& book, uint32_t configHash) const;
    String displayName(const String& name) const;

private:
    fs::FS& fs();
    bool ensureDir(const char* path);
    String bookId(const TTBookEntry& book) const;
    void removeCache(const TTBookEntry& book);
    void readRecent(std::vector<String>& out);
    void writeRecent(const std::vector<String>& names);
};
