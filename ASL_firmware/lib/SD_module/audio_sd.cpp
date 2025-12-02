#include "audio_sd.h"

SD_module::SD_module(uint8_t chipSelectPin) : csPin(chipSelectPin), initialized(false),
                    rgb(NUM_PIXELS, RGB_LED_PIN, NEO_GRB + NEO_KHZ800) {
}

void SD_module::setLED(uint8_t r, uint8_t g, uint8_t b) { 
  rgb.setPixelColor(0, rgb.Color(r, g, b));
  rgb.show(); 
}

void SD_module::ledOff() {
  setLED(0, 0, 0);
}

void SD_module::ledRed() {
  setLED(255, 0, 0);
}

void SD_module::ledBlink(uint8_t r, uint8_t g, uint8_t b, int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    setLED(r, g, b);
    delay(delayMs);
    ledOff();
    delay(delayMs);
  }
}

bool SD_module::begin() {
  // Initialize NeoPixel HERE, not in constructor
  rgb.begin();
  rgb.setBrightness(50);
  ledOff();
  
  ledBlink(0, 0, 255, 2, 150);
  
  // Initialize SPI with your custom pins
  SPI.begin(13, 12, 10, 9);  // SCK, MISO, MOSI, CS
  delay(100);
  
  if (!SD.begin(csPin, SPI)) {
    Serial.println("SD Card Mount Failed!");
    ledBlink(255, 0, 0, 2, 100);
    return false;
  }
  
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    ledBlink(255, 0, 0, 3, 200);
    return false;
  }
  
  Serial.print("SD Card Type: ");
  if(cardType == CARD_MMC) Serial.println("MMC");
  else if(cardType == CARD_SD) Serial.println("SDSC");
  else if(cardType == CARD_SDHC) Serial.println("SDHC");
  else Serial.println("UNKNOWN");
  
  File root = SD.open("/");
  if (!root) {
    Serial.println("Failed to open root directory");
    ledBlink(255, 0, 0, 3, 200);
    return false;
  }
  root.close();
  
  ledBlink(0, 255, 0, 3, 150);
  setLED(0, 255, 0);
  delay(1000);
  ledOff();
  
  initialized = true;
  return true;
}

bool SD_module::isReady() {
  return initialized;
}

bool SD_module::saveAudioChunk(const char* filename, const uint8_t* audioData, size_t dataSize, bool append) {
  if (!initialized) {
    ledBlink(255, 0, 0, 1, 150);
    return false;
  }
  
  setLED(0, 0, 100);

  const char* openMode;
  if (append) {
    openMode = FILE_APPEND;
  } else {
    if (SD.exists(filename)) SD.remove(filename);
    openMode = FILE_WRITE;
  }

  File file = SD.open(filename, openMode);
  if (!file) {
    ledBlink(255, 0, 0, 2, 150);
    return false;
  }
  
  size_t written = file.write(audioData, dataSize);
  file.close();
  
  if (written == dataSize) {
    ledBlink(0, 255, 0, 1, 100);
    ledOff();
    return true;
  } else {
    ledBlink(255, 0, 0, 4, 200);
    return false;
  }
}

size_t SD_module::readAudioFile(const char* filename, uint8_t* buffer, size_t bufferSize) {
  if (!initialized) return 0;
  
  File file = SD.open(filename, FILE_READ);
  if (!file) return 0;
  
  size_t bytesRead = file.read(buffer, bufferSize);
  file.close();
  
  return bytesRead;
}

size_t SD_module::getFileSize(const char* filename) {
  if (!initialized) return 0;
  
  File file = SD.open(filename, FILE_READ);
  if (!file) return 0;
  
  size_t size = file.size();
  file.close();
  return size;
}

bool SD_module::streamAudioFile(const char* filename, void (*callback)(uint8_t*, size_t), size_t chunkSize) {
  if (!initialized) {
    ledBlink(255, 0, 0, 1, 150);
    return false;
  }
  
  File file = SD.open(filename, FILE_READ);
  if (!file) {
    ledBlink(255, 0, 0, 2, 150);
    return false;
  }
  
  setLED(128, 0, 128);
  
  uint8_t* buffer = new uint8_t[chunkSize];
  if (!buffer) {
    file.close();
    ledBlink(255, 0, 0, 5, 250);
    return false;
  }

  while (file.available()) {
    size_t bytesRead = file.read(buffer, chunkSize);
    if (bytesRead > 0) {
      callback(buffer, bytesRead);
    }
  }
  
  delete[] buffer;
  file.close();
  
  ledBlink(0, 255, 0, 1, 100);
  ledOff();
  
  return true;
}

bool SD_module::deleteAudioFile(const char* filename) {
  if (!initialized) return false;
  return SD.remove(filename);
}

bool SD_module::fileExists(const char* filename) {
  if (!initialized) return false;
  return SD.exists(filename);
}

void SD_module::printStorageInfo() {
  if (!initialized) {
    Serial.println("SD Card not initialized");
    ledBlink(255, 0, 0, 1, 150);
    return;
  }
  
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  uint64_t usedBytes = SD.usedBytes() / (1024 * 1024);
  uint64_t totalBytes = SD.totalBytes() / (1024 * 1024);
  
  Serial.println("SD Card Info:");
  Serial.printf("  Card Size: %llu MB\n", cardSize);
  Serial.printf("  Total Space: %llu MB\n", totalBytes);
  Serial.printf("  Used Space: %llu MB\n", usedBytes);
  Serial.printf("  Free Space: %llu MB\n", totalBytes - usedBytes);
  
  ledBlink(0, 255, 255, 2, 100);
}

bool SD_module::createAudioDir(const char* dirname) {
  if (!initialized) return false;
  return SD.mkdir(dirname);
}

void SD_module::setStatusLED(uint8_t r, uint8_t g, uint8_t b) {
  setLED(r, g, b);
}

void SD_module::clearStatusLED() {
  ledOff();
}

void SD_module::blinkStatusLED(uint8_t r, uint8_t g, uint8_t b, int times, int delayMs) {
  ledBlink(r, g, b, times, delayMs);
}

bool SD_module::clearTTSCache() {
  if (!initialized) {
    Serial.println("[SD] SD card not initialized");
    ledBlink(255, 0, 0, 1, 150);
    return false;
  }

  Serial.println("[SD] Clearing TTS cache...");
  setLED(255, 128, 0);

  File root = SD.open("/");
  if (!root) {
    Serial.println("[SD] Failed to open root directory");
    ledBlink(255, 0, 0, 3, 200);
    return false;
  }

  int deletedCount = 0;
  int failedCount = 0;

  File file = root.openNextFile();
  while (file) {
    String filename = String(file.name());
    file.close();

    if (filename.endsWith(".mp3") || filename.endsWith(".MP3")) {
      Serial.printf("[SD] Deleting: %s\n", filename.c_str());
      String filepath = "/" + filename;
      if (SD.remove(filepath.c_str())) {
        deletedCount++;
      } else {
        Serial.printf("[SD] Failed to delete: %s\n", filename.c_str());
        failedCount++;
      }
    }

    file = root.openNextFile();
  }

  root.close();

  Serial.printf("[SD] TTS cache cleared: %d files deleted, %d failed\n", deletedCount, failedCount);

  if (failedCount > 0) {
    ledBlink(255, 128, 0, 2, 150);
  } else if (deletedCount > 0) {
    ledBlink(0, 255, 0, 3, 150);
  } else {
    Serial.println("[SD] No .mp3 files found in cache");
    ledBlink(0, 255, 255, 2, 100);
  }

  ledOff();
  return (failedCount == 0);
}