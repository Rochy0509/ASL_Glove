#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "finger_sensors.h"
#include "sensor_types.h"

class MPU9250_Sensor;
class SD_module;

class DataLogger {
public:
    enum class InputMode {
        None,
        Person,
        Label
    };

    DataLogger();

    void begin(FingerSensorManager* manager, MPU9250_Sensor* imuSensor = nullptr, SD_module* sdCard = nullptr);
    void processSerial(bool imuReady, bool fingersReady, bool wifiReady);
    void recordSample(const SensorSample& sample);
    void printHelp() const;

    bool imuDebugEnabled() const { return debugIMU; }
    bool fingerDebugEnabled() const { return debugFingers; }
    bool wifiDebugEnabled() const { return debugWiFi; }
    bool shakeDebugEnabled() const { return debugShake; }
    bool inferenceDebugEnabled() const { return debugInference; }
    bool loggingActive() const { return loggingEnabled; }

private:
    FingerSensorManager* fingerManager;
    MPU9250_Sensor* imuSensor;
    SD_module* sdCard;
    SemaphoreHandle_t configMutex;

    bool loggingEnabled;
    bool headerPrinted;
    bool debugIMU;
    bool debugFingers;
    bool debugWiFi;
    bool debugShake;
    bool debugInference;

    InputMode pendingInput;
    char pendingBuffer[32];
    size_t pendingLength;

    char personId[8];
    char currentLabel[16];

    void startInput(InputMode mode);
    void finalizeInput();
    void storePersonId(const char* value);
    void storeLabel(const char* value);
    void startLogging();
    void stopLogging();
    void printStatus(bool imuReady, bool fingersReady, bool wifiReady);
    void toggleFlag(const char* name, bool& flag);
    void resetHeaderFlag();
};

extern DataLogger dataLogger;
