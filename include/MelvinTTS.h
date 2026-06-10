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
#include <esp_random.h>

extern bool playWavFromSD(const char* path);
extern bool playMp3FromSD(const char* path);
#define BOOT_BTN_PIN 0

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

        // Clean text - replace markdown asterisks/formatting which sounds weird
        text.replace("*", "");
        text.replace("`", "");

        // Split text into sentences using punctuation (. ! ? ; \n)
        std::vector<String> sentences;
        int start = 0;
        int len = text.length();
        for (int i = 0; i < len; i++) {
            char c = text[i];
            if (c == '.' || c == '!' || c == '?' || c == ';' || c == '\n') {
                String s = text.substring(start, i + 1);
                s.trim();
                if (s.length() > 0) {
                    sentences.push_back(s);
                }
                start = i + 1;
            }
        }
        if (start < len) {
            String s = text.substring(start);
            s.trim();
            if (s.length() > 0) {
                sentences.push_back(s);
            }
        }

        if (sentences.empty()) {
            sentences.push_back(text);
        }

        Serial.printf("[TTS] Split response into %d sentences for streaming.\n", sentences.size());

        for (size_t idx = 0; idx < sentences.size(); idx++) {
            // Check button before synthesis of each sentence
            if (digitalRead(BOOT_BTN_PIN) == LOW) {
                Serial.println("[TTS] Synthesis loop aborted by button press!");
                break;
            }

            String sentence = sentences[idx];
            Serial.printf("[TTS] Synthesizing sentence %d/%d: \"%s\"\n", idx + 1, sentences.size(), sentence.c_str());

            bool playResult = false;
            if (cfg.tts_provider == "google") {
                playResult = synthesizeGoogle(sentence, cfg);
            } else if (cfg.tts_provider == "google_free") {
                playResult = synthesizeGoogleFree(sentence, cfg);
            } else if (cfg.tts_provider == "yandex") {
                playResult = synthesizeYandex(sentence, cfg);
            } else if (cfg.tts_provider == "openai") {
                playResult = synthesizeOpenAI(sentence, cfg);
            } else if (cfg.tts_provider == "elevenlabs") {
                playResult = synthesizeElevenLabs(sentence, cfg);
            } else if (cfg.tts_provider == "none") {
                Serial.println("[TTS] Provider is none, playing random phrase.");
                speakRandomPhrase();
                playResult = true;
                break;
            } else {
                Serial.printf("[TTS] Provider '%s' not implemented, playing random phrase.\n", cfg.tts_provider.c_str());
                speakRandomPhrase();
                playResult = true;
                break;
            }

            if (!playResult) {
                Serial.println("[TTS] Sentence playback failed or aborted by button. Breaking loop.");
                break;
            }
        }
    }

    void speakRandomPhrase() {
        // Pool of candidate WAV phrases on SD card
        const char* candidates[] = {
            "/hello.wav",
            "/ready.wav",
            "/ok.wav",
            "/beep.wav",
            "/phrase1.wav",
            "/phrase2.wav",
            "/phrase3.wav"
        };
        const int numCandidates = sizeof(candidates) / sizeof(candidates[0]);

        // Build list of files that actually exist
        const char* available[numCandidates];
        int count = 0;
        for (int i = 0; i < numCandidates; i++) {
            if (SD_MMC.exists(candidates[i])) {
                available[count++] = candidates[i];
            }
        }

        if (count == 0) {
            Serial.println("[TTS] speakRandomPhrase: no WAV files found on SD (checked hello/ready/ok/beep/phrase1-3)");
            return;
        }

        // Pick a random file using ESP32 hardware RNG
        int idx = (int)(esp_random() % (uint32_t)count);
        Serial.printf("[TTS] speakRandomPhrase: playing %s (%d available)\n", available[idx], count);
        playWavFromSD(available[idx]);
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

        // Allocate a write-combiner buffer in PSRAM to group writes
        const size_t decodeBufSize = 64 * 1024; // 64KB blocks
        uint8_t* decodeBuf = nullptr;
        size_t currentCapacity = 0;
        if (psramFound()) {
            decodeBuf = (uint8_t*)heap_caps_malloc(decodeBufSize, MALLOC_CAP_SPIRAM);
            if (decodeBuf) {
                currentCapacity = decodeBufSize;
            }
        }
        if (!decodeBuf) {
            Serial.println("[TTS] PSRAM decode buffer allocation failed or PSRAM not found, trying internal RAM (8KB)...");
            decodeBuf = (uint8_t*)malloc(8 * 1024);
            currentCapacity = 8 * 1024;
        }
        if (!decodeBuf) {
            Serial.println("[TTS] Fatal: Out of memory for decode buffer!");
            file.close();
            return false;
        }

        char b64Chunk[4];
        int b64Idx = 0;
        uint8_t decodedChunk[3];
        size_t totalDecoded = 0;
        size_t bufferedBytes = 0;

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
                free(decodeBuf);
                file.close();
                return false;
            }
            if (digitalRead(BOOT_BTN_PIN) == LOW) {
                Serial.println("[TTS] Base64 decode aborted by button press!");
                free(decodeBuf);
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
                        if (bufferedBytes + outLen > currentCapacity) {
                            file.write(decodeBuf, bufferedBytes);
                            bufferedBytes = 0;
                        }
                        memcpy(&decodeBuf[bufferedBytes], decodedChunk, outLen);
                        bufferedBytes += outLen;
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
        
        // Decode remaining data if present
        if (b64Idx > 0) {
            while (b64Idx < 4) b64Chunk[b64Idx++] = '=';
            size_t outLen = 0;
            mbedtls_base64_decode(decodedChunk, 3, &outLen, (const unsigned char*)b64Chunk, 4);
            if (outLen > 0) {
                if (bufferedBytes + outLen > currentCapacity) {
                    file.write(decodeBuf, bufferedBytes);
                    bufferedBytes = 0;
                }
                memcpy(&decodeBuf[bufferedBytes], decodedChunk, outLen);
                bufferedBytes += outLen;
                totalDecoded += outLen;
            }
        }

        // Flush remaining bytes in the buffer
        if (bufferedBytes > 0) {
            file.write(decodeBuf, bufferedBytes);
        }

        free(decodeBuf);
        file.close();
        Serial.printf("[TTS] Streaming decode complete. Wrote %u bytes to SD card.\n", totalDecoded);
        return totalDecoded > 0;
    }

    bool synthesizeElevenLabs(String text, const MelvinConfig& cfg) {
        if (cfg.tts_key.length() < 10) {
            Serial.println("[TTS] No ElevenLabs API Key. Speech synthesis skipped.");
            return false;
        }

        const size_t maxTtsChars = 500;
        if (text.length() > maxTtsChars) {
            text = text.substring(0, maxTtsChars);
        }
        Serial.printf("[TTS][ElevenLabs] Synthesizing %d chars...\n", text.length());

        WiFiClient client;
        client.setTimeout(30000);
        client.setConnectionTimeout(30000);
        WiFiClientSecure secureClient;
        secureClient.setTimeout(30000);
        secureClient.setConnectionTimeout(30000);
        HTTPClient http;
        http.setTimeout(30000);
        http.setConnectTimeout(30000);

        // ElevenLabs voice_id: default to "21m00Tcm4TlvDq8ikWAM" (Rachel) if not specified
        String voiceId = (cfg.tts_voice.length() > 0) ? cfg.tts_voice : "21m00Tcm4TlvDq8ikWAM";
        String url = "https://api.elevenlabs.io/v1/text-to-speech/" + voiceId + "?output_format=pcm_16000";
        if (cfg.api_proxy.length() > 0) {
            url = cfg.api_proxy + "/elevenlabs-tts/v1/text-to-speech/" + voiceId + "?output_format=pcm_16000";
            if (url.startsWith("https://")) {
                secureClient.setInsecure();
                http.begin(secureClient, url);
            } else {
                http.begin(client, url);
            }
        } else {
            secureClient.setInsecure();
            http.begin(secureClient, url);
        }
        http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
        http.addHeader("xi-api-key", cfg.tts_key);
        http.addHeader("Content-Type", "application/json");

        JsonDocument doc;
        doc["text"] = text;
        doc["model_id"] = "eleven_multilingual_v2";

        String body;
        serializeJson(doc, body);

        int code = http.POST(body);
        Serial.printf("[TTS][ElevenLabs] HTTP %d\n", code);

        bool playResult = false;
        if (code == 200) {
            WiFiClient& stream = http.getStream();
            bool success = writeLpcmToWav(stream, "/resp.wav", 16000);
            if (success) {
                Serial.println("[TTS][ElevenLabs] Saved, playing...");
                playResult = playWavFromSD("/resp.wav");
            } else {
                Serial.println("[TTS][ElevenLabs] Stream write failed!");
                if (SD_MMC.exists("/error.wav")) {
                    playWavFromSD("/error.wav");
                }
            }
        } else {
            Serial.printf("[TTS][ElevenLabs] Error %d: %s\n", code, http.getString().c_str());
            if (SD_MMC.exists("/error.wav")) {
                playWavFromSD("/error.wav");
            }
        }
        http.end();
        return playResult;
    }

    bool synthesizeGoogle(String text, const MelvinConfig& cfg) {
        if (cfg.tts_key.length() < 10) {
            Serial.println("[TTS] No Google API Key. Speech synthesis skipped.");
            return false;
        }

        const size_t maxTtsChars = 500;
        if (text.length() > maxTtsChars) {
            text = text.substring(0, maxTtsChars);
        }
        Serial.printf("[TTS] Synthesizing %d chars...\n", text.length());

        WiFiClient client;
        client.setTimeout(30000);
        client.setConnectionTimeout(30000);
        WiFiClientSecure secureClient;
        secureClient.setTimeout(30000);
        secureClient.setConnectionTimeout(30000);
        HTTPClient http;
        http.setTimeout(30000); 
        http.setConnectTimeout(30000);

        String url = "https://texttospeech.googleapis.com/v1/text:synthesize?key=" + cfg.tts_key;
        if (cfg.api_proxy.length() > 0) {
            url = cfg.api_proxy + "/google-tts/v1/text:synthesize?key=" + cfg.tts_key;
            if (url.startsWith("https://")) {
                secureClient.setInsecure();
                http.begin(secureClient, url);
            } else {
                http.begin(client, url);
            }
        } else {
            secureClient.setInsecure();
            http.begin(secureClient, url);
        }
        http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
        http.addHeader("Content-Type", "application/json");

        JsonDocument doc;
        doc["input"]["text"] = text;
        String langCode = "ru-RU"; // fallback
        if (cfg.tts_voice.length() >= 5) {
            langCode = cfg.tts_voice.substring(0, 5);
        }
        doc["voice"]["languageCode"] = langCode;
        doc["voice"]["name"] = cfg.tts_voice;
        doc["audioConfig"]["audioEncoding"] = "LINEAR16";
        doc["audioConfig"]["sampleRateHertz"] = 16000; 

        String body;
        serializeJson(doc, body);

        int code = http.POST(body);
        bool playResult = false;
        if (code == 200) {
            WiFiClient& stream = http.getStream();
            bool success = decodeTtsStreamToWav(stream, "/resp.wav");
            if (success) {
                Serial.println("[TTS] Response saved, playing...");
                playResult = playWavFromSD("/resp.wav");
            } else {
                Serial.println("[TTS] Stream decoding failed!");
            }
        } else {
            Serial.printf("[TTS] Google Error %d: %s\n", code, http.getString().c_str());
            if (SD_MMC.exists("/error.wav")) {
                playWavFromSD("/error.wav");
            }
        }
        http.end();
        return playResult;
    }

    // ============================================================
    // YANDEX SPEECHKIT TTS
    // Endpoint: https://tts.api.cloud.yandex.net/speech/v1/tts:synthesize
    // Auth: Authorization: Api-Key <key>
    // Returns: raw LPCM PCM (16-bit, LE, signed) — WAV header added manually
    // Register at: https://console.yandex.cloud/ → SpeechKit → API Key
    // Free tier: 1 000 000 characters/month
    // Works from Russia: YES
    // ============================================================
    bool synthesizeYandex(String text, const MelvinConfig& cfg) {
        if (cfg.tts_key.length() < 10) {
            Serial.println("[TTS] No Yandex API Key. Speech synthesis skipped.");
            return false;
        }

        const size_t maxTtsChars = 500;
        if (text.length() > maxTtsChars) {
            text = text.substring(0, maxTtsChars);
        }
        Serial.printf("[TTS][Yandex] Synthesizing %d chars...\n", text.length());

        WiFiClient client;
        client.setTimeout(30000);
        client.setConnectionTimeout(30000);
        WiFiClientSecure secureClient;
        secureClient.setTimeout(30000);
        secureClient.setConnectionTimeout(30000);
        HTTPClient http;
        http.setTimeout(30000);
        http.setConnectTimeout(30000);

        String url = "https://tts.api.cloud.yandex.net/speech/v1/tts:synthesize";
        if (cfg.api_proxy.length() > 0) {
            url = cfg.api_proxy + "/yandex-tts/speech/v1/tts:synthesize";
            if (url.startsWith("https://")) {
                secureClient.setInsecure();
                http.begin(secureClient, url);
            } else {
                http.begin(client, url);
            }
        } else {
            secureClient.setInsecure();
            http.begin(secureClient, url);
        }
        http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
        http.addHeader("Authorization", "Api-Key " + cfg.tts_key);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");

        String voice = (cfg.tts_voice.length() > 0) ? cfg.tts_voice : "filipp";
        uint32_t sampleRate = 16000;

        String encodedText = "";
        for (size_t i = 0; i < text.length(); i++) {
            char c = text[i];
            if (c == ' ') encodedText += '+';
            else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
                encodedText += c;
            } else {
                char hex[4];
                snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
                encodedText += hex;
            }
        }

        String body = "text=" + encodedText +
                      "&lang=ru-RU" +
                      "&voice=" + voice +
                      "&format=lpcm" +
                      "&sampleRateHertz=" + String(sampleRate) +
                      "&speed=1.0";

        int code = http.POST(body);
        Serial.printf("[TTS][Yandex] HTTP %d\n", code);

        bool playResult = false;
        if (code == 200) {
            WiFiClient& stream = http.getStream();
            bool success = writeLpcmToWav(stream, "/resp.wav", sampleRate);
            if (success) {
                Serial.println("[TTS][Yandex] Saved, playing...");
                playResult = playWavFromSD("/resp.wav");
            } else {
                Serial.println("[TTS][Yandex] Stream write failed!");
            }
        } else {
            Serial.printf("[TTS][Yandex] Error %d: %s\n", code, http.getString().c_str());
            if (SD_MMC.exists("/error.wav")) playWavFromSD("/error.wav");
        }
        http.end();
        return playResult;
    }

    // Write raw LPCM stream from Yandex to SD as valid WAV file
    // WAV header is prepended (placeholder size), then PCM written, then header patched
    bool writeLpcmToWav(WiFiClient& stream, const char* filepath, uint32_t sampleRate) {
        File file = SD_MMC.open(filepath, FILE_WRITE);
        if (!file) {
            Serial.printf("[TTS] Failed to open %s for writing\n", filepath);
            return false;
        }

        // Write a placeholder WAV header (44 bytes, sizes filled after)
        // We'll patch sizes at the end
        uint8_t wavHdr[44] = {
            'R','I','F','F', 0,0,0,0,   // ChunkID + ChunkSize (patched later)
            'W','A','V','E',             // Format
            'f','m','t',' ', 16,0,0,0,  // Subchunk1ID + Subchunk1Size=16
            1,0,                         // AudioFormat=PCM
            1,0,                         // NumChannels=1
            0,0,0,0,                     // SampleRate (patched)
            0,0,0,0,                     // ByteRate (patched)
            2,0,                         // BlockAlign=2
            16,0,                        // BitsPerSample=16
            'd','a','t','a', 0,0,0,0    // Subchunk2ID + Subchunk2Size (patched)
        };
        // Patch SampleRate
        wavHdr[24] = sampleRate & 0xFF;
        wavHdr[25] = (sampleRate >> 8) & 0xFF;
        wavHdr[26] = (sampleRate >> 16) & 0xFF;
        wavHdr[27] = (sampleRate >> 24) & 0xFF;
        // ByteRate = SampleRate * NumChannels * BitsPerSample/8 = SR * 1 * 2
        uint32_t byteRate = sampleRate * 2;
        wavHdr[28] = byteRate & 0xFF;
        wavHdr[29] = (byteRate >> 8) & 0xFF;
        wavHdr[30] = (byteRate >> 16) & 0xFF;
        wavHdr[31] = (byteRate >> 24) & 0xFF;

        file.write(wavHdr, 44); // placeholder, will patch sizes after

        // Stream PCM data from network to SD
        const size_t bufSize = 4096;
        uint8_t* buf = nullptr;
        if (psramFound()) {
            buf = (uint8_t*)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
        }
        if (!buf) {
            buf = (uint8_t*)malloc(bufSize);
        }
        if (!buf) { file.close(); return false; }

        uint32_t totalPcm = 0;
        uint32_t startMs = millis();
        while (millis() - startMs < 20000) {
            if (digitalRead(BOOT_BTN_PIN) == LOW) {
                Serial.println("[TTS][Yandex] LPCM write aborted by button press!");
                free(buf);
                file.close();
                return false;
            }

            // Animate face during download
            uint32_t now = millis();
            if (now - lastRedrawMs >= 80) {
                animTick++;
                drawFace(canvas, currentState, animTick);
                canvas.pushSprite(0, 0);
                lastRedrawMs = now;
            }

            int avail = stream.available();
            if (avail > 0) {
                size_t toRead = (avail > (int)bufSize) ? bufSize : (size_t)avail;
                size_t read = stream.readBytes(buf, toRead);
                if (read > 0) {
                    file.write(buf, read);
                    totalPcm += read;
                    startMs = millis(); // reset timeout on data
                }
            } else if (!stream.connected()) {
                break;
            } else {
                delay(5);
            }
        }
        free(buf);

        // Patch WAV header sizes
        uint32_t dataSize = totalPcm;
        uint32_t chunkSize = 36 + dataSize;
        file.seek(4);
        uint8_t sz[4];
        sz[0]=chunkSize&0xFF; sz[1]=(chunkSize>>8)&0xFF; sz[2]=(chunkSize>>16)&0xFF; sz[3]=(chunkSize>>24)&0xFF;
        file.write(sz, 4);
        file.seek(40);
        sz[0]=dataSize&0xFF; sz[1]=(dataSize>>8)&0xFF; sz[2]=(dataSize>>16)&0xFF; sz[3]=(dataSize>>24)&0xFF;
        file.write(sz, 4);
        file.close();

        Serial.printf("[TTS][Yandex] Wrote %u bytes PCM + WAV header\n", totalPcm);
        return totalPcm > 0;
    }

    // ============================================================
    // OPENAI TTS
    // Endpoint: https://api.openai.com/v1/audio/speech
    // Auth: Authorization: Bearer <key>
    // Returns: WAV audio directly (no base64)
    // Free tier: $5 credit on new accounts (~500k chars)
    // Works from Russia: UNCERTAIN (may be geoblocked)
    // ============================================================
    bool synthesizeOpenAI(String text, const MelvinConfig& cfg) {
        if (cfg.tts_key.length() < 10) {
            Serial.println("[TTS] No OpenAI API Key. Speech synthesis skipped.");
            return false;
        }

        const size_t maxTtsChars = 500;
        if (text.length() > maxTtsChars) {
            text = text.substring(0, maxTtsChars);
        }
        Serial.printf("[TTS][OpenAI] Synthesizing %d chars...\n", text.length());

        WiFiClient client;
        client.setTimeout(30000);
        client.setConnectionTimeout(30000);
        WiFiClientSecure secureClient;
        secureClient.setTimeout(30000);
        secureClient.setConnectionTimeout(30000);
        HTTPClient http;
        http.setTimeout(30000);
        http.setConnectTimeout(30000);

        String url = "https://api.openai.com/v1/audio/speech";
        if (cfg.api_proxy.length() > 0) {
            url = cfg.api_proxy + "/openai-tts/v1/audio/speech";
            if (url.startsWith("https://")) {
                secureClient.setInsecure();
                http.begin(secureClient, url);
            } else {
                http.begin(client, url);
            }
        } else {
            secureClient.setInsecure();
            http.begin(secureClient, url);
        }
        http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
        http.addHeader("Authorization", "Bearer " + cfg.tts_key);
        http.addHeader("Content-Type", "application/json");

        String voice = (cfg.tts_voice.length() > 0) ? cfg.tts_voice : "alloy";

        JsonDocument doc;
        doc["model"] = "tts-1";
        doc["input"] = text;
        doc["voice"] = voice;
        doc["response_format"] = "wav";

        String body;
        serializeJson(doc, body);

        int code = http.POST(body);
        Serial.printf("[TTS][OpenAI] HTTP %d\n", code);

        bool playResult = false;
        if (code == 200) {
            WiFiClient& stream = http.getStream();
            bool success = writeStreamToFile(stream, "/resp.wav");
            if (success) {
                Serial.println("[TTS][OpenAI] Saved, playing...");
                playResult = playWavFromSD("/resp.wav");
            } else {
                Serial.println("[TTS][OpenAI] Stream write failed!");
            }
        } else {
            Serial.printf("[TTS][OpenAI] Error %d: %s\n", code, http.getString().c_str());
            if (SD_MMC.exists("/error.wav")) playWavFromSD("/error.wav");
        }
        http.end();
        return playResult;
    }

    // Free Google Translate TTS (Path 3) returning MP3
    bool synthesizeGoogleFree(String text, const MelvinConfig& cfg) {
        Serial.printf("[TTS][GoogleFree] Synthesizing %d chars...\n", text.length());

        WiFiClient client;
        client.setTimeout(30000);
        client.setConnectionTimeout(30000);
        WiFiClientSecure secureClient;
        secureClient.setTimeout(30000);
        secureClient.setConnectionTimeout(30000);
        HTTPClient http;
        http.setTimeout(30000);
        http.setConnectTimeout(30000);

        String encodedText = "";
        for (size_t i = 0; i < text.length(); i++) {
            char c = text[i];
            if (c == ' ') encodedText += '+';
            else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
                 encodedText += c;
            } else {
                 char hex[4];
                 snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
                 encodedText += hex;
            }
        }

        String url = "https://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob&tl=ru&q=" + encodedText;
        if (cfg.api_proxy.length() > 0) {
            url = cfg.api_proxy + "/google-free-tts/translate_tts?ie=UTF-8&client=tw-ob&tl=ru&q=" + encodedText;
        }

        if (url.startsWith("https://")) {
            secureClient.setInsecure();
            http.begin(secureClient, url);
        } else {
            http.begin(client, url);
        }

        http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

        int code = http.GET();
        Serial.printf("[TTS][GoogleFree] HTTP %d\n", code);

        bool playSuccess = false;
        if (code == 200) {
            WiFiClient& stream = http.getStream();
            bool success = writeStreamToFile(stream, "/resp.mp3");
            if (success) {
                Serial.println("[TTS][GoogleFree] Saved MP3, playing...");
                playSuccess = playMp3FromSD("/resp.mp3");
            } else {
                Serial.println("[TTS][GoogleFree] MP3 stream write failed!");
            }
        } else {
            Serial.printf("[TTS][GoogleFree] Error %d: %s\n", code, http.getString().c_str());
            if (SD_MMC.exists("/error.wav")) {
                playWavFromSD("/error.wav");
            }
        }
        http.end();
        return playSuccess;
    }

    // Write raw stream (e.g. WAV) directly from network to SD file
    bool writeStreamToFile(WiFiClient& stream, const char* filepath) {
        File file = SD_MMC.open(filepath, FILE_WRITE);
        if (!file) return false;

        const size_t bufSize = 4096;
        uint8_t* buf = nullptr;
        if (psramFound()) {
            buf = (uint8_t*)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
        }
        if (!buf) {
            buf = (uint8_t*)malloc(bufSize);
        }
        if (!buf) { file.close(); return false; }

        uint32_t totalBytes = 0;
        uint32_t startMs = millis();
        while (millis() - startMs < 20000) {
            if (digitalRead(BOOT_BTN_PIN) == LOW) {
                Serial.println("[TTS] Stream write aborted by button press!");
                free(buf);
                file.close();
                return false;
            }

            uint32_t now = millis();
            if (now - lastRedrawMs >= 80) {
                animTick++;
                drawFace(canvas, currentState, animTick);
                canvas.pushSprite(0, 0);
                lastRedrawMs = now;
            }

            int avail = stream.available();
            if (avail > 0) {
                size_t toRead = (avail > (int)bufSize) ? bufSize : (size_t)avail;
                size_t read = stream.readBytes(buf, toRead);
                if (read > 0) {
                    file.write(buf, read);
                    totalBytes += read;
                    startMs = millis();
                }
            } else if (!stream.connected()) {
                break;
            } else {
                delay(5);
            }
        }
        free(buf);
        file.close();
        Serial.printf("[TTS] Wrote %u bytes to SD\n", totalBytes);
        return totalBytes > 0;
    }
};

#endif
