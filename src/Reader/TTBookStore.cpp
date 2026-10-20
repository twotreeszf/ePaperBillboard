#include "TTBookStore.h"
#include "TTReaderText.h"
#include "../Base/Logger.h"
#include "../Base/TTFile.h"
#include "../Base/TTInstance.h"
#include "../Service/TTSdCardService.h"
#include <algorithm>
#include <strings.h>

static void tt_book_yield(uint32_t& lastYieldMs) {
    const uint32_t now = millis();
    if ((uint32_t)(now - lastYieldMs) < TT_BOOKS_CACHE_YIELD_MS) {
        return;
    }
    vTaskDelay(1);
    lastYieldMs = millis();
}

static bool tt_book_ends_with(const char* name, const char* ext) {
    const size_t len = strlen(name);
    const size_t extLen = strlen(ext);
    return len > extLen && strcasecmp(name + len - extLen, ext) == 0;
}

size_t TTBookStore::bookExtLength(const char* name) {
    if (tt_book_ends_with(name, TT_BOOKS_EXT_TXT)) {
        return strlen(TT_BOOKS_EXT_TXT);
    }
    if (tt_book_ends_with(name, TT_BOOKS_EXT_MD)) {
        return strlen(TT_BOOKS_EXT_MD);
    }
    return 0;
}

bool TTBookStore::isMarkdownName(const char* name) {
    return tt_book_ends_with(name, TT_BOOKS_EXT_MD);
}

fs::FS& TTBookStore::fs() {
    return TTInstanceOf<TTSdCardService>().fs();
}

bool TTBookStore::ensureDir(const char* path) {
    if (fs().exists(path)) {
        return true;
    }
    if (!fs().mkdir(path)) {
        LOG_E("Book store: mkdir %s failed", path);
        return false;
    }
    LOG_I("Book store: mkdir %s", path);
    return true;
}

bool TTBookStore::ensureDirs() {
    return ensureDir(TT_BOOKS_DIR) && ensureDir(TT_BOOKS_CACHE_DIR);
}

bool TTBookStore::ensureBookCache(const TTBookEntry& book) {
    return ensureDirs() && ensureDir(cacheDir(book).c_str());
}

bool TTBookStore::listBooks(std::vector<TTBookEntry>& out) {
    out.clear();
    const uint32_t start = millis();
    File dir = fs().open(TT_BOOKS_DIR);
    if (!dir || !dir.isDirectory()) {
        LOG_E("Book store: open %s failed", TT_BOOKS_DIR);
        return false;
    }
    while (out.size() < TT_BOOKS_MAX) {
        File f = dir.openNextFile();
        if (!f) {
            break;
        }
        const char* name = f.name();
        if (!f.isDirectory() && name != nullptr && name[0] != '.' && bookExtLength(name) > 0) {
            TTBookEntry entry;
            entry.name = name;
            entry.size = (uint32_t)f.size();
            out.push_back(entry);
        }
        f.close();
    }
    dir.close();

    std::vector<String> recent;
    readRecent(recent);
    auto rank = [&recent](const String& name) -> size_t {
        for (size_t i = 0; i < recent.size(); i++) {
            if (recent[i] == name) {
                return i;
            }
        }
        return recent.size();
    };
    std::sort(out.begin(), out.end(), [&rank](const TTBookEntry& a, const TTBookEntry& b) {
        const size_t ra = rank(a.name);
        const size_t rb = rank(b.name);
        if (ra != rb) {
            return ra < rb;
        }
        return strcmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    LOG_I("Book store: %u books, %u recent, %u ms",
          (unsigned)out.size(), (unsigned)recent.size(), (unsigned)(millis() - start));
    return true;
}

bool TTBookStore::removeBook(const String& name) {
    TTBookEntry book;
    book.name = name;
    const String path = bookPath(book);
    File f = fs().open(path, "r");
    if (!f || f.isDirectory()) {
        LOG_W("Book store: remove %s, not found", path.c_str());
        return false;
    }
    book.size = (uint32_t)f.size();
    f.close();
    removeCache(book);
    if (!fs().remove(path)) {
        LOG_E("Book store: remove %s failed", path.c_str());
        return false;
    }
    std::vector<String> recent;
    readRecent(recent);
    const size_t before = recent.size();
    recent.erase(std::remove(recent.begin(), recent.end(), name), recent.end());
    if (recent.size() != before) {
        writeRecent(recent);
    }
    LOG_I("Book store: removed %s (%u bytes)", path.c_str(), (unsigned)book.size);
    return true;
}

void TTBookStore::removeCache(const TTBookEntry& book) {
    const String dirPath = cacheDir(book);
    File dir = fs().open(dirPath);
    if (!dir || !dir.isDirectory()) {
        return;
    }
    std::vector<String> files;
    uint32_t lastYieldMs = millis();
    while (true) {
        File f = dir.openNextFile();
        if (!f) {
            break;
        }
        files.push_back(dirPath + "/" + f.name());
        f.close();
        tt_book_yield(lastYieldMs);
    }
    dir.close();
    LOG_I("Book store: clearing cache %s, %u file(s)", dirPath.c_str(), (unsigned)files.size());
    for (const String& file : files) {
        fs().remove(file);
        tt_book_yield(lastYieldMs);
    }
    const bool removed = fs().rmdir(dirPath);
    LOG_I("Book store: cache %s cleared, %u file(s), rmdir=%d",
          dirPath.c_str(), (unsigned)files.size(), removed ? 1 : 0);
}

void TTBookStore::readRecent(std::vector<String>& out) {
    out.clear();
    if (!fs().exists(TT_BOOKS_RECENT_FILE)) {
        return;
    }
    File f = tt_file_open(fs(), TT_BOOKS_RECENT_FILE, "r");
    if (!f) {
        return;
    }
    while (f.available() && out.size() < TT_BOOKS_RECENT_MAX) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            out.push_back(line);
        }
    }
    f.close();
}

void TTBookStore::writeRecent(const std::vector<String>& names) {
    File f = tt_file_open(fs(), TT_BOOKS_RECENT_FILE, "w");
    if (!f) {
        return;
    }
    for (const String& name : names) {
        f.print(name);
        f.print('\n');
    }
    f.close();
}

void TTBookStore::touchRecent(const String& name) {
    std::vector<String> recent;
    readRecent(recent);
    recent.erase(std::remove(recent.begin(), recent.end(), name), recent.end());
    recent.insert(recent.begin(), name);
    if (recent.size() > TT_BOOKS_RECENT_MAX) {
        recent.resize(TT_BOOKS_RECENT_MAX);
    }
    writeRecent(recent);
    LOG_I("Book store: recent %s (%u)", name.c_str(), (unsigned)recent.size());
}

String TTBookStore::bookId(const TTBookEntry& book) const {
    uint32_t h = tt_reader_hash(book.name.c_str(), book.name.length());
    h = tt_reader_hash(&book.size, sizeof(book.size), h);
    char text[TT_BOOKS_ID_TEXT_MAX];
    snprintf(text, sizeof(text), "%08x", (unsigned)h);
    return String(text);
}

String TTBookStore::bookPath(const TTBookEntry& book) const {
    return String(TT_BOOKS_DIR "/") + book.name;
}

String TTBookStore::cacheDir(const TTBookEntry& book) const {
    return String(TT_BOOKS_CACHE_DIR "/") + bookId(book);
}

String TTBookStore::indexPath(const TTBookEntry& book, uint32_t configHash) const {
    char hash[TT_BOOKS_ID_TEXT_MAX];
    snprintf(hash, sizeof(hash), "%08x", (unsigned)configHash);
    return cacheDir(book) + "/" TT_BOOKS_INDEX_PREFIX + hash + TT_BOOKS_INDEX_SUFFIX;
}

String TTBookStore::displayName(const String& name) const {
    const size_t ext = bookExtLength(name.c_str());
    if (ext > 0) {
        return name.substring(0, name.length() - ext);
    }
    return name;
}

bool TTBookStore::loadProgress(const TTBookEntry& book, uint32_t& offset) {
    offset = 0;
    const String path = cacheDir(book) + "/" TT_BOOKS_PROGRESS_FILE;
    if (!fs().exists(path)) {
        return false;
    }
    File f = tt_file_open(fs(), path.c_str(), "r");
    if (!f) {
        return false;
    }
    TTBookProgress progress = {};
    const bool ok = f.read((uint8_t*)&progress, sizeof(progress)) == sizeof(progress);
    f.close();
    if (!ok || progress.magic != TT_BOOKS_PROGRESS_MAGIC || progress.version != TT_BOOKS_PROGRESS_VERSION
        || progress.sourceSize != book.size || progress.offset >= book.size) {
        LOG_W("Book store: progress invalid %s", path.c_str());
        return false;
    }
    offset = progress.offset;
    return true;
}

bool TTBookStore::saveProgress(const TTBookEntry& book, uint32_t offset) {
    if (!ensureBookCache(book)) {
        return false;
    }
    const String path = cacheDir(book) + "/" TT_BOOKS_PROGRESS_FILE;
    File f = tt_file_open(fs(), path.c_str(), "w");
    if (!f) {
        return false;
    }
    TTBookProgress progress = {};
    progress.magic = TT_BOOKS_PROGRESS_MAGIC;
    progress.version = TT_BOOKS_PROGRESS_VERSION;
    progress.sourceSize = book.size;
    progress.offset = offset;
    const bool ok = f.write((const uint8_t*)&progress, sizeof(progress)) == sizeof(progress);
    f.close();
    if (!ok) {
        LOG_E("Book store: progress write failed %s", path.c_str());
        return false;
    }
    LOG_I("Book store: progress %s offset=%u/%u", book.name.c_str(), (unsigned)offset, (unsigned)book.size);
    return true;
}
