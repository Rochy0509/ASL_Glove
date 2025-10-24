#include "i2s_amp.h"
#include "FS.h"
#include <WiFiClient.h>
#include <WiFi.h>

I2S_Amplifier::I2S_Amplifier(int8_t bclk, int8_t lrc, int8_t dout)
    : initialized(false), volume(10), bclk_pin(bclk), lrc_pin(lrc), dout_pin(dout) {
    audio = new Audio(false, 3, I2S_NUM_0);
    audio->setConnectionTimeout(500, 2700);
}

I2S_Amplifier::~I2S_Amplifier() {
    if (audio) {
        delete audio;
        audio = nullptr;
    }
}

bool I2S_Amplifier::begin() {
    if (!audio) return false;

    audio->setPinout(bclk_pin, lrc_pin, dout_pin);
    setLibraryVolume(volume);
    initialized = true;
    return true;
}

bool I2S_Amplifier::isReady() {
    return initialized;
}

bool I2S_Amplifier::playFileFromSD(const char* filename) {
    if (!initialized) return false;
    if (!SD.exists(filename)) return false;
    if (audio) {
        audio->connecttoFS((fs::FS&)SD, filename);
        return true;
    }
    return false;
}

bool I2S_Amplifier::playCloudTTS(const char* text, const char* language) {
    if (!initialized || !text || !language) return false;

    String baseUrl = "http://translate.google.com/translate_tts?ie=UTF-8&tl=";
    String langParam = language;
    String textParam = text;
    textParam.replace(" ", "%20");
    textParam.replace(",", "%2C");
    textParam.replace(".", "%2E");
    textParam.replace("!", "%21");
    textParam.replace("?", "%3F");
    
    String ttsUrl = baseUrl + langParam + "&q=" + textParam + "&client=tw-ob&total=1&idx=0";

    if (audio) {
        bool connected = audio->connecttohost(ttsUrl.c_str());
        if (connected) {
            delay(1000);  // Pre-buffer for better quality
        }
        return connected;
    }
    return false;
}

void I2S_Amplifier::stop() {
    if (audio) audio->stopSong();
}

void I2S_Amplifier::pauseResume() {
    if (audio) audio->pauseResume();
}

void I2S_Amplifier::setVolume(int8_t vol) {
    if (vol >= 0 && vol <= 21) {
        volume = vol;
        setLibraryVolume(volume);
    }
}

int8_t I2S_Amplifier::getVolume() {
    return volume;
}

void I2S_Amplifier::loop() {
    if (audio) audio->loop();
}

bool I2S_Amplifier::isRunning() {
    if (audio) return audio->isRunning();
    return false;
}

void I2S_Amplifier::setLibraryVolume(int8_t vol) {
    if (audio) audio->setVolume(vol);
}