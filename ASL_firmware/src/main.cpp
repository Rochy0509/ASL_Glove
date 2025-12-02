#include <Arduino.h>
#include <WiFi.h>
#include <esp_wpa2.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "audio_sd.h"
#include "data_logger.h"
#include "finger_sensors.h"
#include "freertos_tasks.h"
#include "i2s_amp.h"
#include "mpu9250_sensor.h"
#include "perf_profiler.h"

// WiFi credentials  
const char* ssid = "BELL229";
const char* password = "7C5D3235D53D";
const char* username = "";  // Empty for WPA2-Personal
const char* API_KEY = "AIzaSyAo-gzHS52FS15EUDmNnehv7xy3Se6d-go";

void connectToWiFi() {
  Serial.println("\nTesting WiFi Connection...");
  Serial.printf("SSID: %s\n", ssid);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(500);

  // Scan for available networks
  Serial.println("Scanning for WiFi networks...");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("No networks found!");
  } else {
    Serial.printf("Found %d networks:\n", n);
    for (int i = 0; i < n; ++i) {
      Serial.printf("%d: %s (Signal: %d dBm) %s\n",
        i + 1,
        WiFi.SSID(i).c_str(),
        WiFi.RSSI(i),
        (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Encrypted");
    }
  }
  Serial.println();

  // Check if WPA2 Enterprise or regular WiFi
  if (username && strlen(username) > 0) {
    // WPA2 Enterprise
    Serial.printf("Username: %s\n", username);
    Serial.println("Configuring WPA2 Enterprise...");

    esp_wifi_sta_wpa2_ent_disable();
    delay(100);

    esp_wifi_sta_wpa2_ent_set_identity((uint8_t*)username, strlen(username));
    esp_wifi_sta_wpa2_ent_set_username((uint8_t*)username, strlen(username));
    esp_wifi_sta_wpa2_ent_set_password((uint8_t*)password, strlen(password));

    esp_wifi_sta_wpa2_ent_enable();
    WiFi.begin(ssid);
  } else {
    // Regular WiFi (WPA2-Personal)
    Serial.println("Configuring regular WiFi (WPA2-Personal)...");
    WiFi.begin(ssid, password);
  }

  // Wait for connection
  uint32_t start = millis();
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected!");
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Signal Strength: %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println("WiFi Connection Failed");
    Serial.printf("Final Status: %d\n", WiFi.status());
    Serial.println("Status codes: 0=IDLE, 1=NO_SSID, 3=CONNECTED, 4=CONNECT_FAILED, 6=DISCONNECTED");
  }
  Serial.println();

  // Disconnect for now - will reconnect when needed for TTS
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

MPU9250_Sensor imu_sensor;
I2S_Amplifier i2s_amp;
SD_module sd_card(9);
FingerSensorManager fingerManager;

void initializeFingerSensors() {
  Serial.println("Initializing Finger Sensors...");
  fingerManager.begin();

  fingerManager.addFinger(1, "Pinky", 5);
  fingerManager.addFinger(2, "Ring", 5);
  fingerManager.addFinger(4, "Middle", 5);
  fingerManager.addFinger(5, "Index", 5);
  fingerManager.addFinger(6, "Thumb", 5);

  Serial.println("Establishing finger sensor baseline (keep hand relaxed)...");
  int attempts = 0;
  while (!fingerManager.establishBaseline() && attempts < 100) {
    delay(50);
    attempts++;
  }

  if (fingerManager.isBaselineComplete()) {
    Serial.println("Finger sensors baseline ready!");
    gFingersAvailable = true;
    fingerManager.printCalibrationInfo();
  } else {
    Serial.println("WARNING: Finger sensor baseline timeout");
    gFingersAvailable = false;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\nASL Glove Firmware");
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("CPU Cores: %d\n\n", ESP.getChipCores());

  // Test WiFi connection at startup
  connectToWiFi();

  WiFi.mode(WIFI_OFF);

  Wire.begin(18, 46, 100000);
  Wire.setTimeout(1000);
  delay(100);

  Serial.print("Initializing SD Card...");
  if (sd_card.begin()) {
    Serial.println(" OK");
    sd_card.setStatusLED(0, 255, 0);
    delay(300);
    sd_card.clearStatusLED();
  } else {
    Serial.println(" FAILED");
  }

  Serial.print("Initializing I2S Amplifier...");
  if (i2s_amp.begin()) {
    Serial.println(" OK");
    i2s_amp.setVolume(21);  // Max volume
  } else {
    Serial.println(" FAILED");
  }

  Serial.print("Initializing IMU...");
  if (imu_sensor.begin()) {
    Serial.println(" OK");
    gImuAvailable = true;
  } else {
    Serial.println(" FAILED");
    gImuAvailable = false;
  }

  initializeFingerSensors();
  
  // Initialize performance profiler
  perfProfiler.begin();
  Serial.println("[PROFILER] Initialized. Use 'o' to start, 'O' to stop and show stats, 'j' to export VCD.");
  
  dataLogger.begin(&fingerManager, &imu_sensor, &sd_card);

  TaskResources resources{};
  resources.imu = &imu_sensor;
  resources.fingers = &fingerManager;
  resources.amplifier = &i2s_amp;
  resources.sd = &sd_card;
  resources.wifiSsid = ssid;
  resources.wifiPassword = password;
  resources.wifiUsername = username;  // For WPA2 Enterprise

  startSystemTasks(resources);

  Serial.println("\nSetup complete!");
  Serial.println("Press 'r' to run flex calibration, 'u' for IMU calibration, then 'e' to start inference and 'p'/'l' to log data.");
  Serial.println("Use 'x' to enable/disable shake-triggered TTS (off by default).");

  sd_card.setStatusLED(128, 0, 128);
  delay(500);
  sd_card.clearStatusLED();
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(50));
}

// Audio library callbacks (required for v2.3.0)
void audio_info(const char *info) {
  Serial.print("Audio Info: ");
  Serial.println(info);
}
void audio_id3data(const char *info) {
  Serial.print("ID3 Data: ");
  Serial.println(info);
}
void audio_eof_mp3(const char *info) {
  Serial.print("EOF MP3: ");
  Serial.println(info);
}
void audio_showstation(const char *info) {
  Serial.print("Station: ");
  Serial.println(info);
}
void audio_showstreamtitle(const char *info) {
  Serial.print("Stream Title: ");
  Serial.println(info);
}
void audio_bitrate(const char *info) {
  Serial.print("Bitrate: ");
  Serial.println(info);
}
void audio_commercial(const char *info) {
  Serial.print("Commercial: ");
  Serial.println(info);
}
void audio_icyurl(const char *info) {
  Serial.print("ICY URL: ");
  Serial.println(info);
}
void audio_lasthost(const char *info) {
 Serial.print("Last Host: ");
 Serial.println(info);
}
void audio_eof_stream(const char *info) {
  Serial.print("EOF Stream: ");
  Serial.println(info);
}
void audio_eof_speech(const char *info) {
  Serial.print("EOF Speech: ");
  Serial.println(info);
}
