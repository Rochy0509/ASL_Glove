#ifndef AUDIO_SD_H
#define AUDIO_SD_H

#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "Adafruit_NeoPixel.h"

#define RGB_LED_PIN 48 //Onboard RGB LED Pin
#define NUM_PIXELS 1 //one LED

class SD_module{
    private:
        bool initialized;
        uint8_t csPin;
        Adafruit_NeoPixel rgb;

        // LED color definitions
        void setLED(uint8_t r, uint8_t g, uint8_t b);
        void ledOff();
        void ledRed();
        void ledBlink(uint8_t r, uint8_t g, uint8_t b, int times = 3, int delayMs = 50);

    
    public:
        //constructor
        SD_module(uint8_t chipSelectPin = 9);
        
        //initialization
        bool begin();
        
        //check if its ready
        bool isReady();

        // Save audio chunk to SD card
        bool saveAudioChunk(const char* filename, const uint8_t* audioData, size_t dataSize, bool append = false);
        
        // Read audio file for playback
        size_t readAudioFile(const char* filename, uint8_t* buffer, size_t bufferSize);
        
        // Get audio file size
        size_t getFileSize(const char* filename);
        
        // Stream audio file in chunks (for playback)
        bool streamAudioFile(const char* filename, void (*callback)(uint8_t*, size_t), size_t chunkSize = 512);
        
        // Delete audio file
        bool deleteAudioFile(const char* filename);
        
        // Check if file exists
        bool fileExists(const char* filename);
        
        // Get storage info
        void printStorageInfo();
        
        // Create directory for audio files
        bool createAudioDir(const char* dirname);
        
        // Public LED control methods
        void setStatusLED(uint8_t r, uint8_t g, uint8_t b);
        void clearStatusLED();
        void blinkStatusLED(uint8_t r, uint8_t g, uint8_t b, int times = 3, int delayMs = 50);

        // Clear TTS cache (delete all .mp3 files)
        bool clearTTSCache();
};

#endif //AUDIO_SD_H