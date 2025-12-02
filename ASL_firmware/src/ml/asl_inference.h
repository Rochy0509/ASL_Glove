#pragma once

#include <cstddef>

#include "sensor_types.h"

class ASLInferenceEngine {
public:
    static constexpr char kNeutralToken = '\x01';
    static constexpr char kBackspaceToken = '\b';
    static constexpr char kSpaceToken = ' ';

    bool begin();
    bool isReady() const { return ready_; }

    bool classify(const SensorSample* samples,
                  size_t sample_count,
                  char& letter,
                  float& confidence,
                  int& class_index);

    const char* labelForIndex(size_t index) const;

private:
    bool ready_{false};
};

extern ASLInferenceEngine aslInference;
