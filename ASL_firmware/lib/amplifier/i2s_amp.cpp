#include "i2s_amp.h"
#include "FS.h"
#include "SD.h"
// WiFi for TTS streaming
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ctype.h>

extern const char* API_KEY;

namespace {

int8_t base64CharToValue(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool decodeBase64ToFile(const char* input, size_t length, File& file, size_t& bytesWritten) {
    uint32_t bit_stream = 0;
    int bits = 0;
    uint8_t buffer[512];
    size_t buffer_len = 0;
    bytesWritten = 0;

    auto flushBuffer = [&]() -> bool {
        if (buffer_len == 0) return true;
        const size_t toWrite = buffer_len;
        const size_t written = file.write(buffer, toWrite);
        if (written != toWrite) {
            return false;
        }
        bytesWritten += written;
        buffer_len = 0;
        return true;
    };

    for (size_t i = 0; i < length; ++i) {
        char c = input[i];
        if (c == '=') {
            break;
        }
        if (isspace(static_cast<unsigned char>(c))) {
            continue;
        }
        int8_t value = base64CharToValue(c);
        if (value < 0) {
            continue;
        }

        bit_stream = (bit_stream << 6) | static_cast<uint32_t>(value);
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            buffer[buffer_len++] = static_cast<uint8_t>((bit_stream >> bits) & 0xFF);
            if (buffer_len == sizeof(buffer)) {
                if (!flushBuffer()) {
                    return false;
                }
            }
        }
    }

    return flushBuffer();
}

}  // namespace

size_t base64_decode(const char* input, uint8_t* output, size_t outputLen) {
    size_t input_len = strlen(input);
    size_t output_len = 0;
    uint32_t bit_stream = 0;
    int bits = 0;

    for (size_t i = 0; i < input_len; i++) {
        char c = input[i];

        if (c == '=') break;
        if (isspace(static_cast<unsigned char>(c))) continue;

        int8_t value = base64CharToValue(c);
        if (value < 0) continue;

        bit_stream = (bit_stream << 6) | static_cast<uint32_t>(value);
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            if (output_len < outputLen) {
                output[output_len++] = static_cast<uint8_t>((bit_stream >> bits) & 0xFF);
            }
        }
    }

    return output_len;
}

I2S_Amplifier::I2S_Amplifier(int8_t bclk, int8_t lrc, int8_t dout)
    : initialized(false), volume(24), bclk_pin(bclk), lrc_pin(lrc), dout_pin(dout) {
    audio = new Audio(false, 3, I2S_NUM_0);
    audio->setConnectionTimeout(500, 2700);
    audio->setBufsize(8192, 16384);  // 8KB input, 16KB output
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
    Serial.printf("[TTS] Streaming '%s' in language '%s'\n", text, language);
    return audio->connecttospeech(text, language);
}

bool I2S_Amplifier::downloadCloudTTS(const char* text, const char* language, const char* filename) {
    if (!initialized || !text || !language || !filename) return false;

    Serial.printf("[TTS] Downloading '%s' to %s\n", text, filename);

    HTTPClient http;
    const char* url = "https://texttospeech.googleapis.com/v1/text:synthesize";
    String fullUrl = String(url) + "?key=" + API_KEY;

    http.begin(fullUrl);
    http.addHeader("Content-Type", "application/json");

    JsonDocument doc;
    doc["input"]["text"] = text;
    doc["voice"]["languageCode"] = language;
    doc["voice"]["name"] = String(language) + "-Neural2-C";
    doc["audioConfig"]["audioEncoding"] = "MP3";
    doc["audioConfig"]["speakingRate"] = 1.0;
    doc["audioConfig"]["pitch"] = 0.0;

    String requestBody;
    serializeJson(doc, requestBody);
    doc.clear();

    Serial.printf("[TTS] Request size: %d bytes, Free heap: %d\n", requestBody.length(), ESP.getFreeHeap());

    int httpCode = http.POST(requestBody);

    if (httpCode != 200) {
        Serial.printf("[TTS] HTTP error: %d\n", httpCode);
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();

    bool found = false;
    String searchPattern = "\"audioContent\"";
    String buffer;

    Serial.printf("[TTS] Searching for audioContent field, Free heap: %d\n", ESP.getFreeHeap());

    while (stream->connected() && stream->available()) {
        char c = stream->read();
        buffer += c;
        if (buffer.endsWith(searchPattern)) {
            found = true;
            break;
        }
        if (buffer.length() > 50) {
            buffer.remove(0, buffer.length() - searchPattern.length());
        }
    }

    if (!found) {
        Serial.println("[TTS] No audioContent field in response");
        http.end();
        return false;
    }

    while (stream->connected() && stream->available()) {
        char c = stream->read();
        if (c == '"') break;
    }

    Serial.printf("[TTS] Found audioContent, decoding to file, Free heap: %d\n", ESP.getFreeHeap());

    if (SD.exists(filename)) {
        SD.remove(filename);
    }

    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        Serial.println("[TTS] Failed to open file for writing");
        http.end();
        return false;
    }

    String b64Buffer;
    size_t totalBytesWritten = 0;
    const size_t CHUNK_SIZE = 1024;

    while (stream->connected() || stream->available()) {
        if (!stream->available()) {
            delay(1);
            continue;
        }

        char c = stream->read();
        if (c == '"') break;

        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=') {
            b64Buffer += c;

            if (b64Buffer.length() >= CHUNK_SIZE) {
                size_t bytesWritten = 0;
                if (!decodeBase64ToFile(b64Buffer.c_str(), b64Buffer.length(), file, bytesWritten)) {
                    Serial.println("[TTS] Base64 decode failed during streaming");
                    file.close();
                    http.end();
                    SD.remove(filename);
                    return false;
                }
                totalBytesWritten += bytesWritten;
                b64Buffer = "";
            }
        }
    }

    if (b64Buffer.length() > 0) {
        size_t bytesWritten = 0;
        decodeBase64ToFile(b64Buffer.c_str(), b64Buffer.length(), file, bytesWritten);
        totalBytesWritten += bytesWritten;
    }

    file.close();
    http.end();

    Serial.printf("[TTS] Saved %d bytes to %s, Free heap: %d\n", totalBytesWritten, filename, ESP.getFreeHeap());
    return true;
}

void I2S_Amplifier::stop() {
    if (audio) audio->stopSong();
}

void I2S_Amplifier::pauseResume() {
    if (audio) audio->pauseResume();
}

void I2S_Amplifier::setVolume(int8_t vol) {
    if (vol >= 0 && vol <= 30) {
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
