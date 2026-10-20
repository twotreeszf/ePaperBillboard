#include "TTReaderSource.h"
#include "TTReaderText.h"
#include "../Base/Logger.h"
#include "../Base/TTFile.h"
#include <cstring>

bool TTReaderSource::open(fs::FS& fs, const char* path) {
    close();
    _file = tt_file_open(fs, path, "r");
    if (!_file) {
        return false;
    }
    _size = (uint32_t)_file.size();
    _failed = false;
    LOG_I("Reader source: open %s size=%u", path, (unsigned)_size);
    return true;
}

bool TTReaderSource::openMemory(const uint8_t* data, uint32_t size) {
    close();
    if (data == nullptr || size == 0 || size > TT_READER_SOURCE_BUF_SIZE) {
        LOG_E("Reader source: memory size %u rejected", (unsigned)size);
        return false;
    }
    memcpy(_buf, data, size);
    _bufStart = 0;
    _bufLen = size;
    _size = size;
    _failed = false;
    LOG_I("Reader source: memory size=%u", (unsigned)size);
    return true;
}

void TTReaderSource::close() {
    if (_file) {
        LOG_I("Reader source: close, %u buffer loads", (unsigned)_loads);
        _file.close();
    }
    _size = 0;
    _bufStart = 0;
    _bufLen = 0;
    _loads = 0;
}

bool TTReaderSource::ensure(uint32_t offset, uint32_t need) {
    if (need == 0 || offset >= _size) {
        return false;
    }
    if (offset >= _bufStart && offset + need <= _bufStart + _bufLen) {
        return true;
    }
    if (!_file) {
        return false;
    }
    uint32_t start = offset;
    if (_bufLen > 0 && offset < _bufStart && TT_READER_SOURCE_BUF_SIZE > need) {
        const uint32_t room = TT_READER_SOURCE_BUF_SIZE - need;
        start = offset > room ? offset - room : 0;
    }
    if (!_file.seek(start)) {
        LOG_E("Reader source: seek %u failed", (unsigned)start);
        _failed = true;
        return false;
    }
    const uint32_t want = _size - start < TT_READER_SOURCE_BUF_SIZE ? _size - start : TT_READER_SOURCE_BUF_SIZE;
    const size_t got = _file.read(_buf, want);
    if (got == 0) {
        LOG_E("Reader source: read at %u failed", (unsigned)start);
        _failed = true;
        _bufLen = 0;
        return false;
    }
    _bufStart = start;
    _bufLen = (uint32_t)got;
    _loads++;
    if (start != offset) {
        LOG_D("Reader source: rewind window %u covering %u", (unsigned)start, (unsigned)offset);
    }
    return offset + need <= _bufStart + _bufLen;
}

bool TTReaderSource::byteAt(uint32_t offset, uint8_t& out) {
    if (!ensure(offset, 1)) {
        return false;
    }
    out = _buf[offset - _bufStart];
    return true;
}

uint32_t TTReaderSource::next(uint32_t& offset) {
    if (offset >= _size) {
        return 0;
    }
    const uint32_t remain = _size - offset;
    const uint32_t need = remain < TT_UTF8_MAX_BYTES ? remain : TT_UTF8_MAX_BYTES;
    if (!ensure(offset, need)) {
        offset = _size;
        return 0;
    }
    uint32_t cp = 0;
    const size_t n = tt_utf8_decode(_buf + (offset - _bufStart), need, &cp);
    offset += (uint32_t)(n > 0 ? n : 1);
    return cp;
}

size_t TTReaderSource::probeInvalidUtf8(size_t& probed) {
    probed = 0;
    const uint32_t want = _size < TT_READER_SOURCE_BUF_SIZE ? _size : TT_READER_SOURCE_BUF_SIZE;
    if (want == 0 || !ensure(0, want)) {
        return 0;
    }
    probed = want;
    return tt_utf8_count_invalid(_buf, want);
}
