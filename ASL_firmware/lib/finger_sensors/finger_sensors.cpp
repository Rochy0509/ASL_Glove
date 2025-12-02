#include "finger_sensors.h"

// Moving Average Filter Implementation

MovingAverageFilter::MovingAverageFilter(int windowSize, float deadbandThreshold) {
    size = min(windowSize, MAX_FILTER_SIZE);
    index = 0;
    sum = 0;
    count = 0;
    lastOutput = 0;
    deadband = deadbandThreshold;

    for (int i = 0; i < MAX_FILTER_SIZE; i++) {
        buffer[i] = 0;
    }
}

float MovingAverageFilter::add(float value) {
    // Remove oldest value from sum
    if (count >= size) {
        sum -= buffer[index];
    }

    // Add new value
    buffer[index] = value;
    sum += value;
    index = (index + 1) % size;

    if (count < size) count++;

    // Calculate average
    float filtered = sum / count;

    // Apply deadband to reduce jitter
    if (count >= size && abs(filtered - lastOutput) < deadband) {
        return lastOutput;
    }

    lastOutput = filtered;
    return filtered;
}

void MovingAverageFilter::reset() {
    sum = 0;
    count = 0;
    index = 0;
    lastOutput = 0;
}

// Finger Sensor Implementation

FingerSensor::FingerSensor()
    : pin(0), name(""), filter(5, 0.02f), numCalibPoints(0),
      baselineVoltage(0), baselineEstablished(false),
      baselineCount(0), baselineSum(0),
      flex_min(0), flex_max(V_REF), calibrationComplete(false),
      lastRawVoltage(0), lastFilteredVoltage(0), lastAngle(0), lastNormalized(0) {
}

void FingerSensor::begin(uint8_t analogPin, const String& fingerName, int filterSize) {
    pin = analogPin;
    name = fingerName;
    filter = MovingAverageFilter(filterSize, 0.02f);

    // Set default calibration if none provided
    if (numCalibPoints == 0) {
        setDefaultCalibration();
    }

    // Configure ADC
    analogReadResolution(ADC_RESOLUTION);
    analogSetAttenuation(ADC_11db);  // Full range 0-3.3V
}

void FingerSensor::setCalibration(const CalibrationPoint* points, int count) {
    numCalibPoints = min(count, MAX_CALIB_POINTS);
    for (int i = 0; i < numCalibPoints; i++) {
        calibration[i] = points[i];
    }
}

void FingerSensor::setDefaultCalibration() {
    // Default calibration curve (voltage -> angle)
    numCalibPoints = 6;
    calibration[0] = {3.30f, 0.0f};      // Fully extended
    calibration[1] = {2.80f, 30.0f};     // Slightly bent
    calibration[2] = {2.00f, 60.0f};     // Half bent
    calibration[3] = {1.00f, 90.0f};     // Mostly closed
    calibration[4] = {0.50f, 120.0f};    // Nearly closed
    calibration[5] = {0.00f, 180.0f};    // Fully closed
}

float FingerSensor::readVoltage() {
    // Oversample for better accuracy
    const int oversample = 4;
    int sum = 0;
    for (int i = 0; i < oversample; i++) {
        sum += analogRead(pin);
    }
    float avgRaw = sum / (float)oversample;
    return (avgRaw / ADC_MAX) * V_REF;
}

float FingerSensor::voltageToAngle(float voltage) {
    // Clamp to calibration range
    if (voltage >= calibration[0].voltage) {
        return calibration[0].angle;
    }
    if (voltage <= calibration[numCalibPoints - 1].voltage) {
        return calibration[numCalibPoints - 1].angle;
    }

    // Linear interpolation between calibration points
    for (int i = 0; i < numCalibPoints - 1; i++) {
        float vHigh = calibration[i].voltage;
        float vLow = calibration[i + 1].voltage;

        if (voltage <= vHigh && voltage >= vLow) {
            float aHigh = calibration[i].angle;
            float aLow = calibration[i + 1].angle;

            // Linear interpolation
            float ratio = (voltage - vLow) / (vHigh - vLow);
            return aLow + ratio * (aHigh - aLow);
        }
    }

    return 0;
}

bool FingerSensor::updateBaseline() {
    if (baselineEstablished) {
        return true;
    }

    float rawVoltage = readVoltage();
    baselineSum += rawVoltage;
    baselineCount++;

    if (baselineCount >= BASELINE_SAMPLES) {
        baselineVoltage = baselineSum / BASELINE_SAMPLES;
        baselineEstablished = true;
        return true;
    }

    return false;
}

void FingerSensor::resetBaseline() {
    baselineVoltage = 0;
    baselineEstablished = false;
    baselineCount = 0;
    baselineSum = 0;
    filter.reset();
}

void FingerSensor::update() {
    // Read raw voltage
    lastRawVoltage = readVoltage();

    // Drift compensation - adjust based on baseline
    float compensatedVoltage = lastRawVoltage;
    if (baselineEstablished) {
        compensatedVoltage = lastRawVoltage + (V_REF - baselineVoltage);
        compensatedVoltage = constrain(compensatedVoltage, 0.0f, V_REF);
    }

    // Apply smoothing filter
    lastFilteredVoltage = filter.add(compensatedVoltage);

    // Convert to angle
    lastAngle = voltageToAngle(lastFilteredVoltage);

    // Calculate normalized value (0-1 range) if calibrated
    if (calibrationComplete) {
        float span = flex_max - flex_min;
        if (fabsf(span) > 0.01f) {
            float value;
            if (span > 0.0f) {
                value = (lastRawVoltage - flex_min) / span;
            } else {
                value = (flex_min - lastRawVoltage) / (-span);
            }
            lastNormalized = constrain(value, 0.0f, 1.0f);
        } else {
            lastNormalized = 0.0f;
        }
    } else {
        lastNormalized = 0.0f;
    }
}

String FingerSensor::getPositionStatus() const {
    if (lastAngle < 15.0f) return "Extended";
    else if (lastAngle < 45.0f) return "Slight bend";
    else if (lastAngle < 75.0f) return "Half bent";
    else if (lastAngle < 105.0f) return "Bent";
    else if (lastAngle < 150.0f) return "Nearly closed";
    else return "Closed";
}

// Finger Sensor Manager Implementation

FingerSensorManager::FingerSensorManager()
    : numFingers(0), baselineComplete(false) {
}

void FingerSensorManager::begin() {
    // Configure ADC globally
    analogReadResolution(ADC_RESOLUTION);
    analogSetAttenuation(ADC_11db);
}

int FingerSensorManager::addFinger(uint8_t pin, const String& name, int filterSize) {
    if (numFingers >= MAX_FINGERS) {
        Serial.println("ERROR: Maximum number of fingers reached!");
        return -1;
    }

    int index = numFingers;
    fingers[index].begin(pin, name, filterSize);
    numFingers++;

    return index;
}

void FingerSensorManager::setFingerCalibration(int fingerIndex, const CalibrationPoint* points, int count) {
    if (fingerIndex >= 0 && fingerIndex < numFingers) {
        fingers[fingerIndex].setCalibration(points, count);
    }
}

bool FingerSensorManager::establishBaseline() {
    if (baselineComplete) {
        return true;
    }

    bool allReady = true;
    for (int i = 0; i < numFingers; i++) {
        if (!fingers[i].updateBaseline()) {
            allReady = false;
        }
    }

    if (allReady) {
        baselineComplete = true;
        Serial.println("\nBaseline Calibration Complete");
        for (int i = 0; i < numFingers; i++) {
            Serial.printf("%s: %.3f V\n",
                         fingers[i].getName().c_str(),
                         fingers[i].getBaselineVoltage());
        }
        Serial.println();
    }

    return baselineComplete;
}

void FingerSensorManager::resetAllBaselines() {
    for (int i = 0; i < numFingers; i++) {
        fingers[i].resetBaseline();
    }
    baselineComplete = false;
}

void FingerSensorManager::updateAll() {
    for (int i = 0; i < numFingers; i++) {
        fingers[i].update();
    }
}

FingerSensor* FingerSensorManager::getFinger(int index) {
    if (index >= 0 && index < numFingers) {
        return &fingers[index];
    }
    return nullptr;
}

void FingerSensorManager::getAngles(float* angles) {
    for (int i = 0; i < numFingers; i++) {
        angles[i] = fingers[i].getAngle();
    }
}

void FingerSensorManager::getFilteredVoltages(float* voltages) {
    for (int i = 0; i < numFingers; i++) {
        voltages[i] = fingers[i].getFilteredVoltage();
    }
}

void FingerSensorManager::printStatus() {
    Serial.println("Finger Sensor Status");
    Serial.println("Finger       | Raw V | Filt V | Angle | Status");
    Serial.println("-------------|-------|--------|-------|-------------");

    for (int i = 0; i < numFingers; i++) {
        Serial.printf("%-12s | %.3f | %.3f  | %5.1f | %s\n",
                     fingers[i].getName().c_str(),
                     fingers[i].getRawVoltage(),
                     fingers[i].getFilteredVoltage(),
                     fingers[i].getAngle(),
                     fingers[i].getPositionStatus().c_str());
    }
    Serial.println();
}

void FingerSensorManager::printCalibrationInfo() {
    Serial.println("\nCalibration Information");
    for (int i = 0; i < numFingers; i++) {
        Serial.printf("%s:\n", fingers[i].getName().c_str());
        Serial.printf("  Baseline: %.3f V\n", fingers[i].getBaselineVoltage());
        Serial.printf("  Current:  %.3f V (%.1f°)\n",
                     fingers[i].getFilteredVoltage(),
                     fingers[i].getAngle());
        if (fingers[i].isCalibrated()) {
            Serial.printf("  Flex Min: %.3f V (open)\n", fingers[i].getFlexMin());
            Serial.printf("  Flex Max: %.3f V (closed)\n", fingers[i].getFlexMax());
            Serial.printf("  Normalized: %.2f\n", fingers[i].getNormalizedValue());
        } else {
            Serial.println("  Not calibrated");
        }
        Serial.println();
    }
}

void FingerSensorManager::getNormalizedValues(float* normalized) {
    for (int i = 0; i < numFingers; i++) {
        normalized[i] = fingers[i].getNormalizedValue();
    }
}

bool FingerSensorManager::calibrateOpen(int durationMs) {
    Serial.println("\nOPEN HAND CALIBRATION");
    Serial.println("Open your hand fully and keep it open...");
    Serial.println("Starting in:");

    // Countdown
    for (int i = 3; i > 0; i--) {
        Serial.printf("%d...\n", i);
        delay(1000);
    }
    Serial.println("GO!\n");

    // Initialize accumulators
    float sums[MAX_FINGERS] = {0};
    int sampleCount = 0;

    // Collect samples
    unsigned long startTime = millis();
    while (millis() - startTime < durationMs) {
        // Update all sensors
        for (int i = 0; i < numFingers; i++) {
            fingers[i].update();
            sums[i] += fingers[i].getRawVoltage();
        }
        sampleCount++;

        // Progress indicator
        if (sampleCount % 10 == 0) {
            Serial.print(".");
        }

        delay(10);  // ~100 Hz sampling
    }

    Serial.println("\nDone!\n");

    // Calculate averages and store as flex_min
    if (sampleCount > 0) {
        Serial.println("Open hand values (flex_min):");
        for (int i = 0; i < numFingers; i++) {
            float avg = sums[i] / sampleCount;
            fingers[i].setFlexMin(avg);
            Serial.printf("  %s: %.3f V\n", fingers[i].getName().c_str(), avg);
        }
        Serial.println();
        return true;
    }

    return false;
}

bool FingerSensorManager::calibrateClosed(int durationMs) {
    Serial.println("\nCLOSED FIST CALIBRATION");
    Serial.println("Make a tight fist and keep it closed...");
    Serial.println("Starting in:");

    // Countdown
    for (int i = 3; i > 0; i--) {
        Serial.printf("%d...\n", i);
        delay(1000);
    }
    Serial.println("GO!\n");

    // Initialize accumulators
    float sums[MAX_FINGERS] = {0};
    int sampleCount = 0;

    // Collect samples
    unsigned long startTime = millis();
    while (millis() - startTime < durationMs) {
        // Update all sensors
        for (int i = 0; i < numFingers; i++) {
            fingers[i].update();
            sums[i] += fingers[i].getRawVoltage();
        }
        sampleCount++;

        // Progress indicator
        if (sampleCount % 10 == 0) {
            Serial.print(".");
        }

        delay(10);  // ~100 Hz sampling
    }

    Serial.println("\nDone!\n");

    // Calculate averages and store as flex_max
    if (sampleCount > 0) {
        Serial.println("Closed fist values (flex_max):");
        for (int i = 0; i < numFingers; i++) {
            float avg = sums[i] / sampleCount;
            fingers[i].setFlexMax(avg);
            fingers[i].markCalibrationComplete();
            Serial.printf("  %s: %.3f V\n", fingers[i].getName().c_str(), avg);
        }
        Serial.println();
        return true;
    }

    return false;
}

void FingerSensorManager::runCalibrationRoutine() {
    Serial.println("\nFINGER SENSOR CALIBRATION ROUTINE");
    Serial.println("This will calibrate your flex sensors for your specific hand size.");
    Serial.println("Press any key to start...");

    // Wait for user input
    while (!Serial.available()) {
        delay(100);
    }
    while (Serial.available()) Serial.read();  // Clear buffer

    // Step 1: Open hand
    if (!calibrateOpen(2000)) {
        Serial.println("ERROR: Open hand calibration failed!");
        return;
    }

    Serial.println("Great! Now for the closed fist...");
    Serial.println("Press any key to continue...");
    while (!Serial.available()) {
        delay(100);
    }
    while (Serial.available()) Serial.read();

    // Step 2: Closed fist
    if (!calibrateClosed(2000)) {
        Serial.println("ERROR: Closed fist calibration failed!");
        return;
    }

    // Done!
    Serial.println("\nCALIBRATION COMPLETE!");
    Serial.println("\nCalibration Summary:");
    printCalibrationInfo();

    Serial.println("Your sensors are now calibrated!");
    Serial.println("Normalized values (0-1) will now be available.\n");
}

bool FingerSensorManager::isFullyCalibrated() const {
    for (int i = 0; i < numFingers; i++) {
        if (!fingers[i].isCalibrated()) {
            return false;
        }
    }
    return numFingers > 0;
}

void FingerSensorManager::printNormalizedValues() {
    Serial.println("Normalized Flex Values (0-1)");
    for (int i = 0; i < numFingers; i++) {
        float norm = fingers[i].getNormalizedValue();
        Serial.printf("%s: %.2f ", fingers[i].getName().c_str(), norm);

        // Simple bar
        Serial.print("[");
        int bars = (int)(norm * 20);
        for (int j = 0; j < 20; j++) {
            Serial.print(j < bars ? "#" : " ");
        }
        Serial.println("]");
    }
    Serial.println();
}
