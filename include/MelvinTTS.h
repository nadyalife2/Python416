#ifndef MELVINTTS_H
#define MELVINTTS_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include "Config.h"
#include "MelvinState.h"
#include "Display.h"
#include "RabbitFace.h"
#include <mbedtls/base64.h>

extern bool playWavFromSD(const char* path);

// Forward declarations for display redrawing during network transfer
extern uint32_t lastRedrawMs;
extern RobotState currentState;
extern uint32_t animTick;
extern LGFX_Sprite canvas;

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
    bool decodeTtsStreamToWav(WiFiClient& stream, const char* filepath) {
        // Search for "\"audioContent\"" in the stream
        const char* target = "\"audioContent\"";
        size_t targetLen = strlen(target);
        size_t matchIdx = 0;
        
        uint32_t startMs = millis();
        while (matchIdx < targetLen) {
            if (millis() - startMs > 15000) {
                Serial.println("[TTS] Stream timeout searching for audioContent");
                return false;
            }
            if (stream.available()) {
                char c = stream.read();
                if (c == target[matchIdx]) {
                    matchIdx++;
                } else {
                    matchIdx = (c == target[0]) ? 1 : 0;
                }
            } else {
                delay(1);
            }
        }
        
        // Find the starting quote of the base64 value
        bool foundQuote = false;
        startMs = millis();
        while (!foundQuote) {
            if (millis() - startMs > 5000) {
                Serial.println("[TTS] Stream timeout searching for base64 start");
                return false;
            }
            if (stream.available()) {
                char c = stream.read();
                if (c == '"') {
                    foundQuote = true;
                }
            } else {
                delay(1);
            }
        }
        
        // Open file on SD
        File file = SD_MMC.open(filepath, FILE_WRITE);
        if (!file) {
            Serial.printf("[TTS] Failed to open %s for writing\n", filepath);
            return false;
        }

        char b64Chunk[4];
        int b64Idx = 0;
        uint8_t decodedChunk[3];
        size_t totalDecoded = 0;

        startMs = millis();
        while (true) {
            // Tick screen redraw to keep face animating during download
            uint32_t now = millis();
            if (now - lastRedrawMs >= 80) {
                animTick++;
                drawFace(canvas, currentState, animTick);
                canvas.pushSprite(0, 0);
                lastRedrawMs = now;
            }

            if (millis() - startMs > 15000) {
                Serial.println("[TTS] Stream timeout reading base64 data");
                file.close();
                return false;
            }
            if (stream.available()) {
                char c = stream.read();
                startMs = millis(); // Reset timeout on data
                
                if (c == '"') {
                    // Reached the closing quote
                    break;
                }
                if (c == '\r' || c == '\n' || c == ' ' || c == '\t') {
                    continue; // Skip whitespaces
                }
                
                b64Chunk[b64Idx++] = c;
                if (b64Idx == 4) {
                    size_t outLen = 0;
                    int ret = mbedtls_base64_decode(decodedChunk, 3, &outLen, (const unsigned char*)b64Chunk, 4);
                    if (ret == 0 && outLen > 0) {
                        file.write(decodedChunk, outLen);
                        totalDecoded += outLen;
                    } else if (ret != 0) {
                        Serial.printf("[TTS] Base64 decode error: %d\n", ret);
                    }
                    b64Idx = 0;
                }
            } else {
                delay(1);
            }
        }
        
        // Decoded remaining data if present
        if (b64Idx > 0) {
            while (b64Idx < 4) b64Chunk[b64Idx++] = '=';
            size_t outLen = 0;
            mbedtls_base64_decode(decodedChunk, 3, &outLen, (const unsigned char*)b64Chunk, 4);
            if (outLen > 0) {
                file.write(decodedChunk, outLen);
                totalDecoded += outLen;
            }
        }

        file.close();
        Serial.printf("[TTS] Direct decode complete. Wrote %u bytes to SD card.\n", totalDecoded);
        return totalDecoded > 0;
    }

    void synthesizeGoogle(String text, const MelvinConfig& cfg) {
        if (cfg.tts_key.length() < 10) {
            Serial.println("[TTS] No Google API Key, playing local phrase.");
            speakRandomPhrase();
            return;
        }

        // Limit length to 200 characters to prevent OOM
        const size_t maxTtsChars = 200;
        if (text.length() > maxTtsChars) {
            Serial.printf("[TTS] Text too long (%d chars), truncating to %d\n", text.length(), maxTtsChars);
            text = text.substring(0, maxTtsChars);
        }
        Serial.printf("[TTS] Synthesizing %d chars...\n", text.length());

        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.setTimeout(20000); 

        String url = "https://texttospeech.googleapis.com/v1/text:synthesize?key=" + cfg.tts_key;
        http.begin(client, url);
        http.addHeader("Content-Type", "application/json");

        JsonDocument doc;
        doc["input"]["text"] = text;
        doc["voice"]["languageCode"] = "ru-RU";
        doc["voice"]["name"] = cfg.tts_voice;
        doc["audioConfig"]["audioEncoding"] = "LINEAR16";
        doc["audioConfig"]["sampleRateHertz"] = 16000; // Force 16kHz format

        String body;
        serializeJson(doc, body);

        int code = http.POST(body);
        if (code == 200) {
            WiFiClient& stream = http.getStream();
            bool success = decodeTtsStreamToWav(stream, "/resp.wav");
            if (success) {
                Serial.println("[TTS] Response saved, playing...");
                playWavFromSD("/resp.wav");
            } else {
                Serial.println("[TTS] Stream decoding failed!");
            }
        } else {
            Serial.printf("[TTS] Google Error %d: %s\n", code, http.getString().c_str());
        }
        http.end();
    }
};

#endif
