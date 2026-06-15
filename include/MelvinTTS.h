#ifndef MELVINTTS_H
#define MELVINTTS_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <mbedtls/base64.h>
#include <esp_random.h>
#include "MP3DecoderHelix.h"
#include "Config.h"
#include "MelvinState.h"
#include "Display.h"
#include "RabbitFace.h"

using namespace libhelix;

#ifndef PA_CTRL_PIN
#define PA_CTRL_PIN 46
#endif

extern bool playWavFromSD(const char* path);
extern void switchToTX();
extern void switchToRX();
extern i2s_chan_handle_t tx_handle;
extern uint32_t lastRedrawMs;
extern RobotState currentState;
extern uint32_t animTick;
extern LGFX_Sprite canvas;

#define BOOT_BTN_PIN 0
#define SAMPLE_RATE 16000

// Forward declarations for Helix MP3 callback
extern bool mp3_abort;
extern i2s_chan_handle_t mp3_tx_handle;
extern uint32_t lastMp3Rate;
extern void helixCallback(MP3FrameInfo &info, int16_t *pcm_buffer, size_t len, void* ref);

static String urlEncode(const String& src) {
    String dst;
    for (size_t i = 0; i < src.length(); i++) {
        char c = src[i];
        if (c == ' ') dst += '+';
        else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') dst += c;
        else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
            dst += hex;
        }
    }
    return dst;
}

class MelvinTTS {
public:
    bool begin() { return true; }

    void speak(String text, const MelvinConfig& cfg) {
        if (text.length() == 0) return;
        text.replace("*", "");
        text.replace("`", "");

        if (cfg.tts_provider == "yandex") {
            if (!synthesizeYandex(text, cfg)) speakRandomPhrase();
        } else if (cfg.tts_provider == "google_free") {
            // Google Free TTS URL limit ~2000 chars; chunk to 180 for safety
            int len = text.length();
            int start = 0;
            while (start < len) {
                if (digitalRead(BOOT_BTN_PIN) == LOW) break;
                int end = min(start + 180, len);
                // Prefer split at space or punctuation
                if (end < len) {
                    for (int i = end; i > start; i--) {
                        char c = text[i];
                        if (c == ' ' || c == '.' || c == ',' || c == '!' || c == '?' || c == ';') {
                            end = i + 1;
                            break;
                        }
                    }
                }
                String chunk = text.substring(start, end);
                chunk.trim();
                if (chunk.length() > 0) {
                    if (!synthesizeGoogleFree(chunk, cfg)) break;
                }
                start = end;
            }
        } else {
            speakRandomPhrase();
        }
    }

    void speakRandomPhrase() {
        const char* candidates[] = {
            "/hello.wav", "/ready.wav", "/ok.wav", "/beep.wav",
            "/phrase1.wav", "/phrase2.wav", "/phrase3.wav"
        };
        int num = sizeof(candidates) / sizeof(candidates[0]);
        const char* avail[7];
        int cnt = 0;
        for (int i = 0; i < num; i++) {
            if (SD_MMC.exists(candidates[i])) avail[cnt++] = candidates[i];
        }
        if (cnt == 0) {
            Serial.println("[TTS] No WAV files found on SD");
            return;
        }
        int idx = esp_random() % cnt;
        playWavFromSD(avail[idx]);
    }

private:
    // ============================================================
    // STREAMING PCM (Yandex LPCM / any raw PCM source)
    // ============================================================
    bool playPcmStream(WiFiClient& stream, uint32_t sampleRate, bool isMono) {
        switchToTX();
        if (sampleRate != SAMPLE_RATE) {
            i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate);
            clk.mclk_multiple = I2S_MCLK_MULTIPLE_256;
            i2s_channel_disable(tx_handle);
            i2s_channel_reconfig_std_clock(tx_handle, &clk);
            i2s_channel_enable(tx_handle);
        }
        digitalWrite(PA_CTRL_PIN, HIGH);

        const size_t N = 256; // samples per chunk = 16ms @16kHz
        int16_t mono[N];
        int16_t stereo[N * 2];
        uint32_t lastDataMs = millis();

        while (millis() - lastDataMs < 8000) {
            if (digitalRead(BOOT_BTN_PIN) == LOW) {
                Serial.println("[PCM] Aborted by button");
                break;
            }
            if (stream.available() >= (int)(N * 2)) {
                stream.readBytes((uint8_t*)mono, N * 2);
                for (int i = 0; i < (int)N; i++) {
                    stereo[i * 2]     = mono[i];
                    stereo[i * 2 + 1] = mono[i];
                }
                size_t written = 0;
                i2s_channel_write(tx_handle, stereo, N * 4, &written, portMAX_DELAY);
                lastDataMs = millis();
            } else if (!stream.connected()) {
                break;
            } else {
                delay(1);
            }
        }

        // DMA drain
        static const int16_t silence[512] = {0};
        size_t w = 0;
        for (int i = 0; i < 24; i++) {
            i2s_channel_write(tx_handle, (const void*)silence, sizeof(silence), &w, portMAX_DELAY);
        }
        digitalWrite(PA_CTRL_PIN, LOW);
        delay(150);

        if (sampleRate != SAMPLE_RATE) {
            i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
            clk.mclk_multiple = I2S_MCLK_MULTIPLE_256;
            i2s_channel_disable(tx_handle);
            i2s_channel_reconfig_std_clock(tx_handle, &clk);
            i2s_channel_enable(tx_handle);
        }
        switchToRX();
        return true;
    }

    // ============================================================
    // STREAMING MP3 (Google Free / any MP3 source)
    // ============================================================
    bool playMp3Stream(WiFiClient& stream) {
        switchToTX();
        mp3_abort = false;
        mp3_tx_handle = tx_handle;
        lastMp3Rate = SAMPLE_RATE;
        MP3DecoderHelix mp3(helixCallback);
        mp3.begin();
        digitalWrite(PA_CTRL_PIN, HIGH);

        uint8_t buf[512];
        uint32_t lastDataMs = millis();

        while (millis() - lastDataMs < 8000 && !mp3_abort) {
            if (digitalRead(BOOT_BTN_PIN) == LOW) {
                mp3_abort = true;
                break;
            }
            int avail = stream.available();
            if (avail > 0) {
                int r = stream.readBytes(buf, min(avail, 512));
                mp3.write(buf, r);
                lastDataMs = millis();
            } else if (!stream.connected()) {
                break;
            } else {
                delay(1);
            }
        }

        static const int16_t silence[512] = {0};
        size_t w = 0;
        for (int i = 0; i < 24; i++) {
            i2s_channel_write(tx_handle, (const void*)silence, sizeof(silence), &w, portMAX_DELAY);
        }
        digitalWrite(PA_CTRL_PIN, LOW);
        delay(150);

        i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
        clk.mclk_multiple = I2S_MCLK_MULTIPLE_256;
        i2s_channel_disable(tx_handle);
        i2s_channel_reconfig_std_clock(tx_handle, &clk);
        i2s_channel_enable(tx_handle);
        switchToRX();
        return !mp3_abort;
    }

    // ============================================================
    // YANDEX SPEECHKIT — unsafe_mode allows long text in one request
    // ============================================================
    bool synthesizeYandex(String text, const MelvinConfig& cfg) {
        if (cfg.tts_key.length() < 10) {
            Serial.println("[TTS] No Yandex API Key");
            return false;
        }
        Serial.printf("[TTS][Yandex] Synthesizing %d chars...\n", text.length());

        WiFiClientSecure* secureClient = new WiFiClientSecure();
        secureClient->setInsecure();
        HTTPClient* http = new HTTPClient();
        http->setTimeout(30000);
        http->setConnectTimeout(30000);
        http->begin(*secureClient, "https://tts.api.cloud.yandex.net/speech/v1/tts:synthesize");
        http->setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
        http->addHeader("Authorization", "Api-Key " + cfg.tts_key);
        http->addHeader("Content-Type", "application/x-www-form-urlencoded");

        String voice = (cfg.tts_voice.length() > 0) ? cfg.tts_voice : "filipp";
        String body = "text=" + urlEncode(text) +
                      "&lang=ru-RU" +
                      "&voice=" + voice +
                      "&format=lpcm" +
                      "&sampleRateHertz=16000" +
                      "&speed=1.0" +
                      "&unsafe_mode=true";

        int code = http->POST(body);
        Serial.printf("[TTS][Yandex] HTTP %d\n", code);

        bool ok = false;
        if (code == 200) {
            ok = playPcmStream(http->getStream(), 16000, true);
        } else {
            Serial.printf("[TTS][Yandex] Error %d: %s\n", code, http->getString().c_str());
            if (SD_MMC.exists("/error.wav")) playWavFromSD("/error.wav");
        }
        http->end();
        delete http;
        delete secureClient;
        return ok;
    }

    // ============================================================
    // GOOGLE FREE TTS — client=gtx with Referer, streamed via Helix
    // ============================================================
    bool synthesizeGoogleFree(String text, const MelvinConfig& cfg) {
        Serial.printf("[TTS][GoogleFree] Synthesizing %d chars...\n", text.length());

        WiFiClientSecure* secureClient = new WiFiClientSecure();
        secureClient->setInsecure();
        HTTPClient* http = new HTTPClient();
        http->setTimeout(30000);
        http->setConnectTimeout(30000);

        String url = "https://translate.google.com/translate_tts?ie=UTF-8&client=gtx&tl=ru&q=" + urlEncode(text);
        http->begin(*secureClient, url);
        http->setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        http->addHeader("Referer", "https://translate.google.com/");
        http->addHeader("Accept", "audio/mpeg");

        int code = http->GET();
        Serial.printf("[TTS][GoogleFree] HTTP %d\n", code);

        bool ok = false;
        if (code == 200) {
            ok = playMp3Stream(http->getStream());
        } else {
            Serial.printf("[TTS][GoogleFree] Error %d: %s\n", code, http->getString().c_str());
            if (SD_MMC.exists("/error.wav")) playWavFromSD("/error.wav");
        }
        http->end();
        delete http;
        delete secureClient;
        return ok;
    }
};

#endif
