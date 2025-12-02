#ifndef PERF_PROFILER_H
#define PERF_PROFILER_H

#include <Arduino.h>

// Configuration
#define PROFILER_MAX_EVENTS 1000
#define PROFILER_MAX_MARKERS 16

// Predefined timing markers for ASL Glove
enum ProfilingMarker {
    MARKER_SENSOR_READ = 0,
    MARKER_INFERENCE,
    MARKER_CLASSIFICATION,
    MARKER_LETTER_COMMIT,
    MARKER_TTS_DOWNLOAD,
    MARKER_TTS_PLAYBACK,
    MARKER_SHAKE_DETECT,
    MARKER_IMU_UPDATE,
    MARKER_FINGER_UPDATE,
    MARKER_WINDOW_BUILD,
    MARKER_CUSTOM_1,
    MARKER_CUSTOM_2,
    MARKER_CUSTOM_3,
    MARKER_CUSTOM_4,
    MARKER_CUSTOM_5,
    MARKER_CUSTOM_6
};

// Timing event structure
struct TimingEvent {
    uint32_t timestampUs;
    uint8_t markerId;
    bool isStart;  // true = start, false = end
};

// Statistics for a marker
struct TimingStats {
    uint32_t count;
    uint32_t minUs;
    uint32_t maxUs;
    uint32_t avgUs;
    uint32_t medianUs;
    const char* name;
};

class PerformanceProfiler {
private:
    TimingEvent events[PROFILER_MAX_EVENTS];
    size_t writeIndex;
    size_t eventCount;
    bool enabled;
    bool wrapped;
    
    const char* markerNames[PROFILER_MAX_MARKERS];
    uint32_t markerStartTimes[PROFILER_MAX_MARKERS];
    
    void initializeMarkerNames();
    
public:
    PerformanceProfiler();
    
    // Control
    void begin();
    void enable();
    void disable();
    void reset();
    bool isEnabled() const { return enabled; }
    
    // Timing markers
    void markStart(uint8_t markerId);
    void markEnd(uint8_t markerId);
    void markEvent(uint8_t markerId);
    
    // Statistics
    void calculateStats(uint8_t markerId, TimingStats& stats);
    void printStats();
    void printAllStats();
    
    // Export
    bool exportToVCD(const char* filename);
    
    // Info
    size_t getEventCount() const { return eventCount; }
    const char* getMarkerName(uint8_t markerId) const;
    void setMarkerName(uint8_t markerId, const char* name);
};

// Global profiler instance
extern PerformanceProfiler perfProfiler;

#endif // PERF_PROFILER_H
