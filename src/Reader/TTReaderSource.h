#pragma once

#include <Arduino.h>
#include <FS.h>

#define TT_READER_SOURCE_BUF_SIZE  4096

class TTReaderSource {
public:
    bool open(fs::FS& fs, const char* path);
    bool openMemory(const uint8_t* data, uint32_t size);
    void close();
    bool isOpen() const { return (bool)_file || _size > 0; }
    bool failed() const { return _failed; }
    uint32_t size() const { return _size; }

    bool byteAt(uint32_t offset, uint8_t& out);
    uint32_t next(uint32_t& offset);
    size_t probeInvalidUtf8(size_t& probed);

private:
    bool ensure(uint32_t offset, uint32_t need);

    File _file;
    uint32_t _size = 0;
    uint32_t _bufStart = 0;
    uint32_t _bufLen = 0;
    uint32_t _loads = 0;
    bool _failed = false;
    uint8_t _buf[TT_READER_SOURCE_BUF_SIZE];
};
