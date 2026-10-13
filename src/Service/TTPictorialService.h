#pragma once

#include "../Models/TTPictorialTypes.h"
#include <set>

enum TTPicJob {
    TT_PIC_JOB_TODAY = 0,
    TT_PIC_JOB_ANOTHER,
    TT_PIC_JOB_DAILY,
    TT_PIC_JOB_SERIES,
    TT_PIC_JOB_MANIFEST,
};

class TTPictorialService {
public:
    void requestToday();
    void requestAnother();
    void requestDaily();
    void requestSeries(uint8_t index);
    void requestManifest();

    uint8_t seriesCount() const { return _seriesCount; }
    uint8_t selectedIndex() const { return _selected; }
    bool busy() const { return _busy; }
    bool hasToday() const;
    bool seriesAt(uint8_t index, TTPicSeries* out) const;

private:
    void enqueue(TTPicJob job, uint8_t seriesIndex);
    void runJob(TTPicJob job, uint8_t seriesIndex);
    bool ensureManifest();
    bool loadManifest();
    int resolveSelected() const;
    bool showStoredToday();
    bool downloadIndex(const TTPicSeries& series, uint16_t index, bool updateDay);
    void beginSeen(const char* pinyin, uint16_t count, int dayKey);
    void noteShown(const TTPicSeries& series, int dayKey);
    void markSeen(uint16_t index);
    bool seenHas(uint16_t index) const;
    uint16_t pickUnseen(uint16_t count, uint16_t avoid) const;
    void publish(TTPicState state, const char* message, const char* path, const char* name);
    void remember(const char* pinyin, uint16_t index, int dayKey, const char* path);
    void removeStale(const char* keep, const char* alsoKeep);
    int todayKey() const;

    TTPicSeries _series[TT_PIC_SERIES_MAX];
    uint8_t _seriesCount = 0;
    uint8_t _selected = 0;
    uint16_t _imageIndex = 0;
    bool _busy = false;
    bool _seenReady = false;
    int _seenDay = 0;
    uint16_t _seenCount = 0;
    char _seenPinyin[TT_PIC_PINYIN_MAX] = {};
    std::set<uint16_t> _seen;
};
