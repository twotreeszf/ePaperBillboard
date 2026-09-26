#pragma once

#include "../Base/TTPictorialTypes.h"

enum TTPicJob {
    TT_PIC_JOB_TODAY = 0,
    TT_PIC_JOB_ANOTHER,
    TT_PIC_JOB_DAILY,
    TT_PIC_JOB_SERIES,
};

class TTPictorialService {
public:
    void requestToday();
    void requestAnother();
    void requestDaily();
    void requestSeries(uint8_t index);

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
    uint16_t pickIndex(uint16_t count, uint16_t avoid, bool avoidCurrent) const;
    void publish(TTPicState state, const char* message, const char* path, const char* name);
    void remember(const char* pinyin, uint16_t index, int dayKey, const char* path);
    void removeStale(const char* keep);
    int todayKey() const;

    TTPicSeries _series[TT_PIC_SERIES_MAX];
    uint8_t _seriesCount = 0;
    uint8_t _selected = 0;
    uint16_t _imageIndex = 0;
    bool _busy = false;
};
