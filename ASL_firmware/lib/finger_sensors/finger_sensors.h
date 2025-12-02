#ifndef FINGER_SENSORS_H
#define FINGER_SENSORS_H

#include <Arduino.h>

// Configuration Constants
#define MAX_FINGERS 5
#define MAX_FILTER_SIZE 10
#define MAX_CALIB_POINTS 6
#define BASELINE_SAMPLES 20

// ADC Configuration
#define ADC_RESOLUTION 12
#define ADC_MAX 4095.0f
#define V_REF 3.3f

// Calibration Point Structure
struct CalibrationPoint {
    float voltage;    // Voltage reading
    float angle;      // Corresponding angle in degrees
};

// Moving Average Filter Class
class MovingAverageFilter {
private:
    float buffer[MAX_FILTER_SIZE];
    int size;
    int index;
    float sum;
    int count;
    float lastOutput;
    float deadband;

public:
    MovingAverageFilter(int windowSize = 5, float deadbandThreshold = 0.02f);

    float add(float value);
    void reset();
    float getLastOutput() const { return lastOutput; }
    void setDeadband(float db) { deadband = db; }
};

// Finger Sensor Class
class FingerSensor {
private:
    // Hardware
    uint8_t pin;
    String name;

    // Filtering
    MovingAverageFilter filter;

    // Calibration
    CalibrationPoint calibration[MAX_CALIB_POINTS];
    int numCalibPoints;

    // Baseline tracking
    float baselineVoltage;
    bool baselineEstablished;
    int baselineCount;
    float baselineSum;

    // Person-specific calibration (min/max normalization)
    float flex_min;
    float flex_max;
    bool calibrationComplete;

    // State
    float lastRawVoltage;
    float lastFilteredVoltage;
    float lastAngle;
    float lastNormalized;

    // Helper functions
    float readVoltage();
    float voltageToAngle(float voltage);

public:
    FingerSensor();

    // Initialization
    void begin(uint8_t analogPin, const String& fingerName, int filterSize = 5);

    // Calibration setup
    void setCalibration(const CalibrationPoint* points, int count);
    void setDefaultCalibration();  // Use default calibration curve

    // Baseline management
    bool updateBaseline();  // Returns true when baseline is established
    void resetBaseline();
    bool isBaselineReady() const { return baselineEstablished; }
    float getBaselineVoltage() const { return baselineVoltage; }

    // Main update function
    void update();

    // Data access
    float getRawVoltage() const { return lastRawVoltage; }
    float getFilteredVoltage() const { return lastFilteredVoltage; }
    float getAngle() const { return lastAngle; }
    float getNormalizedValue() const { return lastNormalized; }
    String getName() const { return name; }

    // Person-specific calibration
    void setFlexMin(float min) { flex_min = min; }
    void setFlexMax(float max) { flex_max = max; }
    float getFlexMin() const { return flex_min; }
    float getFlexMax() const { return flex_max; }
    bool isCalibrated() const { return calibrationComplete; }
    void markCalibrationComplete() { calibrationComplete = true; }

    // Status helpers
    String getPositionStatus() const;
    bool isExtended() const { return lastAngle < 15.0f; }
    bool isBent() const { return lastAngle >= 75.0f; }
    bool isClosed() const { return lastAngle >= 150.0f; }
};

// Finger Sensor Manager
class FingerSensorManager {
private:
    FingerSensor fingers[MAX_FINGERS];
    int numFingers;
    bool baselineComplete;

public:
    FingerSensorManager();

    // Setup
    void begin();
    int addFinger(uint8_t pin, const String& name, int filterSize = 5);
    void setFingerCalibration(int fingerIndex, const CalibrationPoint* points, int count);

    // Baseline calibration
    bool establishBaseline();  // Returns true when all baselines ready
    void resetAllBaselines();
    bool isBaselineComplete() const { return baselineComplete; }

    // Update all sensors
    void updateAll();

    // Data access
    FingerSensor* getFinger(int index);
    int getNumFingers() const { return numFingers; }

    // Bulk data access
    void getAngles(float* angles);
    void getFilteredVoltages(float* voltages);
    void getNormalizedValues(float* normalized);

    // Person-specific calibration routine
    bool calibrateOpen(int durationMs = 2000);    // Calibrate open hand (flex_min)
    bool calibrateClosed(int durationMs = 2000);  // Calibrate closed fist (flex_max)
    void runCalibrationRoutine();                 // Full interactive routine
    bool isFullyCalibrated() const;               // Check if all fingers calibrated

    // Status
    void printStatus();
    void printCalibrationInfo();
    void printNormalizedValues();
};

#endif // FINGER_SENSORS_H
