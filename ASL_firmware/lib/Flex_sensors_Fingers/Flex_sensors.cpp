#include "Flex_sensors.h"

FlexSensors::FlexSensors(
    int8_t thumb, int8_t index, int8_t middle, int8_t ring, int8_t pinky,
    uint16_t straight, uint16_t bent)
    : thumb_pin(thumb), index_pin(index), middle_pin(middle), ring_pin(ring), pinky_pin(pinky),
      flex_straight(straight), flex_bent(bent), initialized(false),
      thumb_raw(0), index_raw(0), middle_raw(0), ring_raw(0), pinky_raw(0),
      thumb_percent(0.0f), index_percent(0.0f), middle_percent(0.0f), ring_percent(0.0f), pinky_percent(0.0f) {
}

bool FlexSensors::begin() {
    // Configure ADC pins as inputs
    pinMode(thumb_pin, INPUT);
    pinMode(index_pin, INPUT);
    pinMode(middle_pin, INPUT);
    pinMode(ring_pin, INPUT);
    pinMode(pinky_pin, INPUT);

    // Read initial values to verify pins are working
    analogReadResolution(12);  // 12-bit ADC resolution (0-4095)
    delay(100);

    // Verify we can read from all pins
    thumb_raw = analogRead(thumb_pin);
    index_raw = analogRead(index_pin);
    middle_raw = analogRead(middle_pin);
    ring_raw = analogRead(ring_pin);
    pinky_raw = analogRead(pinky_pin);

    initialized = true;
    return true;
}

bool FlexSensors::isReady() {
    return initialized;
}

float FlexSensors::rawToPercent(uint16_t raw_value) {
    // Clamp raw value to calibration range
    if (raw_value < flex_straight) {
        return 0.0f;
    }
    if (raw_value > flex_bent) {
        return 100.0f;
    }

    // Linear interpolation between straight and bent
    float percent = ((float)(raw_value - flex_straight) / (float)(flex_bent - flex_straight)) * 100.0f;
    return percent;
}

void FlexSensors::update() {
    if (!initialized) return;

    // Read all analog inputs
    thumb_raw = analogRead(thumb_pin);
    index_raw = analogRead(index_pin);
    middle_raw = analogRead(middle_pin);
    ring_raw = analogRead(ring_pin);
    pinky_raw = analogRead(pinky_pin);

    // Convert to percentages
    thumb_percent = rawToPercent(thumb_raw);
    index_percent = rawToPercent(index_raw);
    middle_percent = rawToPercent(middle_raw);
    ring_percent = rawToPercent(ring_raw);
    pinky_percent = rawToPercent(pinky_raw);
}

// Get raw ADC values
uint16_t FlexSensors::getThumbRaw() {
    return thumb_raw;
}

uint16_t FlexSensors::getIndexRaw() {
    return index_raw;
}

uint16_t FlexSensors::getMiddleRaw() {
    return middle_raw;
}

uint16_t FlexSensors::getRingRaw() {
    return ring_raw;
}

uint16_t FlexSensors::getPinkyRaw() {
    return pinky_raw;
}

// Get bend percentage (0-100%)
float FlexSensors::getThumbPercent() {
    return thumb_percent;
}

float FlexSensors::getIndexPercent() {
    return index_percent;
}

float FlexSensors::getMiddlePercent() {
    return middle_percent;
}

float FlexSensors::getRingPercent() {
    return ring_percent;
}

float FlexSensors::getPinkyPercent() {
    return pinky_percent;
}

void FlexSensors::getAllRawReadings(uint16_t& thumb, uint16_t& index, uint16_t& middle, uint16_t& ring, uint16_t& pinky) {
    thumb = thumb_raw;
    index = index_raw;
    middle = middle_raw;
    ring = ring_raw;
    pinky = pinky_raw;
}

void FlexSensors::getAllPercentReadings(float& thumb, float& index, float& middle, float& ring, float& pinky) {
    thumb = thumb_percent;
    index = index_percent;
    middle = middle_percent;
    ring = ring_percent;
    pinky = pinky_percent;
}

void FlexSensors::setCalibrateValues(uint16_t straight, uint16_t bent) {
    flex_straight = straight;
    flex_bent = bent;
}

void FlexSensors::printRawReadings() {
    Serial.printf("[FLEX] Thumb: %4d  Index: %4d  Middle: %4d  Ring: %4d  Pinky: %4d\n",
                  thumb_raw, index_raw, middle_raw, ring_raw, pinky_raw);
}

void FlexSensors::printPercentReadings() {
    Serial.printf("[FLEX] Thumb: %6.2f%%  Index: %6.2f%%  Middle: %6.2f%%  Ring: %6.2f%%  Pinky: %6.2f%%\n",
                  thumb_percent, index_percent, middle_percent, ring_percent, pinky_percent);
}
