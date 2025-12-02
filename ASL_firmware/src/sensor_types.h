#pragma once

#include <Arduino.h>

struct SensorSample {
    uint32_t timestampMs{0};
    float flex[5]{0};
    float accel[3]{0};
    float gyro[3]{0};
    float accelNorm[3]{0};
    float gyroNorm[3]{0};
    bool imuValid{false};
    bool fingersValid{false};
};
