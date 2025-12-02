#include "data_logger.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

#include "mpu9250_sensor.h"
#include "freertos_tasks.h"
#include "ml/asl_inference.h"
#include "audio_sd.h"
#include "perf_profiler.h"

DataLogger dataLogger;

namespace {
void trimInPlace(char* buffer) {
    if (!buffer) return;
    size_t len = strlen(buffer);
    size_t start = 0;
    while (start < len && isspace(static_cast<unsigned char>(buffer[start]))) {
        start++;
    }
    size_t end = len;
    while (end > start && isspace(static_cast<unsigned char>(buffer[end - 1]))) {
        end--;
    }
    if (start > 0) {
        memmove(buffer, buffer + start, end - start);
    }
    buffer[end - start] = '\0';
}

void uppercaseInPlace(char* buffer) {
    if (!buffer) return;
    for (size_t i = 0; buffer[i] != '\0'; ++i) {
        buffer[i] = toupper(static_cast<unsigned char>(buffer[i]));
    }
}

void copySafe(char* dest, size_t len, const char* src) {
    if (!dest || !src || len == 0) return;
    size_t copyLen = strlen(src);
    if (copyLen >= len) copyLen = len - 1;
    memcpy(dest, src, copyLen);
    dest[copyLen] = '\0';
}
}  // namespace

DataLogger::DataLogger()
    : fingerManager(nullptr),
      imuSensor(nullptr),
      sdCard(nullptr),
      configMutex(nullptr),
      loggingEnabled(false),
      headerPrinted(false),
      debugIMU(true),
      debugFingers(true),
      debugWiFi(false),
      debugShake(true),
      debugInference(true),
      pendingInput(InputMode::None),
      pendingLength(0) {
    memset(personId, 0, sizeof(personId));
    memset(currentLabel, 0, sizeof(currentLabel));
    memset(pendingBuffer, 0, sizeof(pendingBuffer));
}

void DataLogger::begin(FingerSensorManager* manager, MPU9250_Sensor* imu, SD_module* sd) {
    fingerManager = manager;
    imuSensor = imu;
    sdCard = sd;
    configMutex = xSemaphoreCreateMutex();
    printHelp();
    Serial.println("[DATA] Press 'p' to set person ID, 'l' to set label, 'g' to start logging.");
}

void DataLogger::printHelp() const {
    Serial.println("\nSerial Command Menu");
    Serial.println("i - Toggle IMU debug output");
    Serial.println("f - Toggle Finger sensor debug output");
    Serial.println("w - Toggle WiFi debug output");
    Serial.println("s - Toggle Shake detection debug output");
    Serial.println("m - Toggle inference debug output");
    Serial.println("x - Toggle TTS/shake-triggered speech");
    Serial.println("e - Start ML inference");
    Serial.println("a - Show sensor + logger status");
    Serial.println("u - Run IMU calibration routine");
    Serial.println("c - Show flex calibration info");
    Serial.println("r - Run flex calibration routine");
    Serial.println("n - Show normalized flex values");
    Serial.println("d - Delete TTS cache (clear all .mp3 files)");
    Serial.println("p - Set person ID (e.g. P1, P2)");
    Serial.println("l - Set label (A, B, NEUTRAL, SPACE, etc)");
    Serial.println("g - Start/arm data logging (auto starts after label entry)");
    Serial.println("t - Stop data logging");
    Serial.println("o - Start performance profiling");
    Serial.println("O - Stop profiling and show statistics");
    Serial.println("j - Export profiling data to VCD file on SD card");
    Serial.println("q - Quiet mode (disable all debug prints)");
    Serial.println("v - Verbose mode (enable all debug prints)");
    Serial.println("h/? - Show this help menu\n");
}

void DataLogger::processSerial(bool imuReady, bool fingersReady, bool wifiReady) {
    while (Serial.available()) {
        char incoming = Serial.read();

        if (incoming == '\r' || incoming == '\n') {
            if (pendingInput != InputMode::None) {
                finalizeInput();
            }
            continue;
        }

        if (pendingInput != InputMode::None) {
            if (pendingLength < sizeof(pendingBuffer) - 1) {
                pendingBuffer[pendingLength++] = incoming;
            }
            continue;
        }

        switch (incoming) {
            case 'i':
            case 'I':
                toggleFlag("IMU debug", debugIMU);
                break;
            case 'f':
            case 'F':
                toggleFlag("Finger debug", debugFingers);
                break;
            case 'w':
            case 'W':
                toggleFlag("WiFi debug", debugWiFi);
                break;
            case 's':
            case 'S':
                toggleFlag("Shake debug", debugShake);
                break;
            case 'm':
            case 'M':
                toggleFlag("Inference debug", debugInference);
                break;
            case 'x':
            case 'X':
                gTTSEnabled = !gTTSEnabled;
                Serial.printf("[CMD] TTS queue %s\n", gTTSEnabled ? "ENABLED" : "DISABLED");
                break;
            case 'e':
            case 'E':
                if (aslInference.isReady()) {
                    Serial.println("[CMD] Inference already initialized.");
                } else if (aslInference.begin()) {
                    Serial.println("[CMD] Inference initialized.");
                } else {
                    Serial.println("[CMD] Failed to initialize inference.");
                }
                break;
            case 'a':
            case 'A':
                printStatus(imuReady, fingersReady, wifiReady);
                break;
            case 'c':
            case 'C':
                if (fingerManager) {
                    fingerManager->printCalibrationInfo();
                }
                break;
            case 'd':
            case 'D':
                if (sdCard) {
                    sdCard->clearTTSCache();
                } else {
                    Serial.println("[CMD] SD card not available.");
                }
                break;
            case 'r':
            case 'R':
                if (fingerManager) {
                    fingerManager->runCalibrationRoutine();
                }
                break;
            case 'u':
            case 'U':
                if (imuSensor) {
                    imuSensor->runCalibrationRoutine();
                } else {
                    Serial.println("[DATA] IMU sensor not available for calibration.");
                }
                break;
            case 'n':
            case 'N':
                if (fingerManager) {
                    if (fingerManager->isFullyCalibrated()) {
                        fingerManager->printNormalizedValues();
                    } else {
                        Serial.println("[DATA] Sensors not calibrated. Run 'r' first.");
                    }
                }
                break;
            case 'p':
            case 'P':
                startInput(InputMode::Person);
                break;
            case 'l':
            case 'L':
                startInput(InputMode::Label);
                break;
            case 'g':
            case 'G':
                startLogging();
                break;
            case 't':
            case 'T':
                stopLogging();
                break;
            case 'q':
            case 'Q':
                debugIMU = debugFingers = debugWiFi = debugShake = debugInference = false;
                Serial.println("[CMD] Quiet mode enabled.");
                break;
            case 'v':
            case 'V':
                debugIMU = debugFingers = debugWiFi = debugShake = debugInference = true;
                Serial.println("[CMD] Verbose mode enabled.");
                break;
            case 'o':
                perfProfiler.reset();
                perfProfiler.enable();
                break;
            case 'O':
                perfProfiler.disable();
                perfProfiler.printAllStats();
                break;
            case 'j':
            case 'J':
                if (sdCard) {
                    char filename[64];
                    snprintf(filename, sizeof(filename), "/profiling_%lu.vcd", millis());
                    if (perfProfiler.exportToVCD(filename)) {
                        Serial.printf("[CMD] Profiling data exported to %s\n", filename);
                    } else {
                        Serial.println("[CMD] Failed to export profiling data.");
                    }
                } else {
                    Serial.println("[CMD] SD card not available for VCD export.");
                }
                break;
            case 'h':
            case 'H':
            case '?':
                printHelp();
                break;
            default:
                Serial.printf("[CMD] Unknown command '%c' - press 'h' for help.\n", incoming);
                break;
        }
    }
}

void DataLogger::recordSample(const SensorSample& sample) {
    if (!configMutex) return;

    char personCopy[sizeof(personId)];
    char labelCopy[sizeof(currentLabel)];
    bool needHeader = false;

    if (xSemaphoreTake(configMutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    bool active = loggingEnabled && personId[0] != '\0' && currentLabel[0] != '\0';
    if (active) {
        copySafe(personCopy, sizeof(personCopy), personId);
        copySafe(labelCopy, sizeof(labelCopy), currentLabel);
        if (!headerPrinted) {
            headerPrinted = true;
            needHeader = true;
        }
    }

    xSemaphoreGive(configMutex);

    if (!active) {
        return;
    }

    if (needHeader) {
        Serial.println("person_id,label,timestamp,flex1,flex2,flex3,flex4,flex5,ax_norm,ay_norm,az_norm,gx_norm,gy_norm,gz_norm");
    }

    Serial.printf(
        "%s,%s,%lu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
        personCopy,
        labelCopy,
        static_cast<unsigned long>(sample.timestampMs),
        sample.flex[0],
        sample.flex[1],
        sample.flex[2],
        sample.flex[3],
        sample.flex[4],
        sample.accelNorm[0],
        sample.accelNorm[1],
        sample.accelNorm[2],
        sample.gyroNorm[0],
        sample.gyroNorm[1],
        sample.gyroNorm[2]);
}

void DataLogger::startInput(InputMode mode) {
    pendingInput = mode;
    pendingLength = 0;
    memset(pendingBuffer, 0, sizeof(pendingBuffer));

    if (mode == InputMode::Person) {
        Serial.println("\n[DATA] Enter person ID (e.g. P1, P2) and press ENTER:");
    } else if (mode == InputMode::Label) {
        Serial.println("\n[DATA] Enter label (A-Z, NEUTRAL, SPACE, etc) and press ENTER:");
    }
}

void DataLogger::finalizeInput() {
    pendingBuffer[pendingLength] = '\0';
    trimInPlace(pendingBuffer);

    if (pendingBuffer[0] == '\0') {
        Serial.println("[DATA] Input cancelled.");
    } else if (pendingInput == InputMode::Person) {
        uppercaseInPlace(pendingBuffer);
        storePersonId(pendingBuffer);
    } else if (pendingInput == InputMode::Label) {
        uppercaseInPlace(pendingBuffer);
        storeLabel(pendingBuffer);
    }

    pendingInput = InputMode::None;
    pendingLength = 0;
    memset(pendingBuffer, 0, sizeof(pendingBuffer));
}

void DataLogger::storePersonId(const char* value) {
    if (!configMutex) return;
    if (xSemaphoreTake(configMutex, portMAX_DELAY) != pdTRUE) return;
    copySafe(personId, sizeof(personId), value);
    xSemaphoreGive(configMutex);
    Serial.printf("[DATA] Person ID set to %s\n", personId);
}

void DataLogger::storeLabel(const char* value) {
    if (!configMutex) return;
    if (xSemaphoreTake(configMutex, portMAX_DELAY) != pdTRUE) return;
    copySafe(currentLabel, sizeof(currentLabel), value);
    headerPrinted = false;
    bool ready = fingerManager && fingerManager->isFullyCalibrated() && personId[0] != '\0';
    loggingEnabled = ready;
    if (ready) {
        debugIMU = false;
        debugFingers = false;
        debugShake = false;
        debugWiFi = false;
    }
    xSemaphoreGive(configMutex);

    if (!fingerManager || !fingerManager->isFullyCalibrated()) {
        Serial.println("[DATA] Label set, but sensors are not calibrated yet. Run 'r'.");
    } else if (personId[0] == '\0') {
        Serial.println("[DATA] Label stored. Set person ID before logging.");
    } else {
        Serial.printf("[DATA] Label set to %s. Logging %s.\n",
                      currentLabel,
                      loggingEnabled ? "ENABLED" : "DISABLED");
        if (loggingEnabled) {
            Serial.println("[DATA] Debug output muted while logging for clean CSV.");
        }
    }
}

void DataLogger::startLogging() {
    if (!configMutex) return;
    if (!fingerManager || !fingerManager->isFullyCalibrated()) {
        Serial.println("[DATA] Cannot start logging until flex sensors are calibrated ('r').");
        return;
    }

    if (personId[0] == '\0') {
        Serial.println("[DATA] Set person ID first ('p').");
        return;
    }

    if (currentLabel[0] == '\0') {
        Serial.println("[DATA] Set a label first ('l').");
        return;
    }

    if (xSemaphoreTake(configMutex, portMAX_DELAY) != pdTRUE) return;
    loggingEnabled = true;
    headerPrinted = false;
    debugIMU = false;
    debugFingers = false;
    debugShake = false;
    debugWiFi = false;
    xSemaphoreGive(configMutex);

    Serial.printf("[DATA] Logging enabled for %s label %s (50 Hz). Use 't' to stop.\n",
                  personId,
                  currentLabel);
    Serial.println("[DATA] Debug output muted while logging for clean CSV.");
}

void DataLogger::stopLogging() {
    if (!configMutex) return;
    if (xSemaphoreTake(configMutex, portMAX_DELAY) != pdTRUE) return;
    loggingEnabled = false;
    xSemaphoreGive(configMutex);
    Serial.println("[DATA] Logging stopped.");
}

void DataLogger::printStatus(bool imuReady, bool fingersReady, bool wifiReady) {
    Serial.println("\nSensor Status");
    Serial.printf("IMU: %s\n", imuReady ? "READY" : "NOT AVAILABLE");
    Serial.printf("Finger Sensors: %s\n", fingersReady ? "READY" : "NOT READY");
    Serial.printf("WiFi: %s\n", wifiReady ? "Connected" : "Offline");

    if (configMutex && xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
        Serial.printf("Logger Person ID: %s\n", personId[0] ? personId : "(not set)");
        Serial.printf("Logger Label: %s\n", currentLabel[0] ? currentLabel : "(not set)");
        Serial.printf("Logging: %s\n", loggingEnabled ? "ENABLED" : "DISABLED");
        xSemaphoreGive(configMutex);
    }

    if (fingerManager) {
        fingerManager->printStatus();
    }
    if (imuSensor) {
        imuSensor->printCalibrationInfo();
    }
    Serial.println();
}

void DataLogger::toggleFlag(const char* name, bool& flag) {
    flag = !flag;
    Serial.printf("[CMD] %s: %s\n", name, flag ? "ON" : "OFF");
}

void DataLogger::resetHeaderFlag() {
    if (!configMutex) return;
    if (xSemaphoreTake(configMutex, portMAX_DELAY) != pdTRUE) return;
    headerPrinted = false;
    xSemaphoreGive(configMutex);
}
