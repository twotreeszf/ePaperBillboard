#include "TTReaderIndex.h"
#include "../Base/Logger.h"
#include "../Base/TTFile.h"

static_assert(sizeof(TTReaderIndexHeader) == TT_READER_INDEX_HEADER_SIZE, "index header size");
static_assert(sizeof(TTReaderIndexRecord) == TT_READER_INDEX_RECORD_SIZE, "index record size");

bool TTReaderIndex::open(fs::FS& fs, const char* path, uint32_t configHash, uint32_t sourceSize) {
    close();
    _header = {};
    _header.magic = TT_READER_INDEX_MAGIC;
    _header.version = TT_READER_INDEX_VERSION;
    _header.recordSize = TT_READER_INDEX_RECORD_SIZE;
    _header.configHash = configHash;
    _header.sourceSize = sourceSize;

    if (fs.exists(path)) {
        _file = tt_file_open(fs, path, "r+");
        TTReaderIndexHeader stored = {};
        const bool readOk = _file && _file.read((uint8_t*)&stored, sizeof(stored)) == sizeof(stored);
        const uint32_t need = TT_READER_INDEX_HEADER_SIZE + stored.pageCount * TT_READER_INDEX_RECORD_SIZE;
        if (readOk && stored.magic == TT_READER_INDEX_MAGIC && stored.version == TT_READER_INDEX_VERSION
            && stored.recordSize == TT_READER_INDEX_RECORD_SIZE && stored.configHash == configHash
            && stored.sourceSize == sourceSize && _file.size() >= need) {
            _header = stored;
            LOG_I("Reader index: resume %s pages=%u parsed=%u/%u complete=%d",
                  path, (unsigned)_header.pageCount, (unsigned)_header.parsedBytes,
                  (unsigned)sourceSize, (int)_header.complete);
            return true;
        }
        LOG_W("Reader index: stale %s read=%d magic=%08x hash=%08x size=%u, rebuild",
              path, readOk ? 1 : 0, (unsigned)stored.magic, (unsigned)stored.configHash,
              (unsigned)stored.sourceSize);
        _file.close();
    }
    return reset(fs, path);
}

bool TTReaderIndex::reset(fs::FS& fs, const char* path) {
    if (fs.exists(path)) {
        fs.remove(path);
    }
    _file = tt_file_open(fs, path, "w+");
    if (!_file) {
        return false;
    }
    _header.pageCount = 0;
    _header.parsedBytes = 0;
    _header.complete = 0;
    if (!writeHeader()) {
        _file.close();
        return false;
    }
    _file.flush();
    LOG_I("Reader index: new %s hash=%08x size=%u",
          path, (unsigned)_header.configHash, (unsigned)_header.sourceSize);
    return true;
}

void TTReaderIndex::close() {
    if (!_file) {
        return;
    }
    sync();
    _file.close();
    LOG_I("Reader index: close pages=%u complete=%d", (unsigned)_header.pageCount, (int)_header.complete);
}

bool TTReaderIndex::writeHeader() {
    if (!_file.seek(0)) {
        LOG_E("Reader index: header seek failed");
        return false;
    }
    if (_file.write((const uint8_t*)&_header, sizeof(_header)) != sizeof(_header)) {
        LOG_E("Reader index: header write failed");
        return false;
    }
    return true;
}

bool TTReaderIndex::append(uint32_t start, uint32_t next, bool complete) {
    if (!_file || isComplete()) {
        return false;
    }
    TTReaderIndexRecord record = {};
    record.offset = start;
    const uint32_t pos = TT_READER_INDEX_HEADER_SIZE + _header.pageCount * TT_READER_INDEX_RECORD_SIZE;
    if (!_file.seek(pos) || _file.write((const uint8_t*)&record, sizeof(record)) != sizeof(record)) {
        LOG_E("Reader index: append page %u failed", (unsigned)_header.pageCount);
        return false;
    }
    _header.pageCount++;
    _header.parsedBytes = next;
    _header.complete = complete ? 1 : 0;
    _dirty = true;
    return true;
}

bool TTReaderIndex::sync() {
    if (!_file || !_dirty) {
        return true;
    }
    if (!writeHeader()) {
        return false;
    }
    _file.flush();
    _dirty = false;
    return true;
}

uint32_t TTReaderIndex::estimatedPages() const {
    if (isComplete() || _header.parsedBytes == 0) {
        return _header.pageCount;
    }
    return (uint32_t)(((uint64_t)_header.pageCount * _header.sourceSize + _header.parsedBytes - 1)
                      / _header.parsedBytes);
}

bool TTReaderIndex::covers(uint32_t offset) const {
    return _header.pageCount > 0 && (isComplete() || offset < _header.parsedBytes);
}

bool TTReaderIndex::pageStart(uint32_t index, uint32_t& start) {
    if (!_file || index >= _header.pageCount) {
        return false;
    }
    TTReaderIndexRecord record = {};
    const uint32_t pos = TT_READER_INDEX_HEADER_SIZE + index * TT_READER_INDEX_RECORD_SIZE;
    if (!_file.seek(pos) || _file.read((uint8_t*)&record, sizeof(record)) != sizeof(record)) {
        LOG_E("Reader index: read page %u failed", (unsigned)index);
        return false;
    }
    start = record.offset;
    return true;
}

int32_t TTReaderIndex::findPage(uint32_t offset) {
    int32_t lo = 0;
    int32_t hi = (int32_t)_header.pageCount - 1;
    int32_t found = -1;
    while (lo <= hi) {
        const int32_t mid = (lo + hi) / 2;
        uint32_t start = 0;
        if (!pageStart((uint32_t)mid, start)) {
            return -1;
        }
        if (start <= offset) {
            found = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return found;
}
