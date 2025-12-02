#include "freertos_tasks.h"

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <math.h>
#include <string.h>
#include <esp_wpa2.h>

#include <algorithm>
#include <freertos/queue.h>

#include "audio_sd.h"
#include "data_logger.h"
#include "finger_sensors.h"
#include "i2s_amp.h"
#include "mpu9250_sensor.h"
#include "ml/asl_inference.h"
#include "ml/imu_normalization.h"
#include "sensor_types.h"
#include "perf_profiler.h"

/*
 FreeRTOS Task Overview
 -------------------------------------------------------------------------------
 [Core 0 | Prio 4] SensorTask    - 50 Hz sampling for IMU + flex, fills windows,
                                  pushes samples to logger/logic queues.
 [Core 0 | Prio 3] InferenceTask - Builds window features, runs classify_letter,
                                  forwards letter decisions.
 [Core 1 | Prio 2] LogicTask     - Serial console, letter state machine, shake
                                  detection, queues TTS requests.
 [Core 1 | Prio 2] TTSTask       - Wi-Fi + Google TTS downloads, feeds AudioTask.
 [Core 1 | Prio 3] AudioTask     - High-priority I2S playback loop from SD files.
*/

namespace {
constexpr size_t SENSOR_WINDOW_SIZE = 25;
constexpr TickType_t SENSOR_PERIOD = pdMS_TO_TICKS(20);
constexpr float GYRO_SHAKE_THRESH = 3.5f;
constexpr size_t SHAKE_BUFFER_SIZE = 25;
constexpr size_t SHAKE_COUNT_THRESHOLD = 18;
constexpr uint32_t SHAKE_COOLDOWN_MS = 1500;
constexpr uint32_t LETTER_HOLD_MS = 200;
constexpr size_t MAX_TEXT_BUFFER = 64;
constexpr float MIN_CONFIDENCE_THRESHOLD = 0.85f;

struct SensorWindow {
    SensorSample samples[SENSOR_WINDOW_SIZE];
};

struct LetterDecision {
    char letter;
    float confidence;
    uint32_t timestamp;
    int classIndex;
};

struct TTSRequest {
    char text[128];
};

struct AudioJob {
    char filepath[32];
};

class ShakeDetector {
public:
    void addSample(float magnitude) {
        buffer[index] = magnitude;
        index = (index + 1) % SHAKE_BUFFER_SIZE;
        if (count < SHAKE_BUFFER_SIZE) {
            count++;
        }
    }

    bool triggered() {
        if (count < SHAKE_BUFFER_SIZE) return false;

        size_t above = 0;
        for (size_t i = 0; i < SHAKE_BUFFER_SIZE; ++i) {
            if (buffer[i] > GYRO_SHAKE_THRESH) {
                above++;
            }
        }

        uint32_t now = millis();
        if (above >= SHAKE_COUNT_THRESHOLD && (now - lastTriggerMs) > SHAKE_COOLDOWN_MS) {
            lastTriggerMs = now;
            return true;
        }
        return false;
    }

    uint32_t cooldownRemaining() const {
        uint32_t now = millis();
        if (now - lastTriggerMs >= SHAKE_COOLDOWN_MS) return 0;
        return SHAKE_COOLDOWN_MS - (now - lastTriggerMs);
    }

private:
    float buffer[SHAKE_BUFFER_SIZE]{0};
    size_t index{0};
    size_t count{0};
    uint32_t lastTriggerMs{0};
};

// Global Resources
TaskResources gResources;
QueueHandle_t sensorSampleQueue = nullptr;
QueueHandle_t sensorWindowQueue = nullptr;
QueueHandle_t letterDecisionQueue = nullptr;
QueueHandle_t ttsRequestQueue = nullptr;
QueueHandle_t audioJobQueue = nullptr;

char classifyLetter(const SensorWindow& window, float& confidence, int& classIndex) {
    char letter = ASLInferenceEngine::kNeutralToken;
    confidence = 0.0f;
    classIndex = -1;

    if (!aslInference.isReady()) {
        return letter;
    }

    aslInference.classify(window.samples, SENSOR_WINDOW_SIZE, letter, confidence, classIndex);
    return letter;
}

bool enqueueTTSRequest(const char* text) {
    if (!ttsRequestQueue || !text) return false;
    TTSRequest req{};
    snprintf(req.text, sizeof(req.text), "%s ", text);
    return xQueueSend(ttsRequestQueue, &req, pdMS_TO_TICKS(100)) == pdPASS;
}

bool connectWiFi(const TaskResources& resources) {
    if (!resources.wifiSsid || !resources.wifiPassword) {
        Serial.println("[TTSTask] WiFi credentials missing.");
        return false;
    }

    if (WiFi.status() == WL_CONNECTED) {
        gWifiConnected = true;
        return true;
    }

    // Allow WiFi resources to clean up
    vTaskDelay(pdMS_TO_TICKS(300));

    WiFi.mode(WIFI_STA);

    // Check if WPA2 Enterprise
    if (resources.wifiUsername && strlen(resources.wifiUsername) > 0) {
        Serial.println("[TTSTask] Configuring WPA2 Enterprise...");
        Serial.printf("[TTSTask] SSID: %s\n", resources.wifiSsid);
        Serial.printf("[TTSTask] Username: %s\n", resources.wifiUsername);

        // Disconnect if previously connected
        WiFi.disconnect(true);
        vTaskDelay(pdMS_TO_TICKS(500));

        // Disable WPA2 Enterprise first
        esp_wifi_sta_wpa2_ent_disable();
        vTaskDelay(pdMS_TO_TICKS(100));

        // Set WPA2 Enterprise credentials
        esp_wifi_sta_wpa2_ent_set_identity((uint8_t*)resources.wifiUsername, strlen(resources.wifiUsername));
        esp_wifi_sta_wpa2_ent_set_username((uint8_t*)resources.wifiUsername, strlen(resources.wifiUsername));
        esp_wifi_sta_wpa2_ent_set_password((uint8_t*)resources.wifiPassword, strlen(resources.wifiPassword));

        // Enable WPA2 Enterprise
        Serial.println("[TTSTask] Enabling WPA2 Enterprise...");
        esp_wifi_sta_wpa2_ent_enable();

        // Connect to the network
        Serial.println("[TTSTask] Starting WiFi connection...");
        WiFi.begin(resources.wifiSsid);
    } else {
        // Standard WPA2-Personal
        Serial.println("[TTSTask] Configuring WPA2-Personal...");
        WiFi.begin(resources.wifiSsid, resources.wifiPassword);
    }

    const uint32_t start = millis();
    Serial.print("[TTSTask] Waiting for connection");
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
        vTaskDelay(pdMS_TO_TICKS(250));
        Serial.print(".");
        Serial.printf(" (Status: %d)", WiFi.status());
    }
    Serial.println();

    gWifiConnected = (WiFi.status() == WL_CONNECTED);
    if (gWifiConnected) {
        if (dataLogger.wifiDebugEnabled()) {
            Serial.printf("[TTSTask] WiFi connected, IP: %s\n",
                          WiFi.localIP().toString().c_str());
        }
        if (resources.sd) {
            resources.sd->setStatusLED(0, 0, 255);
            vTaskDelay(pdMS_TO_TICKS(200));
            resources.sd->clearStatusLED();
        }
    } else {
        Serial.println("[TTSTask] WiFi connection failed.");
    }
    return gWifiConnected;
}

void disconnectWiFi() {
    WiFi.disconnect(true, true);
    vTaskDelay(pdMS_TO_TICKS(100));
    WiFi.mode(WIFI_OFF);
    gWifiConnected = false;
}

void reinitI2C() {
    Wire.end();
    vTaskDelay(pdMS_TO_TICKS(100));
    Wire.begin(18, 46, 100000);
    Wire.setTimeout(1000);
    vTaskDelay(pdMS_TO_TICKS(50));
}

}  // namespace

TaskHandle_t SensorTaskHandle = nullptr;
TaskHandle_t InferenceTaskHandle = nullptr;
TaskHandle_t LogicTaskHandle = nullptr;
TaskHandle_t TTSTaskHandle = nullptr;
TaskHandle_t AudioTaskHandle = nullptr;

bool gImuAvailable = false;
bool gFingersAvailable = false;
bool gWifiConnected = false;
volatile bool gTTSInProgress = false;
volatile uint32_t gLastTTSCompleteTime = 0;
volatile char gLastPlayedWord[32] = "";
constexpr uint32_t TTS_COOLDOWN_MS = 1500;
bool gTTSEnabled = false;

void SensorTask(void* parameter) {
    Serial.println("[SensorTask] Starting on Core 0");
    TickType_t lastWake = xTaskGetTickCount();

    static SensorWindow rollingWindow;
    static SensorWindow snapshot;
    size_t windowIndex = 0;
    bool windowPrimed = false;

    while (true) {
        perfProfiler.markStart(MARKER_SENSOR_READ);
        
        SensorSample sample{};
        sample.timestampMs = millis();

        if (gFingersAvailable && gResources.fingers) {
            perfProfiler.markStart(MARKER_FINGER_UPDATE);
            gResources.fingers->updateAll();
            gResources.fingers->getNormalizedValues(sample.flex);
            sample.fingersValid = true;
            perfProfiler.markEnd(MARKER_FINGER_UPDATE);
        }

        if (gImuAvailable && gResources.imu && gResources.imu->isReady() && !gTTSInProgress) {
            perfProfiler.markStart(MARKER_IMU_UPDATE);
            gResources.imu->update();
            sample.accel[0] = gResources.imu->getAccelX_mss();
            sample.accel[1] = gResources.imu->getAccelY_mss();
            sample.accel[2] = gResources.imu->getAccelZ_mss();
            sample.gyro[0] = gResources.imu->getGyroX_rads();
            sample.gyro[1] = gResources.imu->getGyroY_rads();
            sample.gyro[2] = gResources.imu->getGyroZ_rads();
            if (gResources.imu->isCalibrated()) {
                gResources.imu->getNormalizedReadings(sample.accelNorm, sample.gyroNorm);
            } else {
                sample.accelNorm[0] = normalizeSensor(sample.accel[0], kAxParams);
                sample.accelNorm[1] = normalizeSensor(sample.accel[1], kAyParams);
                sample.accelNorm[2] = normalizeSensor(sample.accel[2], kAzParams);
                sample.gyroNorm[0] = normalizeSensor(sample.gyro[0], kGxParams);
                sample.gyroNorm[1] = normalizeSensor(sample.gyro[1], kGyParams);
                sample.gyroNorm[2] = normalizeSensor(sample.gyro[2], kGzParams);
            }
            sample.imuValid = true;
            perfProfiler.markEnd(MARKER_IMU_UPDATE);
        }
        
        perfProfiler.markEnd(MARKER_SENSOR_READ);

        dataLogger.recordSample(sample);

        if (sensorSampleQueue) {
            xQueueSend(sensorSampleQueue, &sample, 0);
        }

        rollingWindow.samples[windowIndex] = sample;
        windowIndex = (windowIndex + 1) % SENSOR_WINDOW_SIZE;
        if (!windowPrimed && windowIndex == 0) {
            windowPrimed = true;
        }

        if (windowPrimed && sensorWindowQueue) {
            perfProfiler.markStart(MARKER_WINDOW_BUILD);
            for (size_t i = 0; i < SENSOR_WINDOW_SIZE; ++i) {
                size_t idx = (windowIndex + i) % SENSOR_WINDOW_SIZE;
                snapshot.samples[i] = rollingWindow.samples[idx];
            }
            xQueueOverwrite(sensorWindowQueue, &snapshot);
            perfProfiler.markEnd(MARKER_WINDOW_BUILD);
            if (InferenceTaskHandle) {
                xTaskNotifyGive(InferenceTaskHandle);
            }
        }

        if (dataLogger.imuDebugEnabled() && sample.imuValid) {
            static uint32_t lastPrint = 0;
            if (millis() - lastPrint >= 500) {
                lastPrint = millis();
                Serial.printf("[IMU] A: %.2f %.2f %.2f | G: %.2f %.2f %.2f\n",
                              sample.accel[0],
                              sample.accel[1],
                              sample.accel[2],
                              sample.gyro[0],
                              sample.gyro[1],
                              sample.gyro[2]);
            }
        }

        if (dataLogger.fingerDebugEnabled() && gFingersAvailable && gResources.fingers) {
            static uint32_t lastFingerPrint = 0;
            if (millis() - lastFingerPrint >= 1000) {
                lastFingerPrint = millis();
                float angles[5];
                gResources.fingers->getAngles(angles);
                Serial.printf("[FINGERS] T:%.0f I:%.0f M:%.0f R:%.0f P:%.0f\n",
                              angles[4],
                              angles[3],
                              angles[2],
                              angles[1],
                              angles[0]);
            }
        }

        vTaskDelayUntil(&lastWake, SENSOR_PERIOD);
    }
}

void InferenceTask(void* parameter) {
    Serial.println("[InferenceTask] Starting on Core 0");
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        SensorWindow window;
        if (!sensorWindowQueue ||
            xQueueReceive(sensorWindowQueue, &window, 0) != pdPASS) {
            continue;
        }

        perfProfiler.markStart(MARKER_INFERENCE);
        float confidence = 0.0f;
        int classIndex = -1;
        char letter = classifyLetter(window, confidence, classIndex);
        perfProfiler.markEnd(MARKER_INFERENCE);

        if (!dataLogger.loggingActive() && dataLogger.inferenceDebugEnabled()) {
            static uint32_t lastPrintMs = 0;
            static int lastPrintClass = -2;
            static char lastPrintLetter = '\0';
            const uint32_t now = millis();
            const bool changed = (classIndex != lastPrintClass) || (letter != lastPrintLetter);
            if (changed || (now - lastPrintMs) >= 100) {
                lastPrintMs = now;
                lastPrintClass = classIndex;
                lastPrintLetter = letter;

                const char* label = (classIndex >= 0)
                                        ? aslInference.labelForIndex(static_cast<size_t>(classIndex))
                                        : nullptr;
                if (!label || !label[0]) {
                    if (letter == ASLInferenceEngine::kBackspaceToken) {
                        label = "BACKSPACE";
                    } else if (letter == ASLInferenceEngine::kSpaceToken) {
                        label = "SPACE";
                    } else if (letter == ASLInferenceEngine::kNeutralToken) {
                        label = "NEUTRAL";
                    } else {
                        label = "?";
                    }
                }
                const char* confMarker = (confidence < MIN_CONFIDENCE_THRESHOLD) ? " [LOW]" : "";
                Serial.printf("[Inference] Label: %s | Letter: %c | Confidence: %.2f%s\n",
                              label,
                              (letter == ASLInferenceEngine::kSpaceToken)
                                  ? ' '
                                  : (letter == ASLInferenceEngine::kNeutralToken ? '-' : letter),
                              confidence,
                              confMarker);
            }
        }

        if (letterDecisionQueue) {
            LetterDecision decision{
                .letter = letter,
                .confidence = confidence,
                .timestamp = millis(),
                .classIndex = classIndex};
            xQueueSend(letterDecisionQueue, &decision, 0);
        }

        taskYIELD();
        vTaskDelay(1);
    }
}

void LogicTask(void* parameter) {
    Serial.println("[LogicTask] Starting on Core 1");

    ShakeDetector shakeDetector;
    enum class LetterState { Neutral, LetterHeld, WaitNeutral };
    LetterState state = LetterState::Neutral;
    char heldLetter = '\0';
    uint32_t holdStart = 0;
    uint32_t lastCommitMs = 0;
    String textBuffer;
    char lastCommittedLetter = ASLInferenceEngine::kNeutralToken;
    constexpr uint32_t LETTER_COOLDOWN_MS = 200;
    auto commitToBuffer = [&](char value, int classIndex) {
        if (value == ASLInferenceEngine::kNeutralToken) {
            return;
        }

        const uint32_t now = millis();
        const char* fullLabel = (classIndex >= 0) ? aslInference.labelForIndex(static_cast<size_t>(classIndex)) : nullptr;

        // Block same word during TTS cooldown
        if (gLastTTSCompleteTime > 0 && (now - gLastTTSCompleteTime) < TTS_COOLDOWN_MS) {
            if (fullLabel && strcmp(fullLabel, (const char*)gLastPlayedWord) == 0) {
                return;
            }
        }
        if (value != ASLInferenceEngine::kBackspaceToken) {
            if (value == lastCommittedLetter &&
                (now - lastCommitMs) < LETTER_COOLDOWN_MS) {
                return;
            }
            lastCommitMs = now;
        } else {
            lastCommitMs = now;
        }

        if (value == ASLInferenceEngine::kBackspaceToken) {
            if (textBuffer.length() > 0) {
                textBuffer.remove(textBuffer.length() - 1);
            }
            lastCommittedLetter = ASLInferenceEngine::kNeutralToken;
        } else {
            const char* fullLabel = (classIndex >= 0) ? aslInference.labelForIndex(static_cast<size_t>(classIndex)) : nullptr;

            if (fullLabel && fullLabel[0] != '\0' &&
                strcmp(fullLabel, "NEUTRAL") != 0 &&
                strcmp(fullLabel, "BACKSPACE") != 0 &&
                strcmp(fullLabel, "SPACE") != 0) {
                textBuffer = String(fullLabel) + " ";
            } else if (value == ASLInferenceEngine::kSpaceToken) {
                textBuffer += ' ';
            } else {
                textBuffer += value;
            }
            lastCommittedLetter = value;
        }

        if (!dataLogger.loggingActive()) {
            const char* label = (classIndex >= 0)
                                    ? aslInference.labelForIndex(static_cast<size_t>(classIndex))
                                    : nullptr;
            if (!label || !label[0]) {
                if (value == ASLInferenceEngine::kBackspaceToken) {
                    label = "BACKSPACE";
                } else if (value == ASLInferenceEngine::kSpaceToken) {
                    label = "SPACE";
                } else if (value == ASLInferenceEngine::kNeutralToken) {
                    label = "NEUTRAL";
                } else {
                    label = "?";
                }
            }
            Serial.printf("[LogicTask] Letter committed: %s | Buffer: %s\n",
                          label,
                          textBuffer.c_str());
        }
    };

    while (true) {
        if (sensorSampleQueue) {
            SensorSample sample;
            if (xQueueReceive(sensorSampleQueue, &sample, pdMS_TO_TICKS(5)) == pdPASS) {
                if (sample.imuValid) {
                    perfProfiler.markStart(MARKER_SHAKE_DETECT);
                    float mag = sqrtf(sample.gyro[0] * sample.gyro[0] +
                                      sample.gyro[1] * sample.gyro[1] +
                                      sample.gyro[2] * sample.gyro[2]);
                    shakeDetector.addSample(mag);
                    perfProfiler.markEnd(MARKER_SHAKE_DETECT);
                    if (dataLogger.shakeDebugEnabled()) {
                        static uint32_t lastShakePrint = 0;
                        if (millis() - lastShakePrint >= 1000) {
                            lastShakePrint = millis();
                            Serial.printf("[SHAKE] Mag: %.2f\n", mag);
                        }
                    }
                }

                const bool shakeFired = shakeDetector.triggered();

                if (gTTSEnabled && shakeFired) {
                    if (dataLogger.shakeDebugEnabled()) {
                        Serial.println("[LogicTask] Shake detected.");
                    }

                    if (textBuffer.length() > 0) {
                        if (!gTTSInProgress) {
                            if (enqueueTTSRequest(textBuffer.c_str())) {
                                if (!dataLogger.loggingActive()) {
                                    Serial.printf("[LogicTask] Queued TTS for \"%s\"\n",
                                                  textBuffer.c_str());
                                }
                                textBuffer = "";
                            } else if (!dataLogger.loggingActive()) {
                                Serial.println("[LogicTask] TTS queue full.");
                            }
                        } else {
                            if (!dataLogger.loggingActive()) {
                                Serial.println("[LogicTask] TTS in progress, skipping queue.");
                            }
                        }
                    } else if (dataLogger.shakeDebugEnabled()) {
                        Serial.println("[LogicTask] Shake ignored (buffer empty).");
                    }
                } else if (!gTTSEnabled && shakeFired && dataLogger.shakeDebugEnabled()) {
                    Serial.println("[LogicTask] Shake detected but TTS disabled.");
                }
            }
        }

        if (letterDecisionQueue) {
            LetterDecision decision;
            if (xQueueReceive(letterDecisionQueue, &decision, 0) == pdPASS) {
                // Treat low-confidence predictions as neutral
                const bool isNeutralDecision =
                    (decision.letter == ASLInferenceEngine::kNeutralToken) ||
                    (decision.confidence < MIN_CONFIDENCE_THRESHOLD);

                switch (state) {
                    case LetterState::Neutral:
                        if (!isNeutralDecision) {
                            heldLetter = decision.letter;
                            holdStart = millis();
                            state = LetterState::LetterHeld;
                        }
                        break;
                    case LetterState::LetterHeld:
                        if (decision.letter == heldLetter) {
                            if (millis() - holdStart >= LETTER_HOLD_MS) {
                                perfProfiler.markStart(MARKER_LETTER_COMMIT);
                                commitToBuffer(heldLetter, decision.classIndex);
                                perfProfiler.markEnd(MARKER_LETTER_COMMIT);
                                state = LetterState::WaitNeutral;
                            }
                        } else if (isNeutralDecision) {
                            state = LetterState::Neutral;
                        }
                        break;
                    case LetterState::WaitNeutral:
                        if (isNeutralDecision) {
                            state = LetterState::Neutral;
                        }
                        break;
                }
            }
        }

        dataLogger.processSerial(gImuAvailable, gFingersAvailable, gWifiConnected);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void TTSTask(void* parameter) {
    Serial.println("[TTSTask] Starting on Core 1");

    while (true) {
        if (!ttsRequestQueue) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        TTSRequest request;
        if (xQueueReceive(ttsRequestQueue, &request, portMAX_DELAY) != pdPASS) {
            continue;
        }

        gTTSInProgress = true;

        // Create filename based on text
        // Strip trailing spaces from text for filename
        String textTrimmed = String(request.text);
        textTrimmed.trim();

        // Save what we're about to play for cooldown tracking
        strncpy((char*)gLastPlayedWord, textTrimmed.c_str(), sizeof(gLastPlayedWord) - 1);
        gLastPlayedWord[sizeof(gLastPlayedWord) - 1] = '\0';

        char filename[32];
        snprintf(filename, sizeof(filename), "/%s.mp3", textTrimmed.c_str());

        Serial.printf("[TTSTask] Free heap: %d bytes\n", ESP.getFreeHeap());

        bool fileExists = gResources.sd && gResources.sd->fileExists(filename);

        if (!fileExists) {
            if (!gResources.amplifier || !gResources.amplifier->isReady()) {
                Serial.println("[TTSTask] Amplifier not ready.");
                gTTSInProgress = false;
                continue;
            }

            if (!connectWiFi(gResources)) {
                Serial.println("[TTSTask] WiFi failed, cannot download new file.");
                gTTSInProgress = false;
                continue;
            }

            if (gResources.sd) {
                gResources.sd->setStatusLED(0, 255, 255);
            }

            Serial.printf("[TTSTask] Downloading TTS for \"%s\"\n", request.text);

            // Download TTS and save to SD card
            perfProfiler.markStart(MARKER_TTS_DOWNLOAD);
            bool success = gResources.amplifier->downloadCloudTTS(request.text, "en-US", filename);
            perfProfiler.markEnd(MARKER_TTS_DOWNLOAD);

            if (!success) {
                Serial.println("[TTSTask] TTS download failed.");
                if (gResources.sd) gResources.sd->clearStatusLED();
                disconnectWiFi();
                gTTSInProgress = false;
                continue;
            }

            Serial.printf("[TTSTask] Download complete, saved to %s\n", filename);

            // Disconnect WiFi before playback
            disconnectWiFi();
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        // Play from SD card
        Serial.printf("[TTSTask] Playing %s from SD card...\n", filename);

        if (!gResources.amplifier->playFileFromSD(filename)) {
            Serial.println("[TTSTask] Failed to start playback from SD.");
            if (gResources.sd) gResources.sd->clearStatusLED();
            gTTSInProgress = false;
            continue;
        }

        // Keep calling loop() while audio is playing
        perfProfiler.markStart(MARKER_TTS_PLAYBACK);
        while (gResources.amplifier && gResources.amplifier->isRunning()) {
            gResources.amplifier->loop();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        perfProfiler.markEnd(MARKER_TTS_PLAYBACK);

        Serial.println("[TTSTask] Audio playback complete");

        // Cleanup
        gResources.amplifier->stop();
        vTaskDelay(pdMS_TO_TICKS(100));

        if (gResources.sd) {
            gResources.sd->clearStatusLED();
        }

        reinitI2C();
        gTTSInProgress = false;
        // Set cooldown timestamp
        gLastTTSCompleteTime = millis();
    }
}

void AudioTask(void* parameter) {
    Serial.println("[AudioTask] Starting on Core 1");
    while (true) {
        if (!audioJobQueue) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        AudioJob job;
        if (xQueueReceive(audioJobQueue, &job, portMAX_DELAY) != pdPASS) {
            continue;
        }

        if (!gResources.amplifier || !gResources.amplifier->isReady()) {
            Serial.println("[AudioTask] Amplifier not ready for playback.");
            gTTSInProgress = false;
            continue;
        }

        Serial.printf("[AudioTask] Playing %s\n", job.filepath);
        if (!gResources.amplifier->playFileFromSD(job.filepath)) {
            Serial.println("[AudioTask] Failed to start playback.");
            gTTSInProgress = false;
            continue;
        }

        while (gResources.amplifier->isRunning()) {
            gResources.amplifier->loop();
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // Explicitly stop and cleanup audio resources
        gResources.amplifier->stop();
        // Allow time for cleanup
        vTaskDelay(pdMS_TO_TICKS(100));

        if (gResources.sd) {
            gResources.sd->clearStatusLED();
        }

        reinitI2C();
        gTTSInProgress = false;
        // Set cooldown timestamp
        gLastTTSCompleteTime = millis();
        Serial.println("[AudioTask] Playback complete.");
        Serial.printf("[AudioTask] Free heap after cleanup: %d bytes\n", ESP.getFreeHeap());
    }
}

void startSystemTasks(const TaskResources& resources) {
    gResources = resources;

    sensorSampleQueue = xQueueCreate(20, sizeof(SensorSample));
    sensorWindowQueue = xQueueCreate(1, sizeof(SensorWindow));
    letterDecisionQueue = xQueueCreate(10, sizeof(LetterDecision));
    ttsRequestQueue = xQueueCreate(3, sizeof(TTSRequest));
    audioJobQueue = xQueueCreate(3, sizeof(AudioJob));

    if (!sensorSampleQueue || !sensorWindowQueue || !letterDecisionQueue ||
        !ttsRequestQueue || !audioJobQueue) {
        Serial.println("[RTOS] Failed to allocate queues!");
        return;
    }

    xTaskCreatePinnedToCore(
        SensorTask,
        "SensorTask",
        3072,
        nullptr,
        4,
        &SensorTaskHandle,
        0);

    xTaskCreatePinnedToCore(
        InferenceTask,
        "InferenceTask",
        4096,
        nullptr,
        3,
        &InferenceTaskHandle,
        0);

    xTaskCreatePinnedToCore(
        LogicTask,
        "LogicTask",
        4096,
        nullptr,
        2,
        &LogicTaskHandle,
        1);

    xTaskCreatePinnedToCore(
        TTSTask,
        "TTSTask",
        12288,
        nullptr,
        2,
        &TTSTaskHandle,
        1);

    xTaskCreatePinnedToCore(
        AudioTask,
        "AudioTask",
        4096,
        nullptr,
        3,
        &AudioTaskHandle,
        1);
}
