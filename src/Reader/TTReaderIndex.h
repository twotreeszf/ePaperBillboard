#pragma once

#include <Arduino.h>
#include <FS.h>

#define TT_READER_INDEX_MAGIC        0x49505454u
#define TT_READER_INDEX_VERSION      1
#define TT_READER_INDEX_RECORD_SIZE  8
#define TT_READER_INDEX_HEADER_SIZE  32

struct __attribute__((packed)) TTReaderIndexHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t recordSize;
    uint32_t configHash;
    uint32_t sourceSize;
    uint32_t pageCount;
    uint32_t parsedBytes;
    uint8_t complete;
    uint8_t reserved[7];
};

struct __attribute__((packed)) TTReaderIndexRecord {
    uint32_t offset;
    uint8_t style;
    uint8_t block;
    uint8_t flags;
    uint8_t reserved;
};

class TTReaderIndex {
public:
    bool open(fs::FS& fs, const char* path, uint32_t configHash, uint32_t sourceSize);
    void close();
    bool isOpen() const { return (bool)_file; }

    bool append(uint32_t start, uint32_t next, bool complete);
    bool sync();

    bool isComplete() const { return _header.complete != 0; }
    uint32_t pageCount() const { return _header.pageCount; }
    uint32_t parsedBytes() const { return _header.parsedBytes; }
    uint32_t estimatedPages() const;
    bool covers(uint32_t offset) const;
    bool pageStart(uint32_t index, uint32_t& start);
    int32_t findPage(uint32_t offset);

private:
    bool reset(fs::FS& fs, const char* path);
    bool writeHeader();

    File _file;
    TTReaderIndexHeader _header = {};
    bool _dirty = false;
};
