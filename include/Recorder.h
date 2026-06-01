#ifndef RECORDER_H
#define RECORDER_H

#include <Arduino.h>
#include <driver/i2s_std.h>
#include <SD_MMC.h>
#include <Wire.h>

extern i2s_chan_handle_t rx_handle;

class MelvinRecorder {
private:
    int16_t* phrase_buf = nullptr;
    int phrase_frames = 0;
    const int PHRASE_MAX = 16000 * 10; 
    bool in_speech = false;

public:
    bool begin() {
        if (phrase_buf == nullptr) {
            phrase_buf = (int16_t*)ps_malloc(PHRASE_MAX * sizeof(int16_t));
        }
        return phrase_buf != nullptr;
    }

    void startRecording() {
        phrase_frames = 0;
        in_speech = true;
        Serial.println("[REC] Recording...");
    }

    bool process() {
        if (!in_speech || !rx_handle) return false;
        size_t br = 0;
        int16_t dma_buf[256];
        if (i2s_channel_read(rx_handle, dma_buf, sizeof(dma_buf), &br, pdMS_TO_TICKS(10)) == ESP_OK && br > 0) {
            int n = br / 2;
            for (int i = 0; i < n && phrase_frames < PHRASE_MAX; i++) {
                phrase_buf[phrase_frames++] = dma_buf[i];
            }
            if (phrase_frames >= PHRASE_MAX) return true;
        }
        return false;
    }

    int getFrameCount() { return phrase_frames; }

    int16_t* getLatestFrame() {
        if (phrase_frames < 480) return nullptr;
        return &phrase_buf[phrase_frames - 480];
    }

    float getPeakLevel() {
        if (!rx_handle) return 0;
        size_t br = 0;
        int16_t dma_buf[128];
        float max_val = 0;
        if (i2s_channel_read(rx_handle, dma_buf, sizeof(dma_buf), &br, pdMS_TO_TICKS(5)) == ESP_OK && br > 0) {
            int n = br / 2;
            for (int i = 0; i < n; i++) {
                float val = abs(dma_buf[i]);
                if (val > max_val) max_val = val;
            }
        }
        return max_val / 32768.0f;
    }

    String stopAndSave() {
        in_speech = false;
        if (phrase_frames < 16000 / 2) return ""; 
        File file = SD_MMC.open("/rec.wav", FILE_WRITE);
        if (!file) return "";

        uint32_t dataSize = phrase_frames * 2;
        uint32_t fileSize = dataSize + 36;
        uint32_t sampleRate = 16000;
        uint32_t byteRate = sampleRate * 2;
        uint16_t blockAlign = 2;

        file.write((const uint8_t*)"RIFF", 4);
        file.write((const uint8_t*)&fileSize, 4);
        file.write((const uint8_t*)"WAVEfmt ", 8);
        uint32_t fmtSize = 16; file.write((const uint8_t*)&fmtSize, 4);
        uint16_t fmtTag = 1; file.write((const uint8_t*)&fmtTag, 2);
        uint16_t channels = 1; file.write((const uint8_t*)&channels, 2);
        file.write((const uint8_t*)&sampleRate, 4);
        file.write((const uint8_t*)&byteRate, 4);
        file.write((const uint8_t*)&blockAlign, 2);
        uint16_t bps = 16; file.write((const uint8_t*)&bps, 2);
        file.write((const uint8_t*)"data", 4);
        file.write((const uint8_t*)&dataSize, 4);
        file.write((const uint8_t*)phrase_buf, dataSize);
        file.close();
        
        Serial.printf("[REC] Saved (%d bytes)\n", dataSize);
        return "/rec.wav";
    }
};

#endif
