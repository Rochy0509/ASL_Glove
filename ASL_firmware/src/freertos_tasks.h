#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class MPU9250_Sensor;
class FingerSensorManager;
class I2S_Amplifier;
class SD_module;

struct TaskResources {
    MPU9250_Sensor* imu;
    FingerSensorManager* fingers;
    I2S_Amplifier* amplifier;
    SD_module* sd;
    const char* wifiSsid;
    const char* wifiPassword;
    const char* wifiUsername;  // For WPA2 Enterprise
};

extern TaskHandle_t SensorTaskHandle;
extern TaskHandle_t InferenceTaskHandle;
extern TaskHandle_t LogicTaskHandle;
extern TaskHandle_t TTSTaskHandle;
extern TaskHandle_t AudioTaskHandle;

extern bool gImuAvailable;
extern bool gFingersAvailable;
extern bool gWifiConnected;
extern volatile bool gTTSInProgress;
extern bool gTTSEnabled;

void startSystemTasks(const TaskResources& resources);
