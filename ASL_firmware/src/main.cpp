#include <Arduino.h>
#include <WiFi.h>

#include "i2s_amp.h"

const char* ssid = "GRASP-Robots";
const char* password = "sirc3310";

I2S_Amplifier i2s_amp;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.print("Initializing I2S Amplifier...");
  if (i2s_amp.begin()) {
    Serial.println(" I2S Amplifier initialized successfully!");
    i2s_amp.setVolume(18);
  } else {
    Serial.println(" I2S Amplifier initialization failed!");
  }

  Serial.println("Setup complete!");
}

void loop() {
  i2s_amp.loop();

  static bool ttsPlayed = false;
  if (!ttsPlayed && i2s_amp.isReady() && WiFi.status() == WL_CONNECTED) {
    Serial.println("Attempting to play TTS 'Hello, There'...");
    if (i2s_amp.playCloudTTS("Hello, There", "en-US")) {
         Serial.println("TTS playback started.");
         ttsPlayed = true;
    } else {
         Serial.println("Failed to start TTS playback.");
         ttsPlayed = true;
    }
  }

  delay(10);
}

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