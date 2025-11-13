#ifndef FLEX_SENSORS_H
#define FLEX_SENSORS_H

#include <Arduino.h>

// ADC Pin Definitions for ESP32-S3-DEV-KIT-NXR8
#define THUMB_PIN    3      // ADC1_CH2
#define INDEX_PIN    4      // ADC1_CH3
#define MIDDLE_PIN   5      // ADC1_CH4
#define RING_PIN     6      // ADC1_CH5
#define PINKY_PIN    7      // ADC1_CH6

// Flex sensor calibration values
#define FLEX_STRAIGHT   200  // ADC value when straight
#define FLEX_BENT       800  // ADC value when bent

class FlexSensors {
private:
    // ADC pin assignments
    int8_t thumb_pin;
    int8_t index_pin;
    int8_t middle_pin;
    int8_t ring_pin;
    int8_t pinky_pin;

    // Calibration values
    uint16_t flex_straight;
    uint16_t flex_bent;

    // Raw ADC readings
    uint16_t thumb_raw;
    uint16_t index_raw;
    uint16_t middle_raw;
    uint16_t ring_raw;
    uint16_t pinky_raw;

    // Percentage values (0-100%)
    float thumb_percent;
    float index_percent;
    float middle_percent;
    float ring_percent;
    float pinky_percent;

    bool initialized;

    // Helper function to convert raw ADC to percentage
    float rawToPercent(uint16_t raw_value);

public:
    // Constructor with default pins
    FlexSensors(
        int8_t thumb = THUMB_PIN,
        int8_t index = MIDDLE_PIN,
        int8_t middle = INDEX_PIN,
        int8_t ring = RING_PIN,
        int8_t pinky = PINKY_PIN,



        uint16_t straight = FLEX_STRAIGHT,
        uint16_t bent = FLEX_BENT
    );

    // Initialize ADC channels
    bool begin();

    // Check if initialized
    bool isReady();

    // Update all sensor readings
    void update();

    // Get raw ADC values
    uint16_t getThumbRaw();
    uint16_t getIndexRaw();
    uint16_t getMiddleRaw();
    uint16_t getRingRaw();
    uint16_t getPinkyRaw();

    // Get bend percentage (0-100%)
    float getThumbPercent();
    float getIndexPercent();
    float getMiddlePercent();
    float getRingPercent();
    float getPinkyPercent();

    // Get all readings at once
    void getAllRawReadings(uint16_t& thumb, uint16_t& index, uint16_t& middle, uint16_t& ring, uint16_t& pinky);
    void getAllPercentReadings(float& thumb, float& index, float& middle, float& ring, float& pinky);

    // Calibration functions
    void setCalibrateValues(uint16_t straight, uint16_t bent);
    void printRawReadings();
    void printPercentReadings();
};

#endif // FLEX_SENSORS_H
