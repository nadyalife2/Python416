#ifndef MELVINTTS_H
#define MELVINTTS_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include "Config.h"
#include <mbedtls/base64.h>

extern bool playWavFromSD(const char* path);

class MelvinTTS {
public:
    bool begin() { return true; }

    void speak(String text, const MelvinConfig& cfg) {
        if (text.length() == 0) return;
        
        Serial.printf("[TTS] Synthesizing with %s...\n", cfg.tts_provider.c_str());
        
        if (cfg.tts_provider == "google") {
            synthesizeGoogle(text, cfg);
        } else {
            Serial.println("[TTS] Provider not fully implemented, playing placeholder.");
            speakRandomPhrase();
        }
    }

    void speakRandomPhrase() {
        if (SD_MMC.exists("/hello.wav"))      playWavFromSD("/hello.wav");
        else if (SD_MMC.exists("/ready.wav")) playWavFromSD("/ready.wav");
    }

private:
    void synthesizeGoogle(String text, const MelvinConfig& cfg) {
        if (cfg.tts_key.length() < 10) {
            Serial.println("[TTS] No Google API Key, playing local phrase.");
            speakRandomPhrase();
            return;
        }

        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;

        String url = "https://texttospeech.googleapis.com/v1/text:synthesize?key=" + cfg.tts_key;
        http.begin(client, url);
        http.addHeader("Content-Type", "application/json");

        JsonDocument doc;
        doc["input"]["text"] = text;
        doc["voice"]["languageCode"] = "ru-RU";
        doc["voice"]["name"] = cfg.tts_voice;
        doc["audioConfig"]["audioEncoding"] = "LINEAR16"; // CHANGED TO WAV/PCM

        String body;
        serializeJson(doc, body);

        int code = http.POST(body);
        if (code == 200) {
            String response = http.getString();
            JsonDocument res;
            deserializeJson(res, response);
            String base64Audio = res["audioContent"].as<String>();
            
            // Decode and save to SD
            File file = SD_MMC.open("/resp.wav", FILE_WRITE);
            if (file) {
                size_t outputLen = 0;
                size_t inputLen = base64Audio.length();
                uint8_t* decoded = (uint8_t*)malloc(inputLen); 
                if (decoded) {
                    int ret = mbedtls_base64_decode(decoded, inputLen, &outputLen, (const unsigned char*)base64Audio.c_str(), inputLen);
                    if (ret == 0) {
                        file.write(decoded, outputLen);
                    }
                    free(decoded);
                }
                file.close();
                
                Serial.println("[TTS] Response saved, playing...");
                playWavFromSD("/resp.wav");
            }
        } else {
            Serial.printf("[TTS] Google Error %d: %s\n", code, http.getString().c_str());
        }
        http.end();
    }
};

#endif
