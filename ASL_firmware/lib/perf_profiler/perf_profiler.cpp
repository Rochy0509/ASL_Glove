#include "perf_profiler.h"
#include <algorithm>
#include <SD.h>
#include <FS.h>

PerformanceProfiler perfProfiler;

PerformanceProfiler::PerformanceProfiler()
    : writeIndex(0), eventCount(0), enabled(false), wrapped(false) {
    memset(events, 0, sizeof(events));
    memset(markerStartTimes, 0, sizeof(markerStartTimes));
    initializeMarkerNames();
}

void PerformanceProfiler::initializeMarkerNames() {
    markerNames[MARKER_SENSOR_READ] = "SensorRead";
    markerNames[MARKER_INFERENCE] = "Inference";
    markerNames[MARKER_CLASSIFICATION] = "Classification";
    markerNames[MARKER_LETTER_COMMIT] = "LetterCommit";
    markerNames[MARKER_TTS_DOWNLOAD] = "TTS_Download";
    markerNames[MARKER_TTS_PLAYBACK] = "TTS_Playback";
    markerNames[MARKER_SHAKE_DETECT] = "ShakeDetect";
    markerNames[MARKER_IMU_UPDATE] = "IMU_Update";
    markerNames[MARKER_FINGER_UPDATE] = "FingerUpdate";
    markerNames[MARKER_WINDOW_BUILD] = "WindowBuild";
    markerNames[MARKER_CUSTOM_1] = "Custom1";
    markerNames[MARKER_CUSTOM_2] = "Custom2";
    markerNames[MARKER_CUSTOM_3] = "Custom3";
    markerNames[MARKER_CUSTOM_4] = "Custom4";
    markerNames[MARKER_CUSTOM_5] = "Custom5";
    markerNames[MARKER_CUSTOM_6] = "Custom6";
}

void PerformanceProfiler::begin() {
    reset();
}

void PerformanceProfiler::enable() {
    enabled = true;
    Serial.println("[PROFILER] Enabled");
}

void PerformanceProfiler::disable() {
    enabled = false;
    Serial.println("[PROFILER] Disabled");
}

void PerformanceProfiler::reset() {
    writeIndex = 0;
    eventCount = 0;
    wrapped = false;
    memset(events, 0, sizeof(events));
    memset(markerStartTimes, 0, sizeof(markerStartTimes));
    Serial.println("[PROFILER] Reset");
}

void PerformanceProfiler::markStart(uint8_t markerId) {
    if (!enabled || markerId >= PROFILER_MAX_MARKERS) return;
    
    uint32_t timestamp = micros();
    markerStartTimes[markerId] = timestamp;
    
    TimingEvent event;
    event.timestampUs = timestamp;
    event.markerId = markerId;
    event.isStart = true;
    
    events[writeIndex] = event;
    writeIndex = (writeIndex + 1) % PROFILER_MAX_EVENTS;
    
    if (eventCount < PROFILER_MAX_EVENTS) {
        eventCount++;
    } else {
        wrapped = true;
    }
}

void PerformanceProfiler::markEnd(uint8_t markerId) {
    if (!enabled || markerId >= PROFILER_MAX_MARKERS) return;
    
    uint32_t timestamp = micros();
    
    TimingEvent event;
    event.timestampUs = timestamp;
    event.markerId = markerId;
    event.isStart = false;
    
    events[writeIndex] = event;
    writeIndex = (writeIndex + 1) % PROFILER_MAX_EVENTS;
    
    if (eventCount < PROFILER_MAX_EVENTS) {
        eventCount++;
    } else {
        wrapped = true;
    }
}

void PerformanceProfiler::markEvent(uint8_t markerId) {
    if (!enabled || markerId >= PROFILER_MAX_MARKERS) return;
    
    markStart(markerId);
    markEnd(markerId);
}

void PerformanceProfiler::calculateStats(uint8_t markerId, TimingStats& stats) {
    stats.count = 0;
    stats.minUs = UINT32_MAX;
    stats.maxUs = 0;
    stats.avgUs = 0;
    stats.medianUs = 0;
    stats.name = getMarkerName(markerId);
    
    if (eventCount == 0) return;
    
    // Collect all durations for this marker
    uint32_t durations[PROFILER_MAX_EVENTS / 2];
    size_t durationCount = 0;
    uint32_t totalTime = 0;
    
    size_t startIdx = wrapped ? writeIndex : 0;
    size_t numEvents = wrapped ? PROFILER_MAX_EVENTS : eventCount;
    
    uint32_t lastStartTime = 0;
    bool foundStart = false;
    
    for (size_t i = 0; i < numEvents; i++) {
        size_t idx = (startIdx + i) % PROFILER_MAX_EVENTS;
        const TimingEvent& event = events[idx];
        
        if (event.markerId == markerId) {
            if (event.isStart) {
                lastStartTime = event.timestampUs;
                foundStart = true;
            } else if (foundStart) {
                uint32_t duration = event.timestampUs - lastStartTime;
                durations[durationCount++] = duration;
                totalTime += duration;
                
                if (duration < stats.minUs) stats.minUs = duration;
                if (duration > stats.maxUs) stats.maxUs = duration;
                
                foundStart = false;
            }
        }
    }
    
    stats.count = durationCount;
    
    if (durationCount > 0) {
        stats.avgUs = totalTime / durationCount;
        
        // Calculate median
        std::sort(durations, durations + durationCount);
        if (durationCount % 2 == 0) {
            stats.medianUs = (durations[durationCount / 2 - 1] + durations[durationCount / 2]) / 2;
        } else {
            stats.medianUs = durations[durationCount / 2];
        }
    } else {
        stats.minUs = 0;
    }
}

void PerformanceProfiler::printStats() {
    Serial.println("\n[PROFILER] Statistics Summary");
    Serial.println("=================================================");
    Serial.printf("Total Events: %u (Buffer %s)\n", 
                  eventCount, 
                  wrapped ? "WRAPPED" : "not wrapped");
    Serial.println("=================================================");
    Serial.println("Marker               | Count | Min(us) | Avg(us) | Max(us) | Median(us)");
    Serial.println("---------------------|-------|---------|---------|---------|------------");
    
    for (uint8_t i = 0; i < PROFILER_MAX_MARKERS; i++) {
        TimingStats stats;
        calculateStats(i, stats);
        
        if (stats.count > 0) {
            Serial.printf("%-20s | %5lu | %7lu | %7lu | %7lu | %10lu\n",
                         stats.name,
                         stats.count,
                         stats.minUs,
                         stats.avgUs,
                         stats.maxUs,
                         stats.medianUs);
        }
    }
    
    Serial.println("=================================================\n");
}

void PerformanceProfiler::printAllStats() {
    printStats();
    
    // Print time breakdown in milliseconds
    Serial.println("[PROFILER] Time Breakdown (milliseconds)");
    Serial.println("=================================================");
    
    for (uint8_t i = 0; i < PROFILER_MAX_MARKERS; i++) {
        TimingStats stats;
        calculateStats(i, stats);
        
        if (stats.count > 0) {
            Serial.printf("%-20s: min=%.3fms avg=%.3fms max=%.3fms\n",
                         stats.name,
                         stats.minUs / 1000.0f,
                         stats.avgUs / 1000.0f,
                         stats.maxUs / 1000.0f);
        }
    }
    
    Serial.println("=================================================\n");
}

bool PerformanceProfiler::exportToVCD(const char* filename) {
    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        Serial.printf("[PROFILER] Failed to open %s for writing\n", filename);
        return false;
    }
    
    // Write VCD header
    file.println("$date");
    file.printf("  %s\n", __DATE__);
    file.println("$end");
    file.println("$version");
    file.println("  ASL Glove Performance Profiler v1.0");
    file.println("$end");
    file.println("$timescale 1us $end");
    
    // Define signals
    file.println("$scope module top $end");
    for (uint8_t i = 0; i < PROFILER_MAX_MARKERS; i++) {
        TimingStats stats;
        calculateStats(i, stats);
        if (stats.count > 0) {
            // Create unique wire identifier (a-z, A-Z)
            char wireId = (i < 26) ? ('a' + i) : ('A' + (i - 26));
            file.printf("$var wire 1 %c %s $end\n", wireId, markerNames[i]);
        }
    }
    file.println("$upscope $end");
    file.println("$enddefinitions $end");
    
    // Write initial values
    file.println("$dumpvars");
    for (uint8_t i = 0; i < PROFILER_MAX_MARKERS; i++) {
        TimingStats stats;
        calculateStats(i, stats);
        if (stats.count > 0) {
            char wireId = (i < 26) ? ('a' + i) : ('A' + (i - 26));
            file.printf("0%c\n", wireId);
        }
    }
    file.println("$end");
    
    // Write timing events
    size_t startIdx = wrapped ? writeIndex : 0;
    size_t numEvents = wrapped ? PROFILER_MAX_EVENTS : eventCount;
    uint32_t baseTime = 0;
    
    if (numEvents > 0) {
        baseTime = events[startIdx].timestampUs;
    }
    
    for (size_t i = 0; i < numEvents; i++) {
        size_t idx = (startIdx + i) % PROFILER_MAX_EVENTS;
        const TimingEvent& event = events[idx];
        
        uint32_t relativeTime = event.timestampUs - baseTime;
        char wireId = (event.markerId < 26) ? ('a' + event.markerId) : ('A' + (event.markerId - 26));
        
        file.printf("#%lu\n", relativeTime);
        file.printf("%d%c\n", event.isStart ? 1 : 0, wireId);
    }
    
    file.close();
    Serial.printf("[PROFILER] Exported %u events to %s\n", numEvents, filename);
    return true;
}

const char* PerformanceProfiler::getMarkerName(uint8_t markerId) const {
    if (markerId >= PROFILER_MAX_MARKERS) return "Unknown";
    return markerNames[markerId];
}

void PerformanceProfiler::setMarkerName(uint8_t markerId, const char* name) {
    if (markerId >= PROFILER_MAX_MARKERS) return;
    markerNames[markerId] = name;
}
