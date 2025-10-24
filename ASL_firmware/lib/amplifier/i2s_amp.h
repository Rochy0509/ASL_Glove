#ifndef I2S_AMP_H
#define I2S_AMP_H

#include "Audio.h"
#include "SD.h"

#define I2S_BCLK_PIN 15
#define I2S_LRC_PIN  16
#define I2S_DOUT_PIN 17

class I2S_Amplifier {
private:
    Audio* audio;
    bool initialized;
    int8_t volume;
    int8_t bclk_pin;
    int8_t lrc_pin;
    int8_t dout_pin;

    void setLibraryVolume(int8_t vol);

public:
    I2S_Amplifier(int8_t bclk = I2S_BCLK_PIN, int8_t lrc = I2S_LRC_PIN, int8_t dout = I2S_DOUT_PIN);
    ~I2S_Amplifier();

    bool begin();
    bool isReady();
    bool playFileFromSD(const char* filename);
    bool playCloudTTS(const char* text, const char* language = "en-US");
    void stop();
    void pauseResume();
    void setVolume(int8_t vol);
    int8_t getVolume();
    void loop();
    bool isRunning();
};

#endif // I2S_AMP_H