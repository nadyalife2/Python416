#ifndef RECORDER_H
#define RECORDER_H

#include <Arduino.h>
#include <driver/i2s_std.h>
#include <SD_MMC.h>
#include <Wire.h>

extern i2s_chan_handle_t rx_handle;

// ============================================================
// WAV format constants (16kHz / 16bit / Mono)
// ============================================================
#define REC_SAMPLE_RATE     16000
#define REC_BIT_DEPTH       16
#define REC_CHANNELS        1
#define REC_BYTE_RATE       (REC_SAMPLE_RATE * REC_CHANNELS * REC_BIT_DEPTH / 8) // = 32000
#define REC_BLOCK_ALIGN     (REC_CHANNELS * REC_BIT_DEPTH / 8)                   // = 2
#define REC_MAX_SECONDS     10
#define REC_PHRASE_MAX      (REC_SAMPLE_RATE * REC_MAX_SECONDS)  // 160000 int16_t = 320KB
#define REC_MIN_FRAMES      (REC_SAMPLE_RATE / 2)                // минимум 0.5 сек

class MelvinRecorder {
private:
    int16_t* phrase_buf   = nullptr;
    int      phrase_frames = 0;
    bool     in_speech    = false;
    static uint32_t rec_counter;  // счётчик для уникальных имён файлов

public:
    bool begin() {
        if (phrase_buf == nullptr) {
            // PSRAM обязателен — 320KB не влезут во внутреннюю heap
            if (psramFound()) {
                phrase_buf = (int16_t*)heap_caps_malloc(
                    REC_PHRASE_MAX * sizeof(int16_t), MALLOC_CAP_SPIRAM);
            }
            if (phrase_buf == nullptr) {
                Serial.println("[REC] ERROR: PSRAM allocation failed or PSRAM not found! (need 320KB)");
                return false;
            }
            Serial.printf("[REC] PSRAM buf allocated: %d bytes, free PSRAM: %u\n",
                REC_PHRASE_MAX * sizeof(int16_t), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }
        return true;
    }

    void startRecording() {
        if (!phrase_buf) {
            Serial.println("[REC] ERROR: buffer not allocated, call begin() first!");
            return;
        }
        phrase_frames = 0;
        in_speech = true;
        Serial.println("[REC] Recording started...");
    }

    void prependBuffer(const int16_t* preroll, size_t samples) {
        if (samples > 0 && phrase_buf) {
            size_t safe_samples = samples;
            if (phrase_frames + safe_samples > REC_PHRASE_MAX) {
                safe_samples = REC_PHRASE_MAX - phrase_frames;
            }
            if (safe_samples > 0) {
                memmove(phrase_buf + safe_samples, phrase_buf, phrase_frames * sizeof(int16_t));
                memcpy(phrase_buf, preroll, safe_samples * sizeof(int16_t));
                phrase_frames += safe_samples;
            }
        }
    }

    bool process() {
        if (!in_speech || !rx_handle || !phrase_buf) return false;
        size_t br = 0;
        int16_t dma_buf[256];
        if (i2s_channel_read(rx_handle, dma_buf, sizeof(dma_buf),
                             &br, pdMS_TO_TICKS(10)) == ESP_OK && br > 0) {
            int n = br / 2;
            for (int i = 0; i < n && phrase_frames < REC_PHRASE_MAX; i++) {
                phrase_buf[phrase_frames++] = dma_buf[i];
            }
            if (phrase_frames >= REC_PHRASE_MAX) {
                Serial.println("[REC] Max recording length reached (10s)");
                return true; // сигнал — буфер полон
            }
        }
        return false;
    }

    int getFrameCount() { return phrase_frames; }

    int16_t* getBuffer() { return phrase_buf; }

    int16_t* getLatestFrame() {
        if (phrase_frames < 480) return nullptr;
        return &phrase_buf[phrase_frames - 480];
    }

    float getPeakLevel() {
        if (!rx_handle) return 0;
        size_t br = 0;
        int16_t dma_buf[128];
        float max_val = 0;
        if (i2s_channel_read(rx_handle, dma_buf, sizeof(dma_buf),
                             &br, pdMS_TO_TICKS(5)) == ESP_OK && br > 0) {
            int n = br / 2;
            for (int i = 0; i < n; i++) {
                float val = abs(dma_buf[i]);
                if (val > max_val) max_val = val;
            }
        }
        return max_val / 32768.0f;
    }

    // ============================================================
    // stopAndSave() — сохраняет PCM в WAV на SD карту
    // Имя файла уникально: /rec_001.wav, /rec_002.wav, ...
    // WAV spec: http://soundfile.sapp.org/doc/WaveFormat/
    // ============================================================
    String stopAndSave() {
        in_speech = false;

        if (phrase_frames < REC_MIN_FRAMES) {
            Serial.printf("[REC] Too short (%d frames < %d min), discarding\n",
                          phrase_frames, REC_MIN_FRAMES);
            return "";
        }
        if (!phrase_buf) return "";

        // TASK-005: уникальное имя файла — не затираем предыдущую запись
        // пока Agent.h ещё читает её для отправки в Gemini
        rec_counter = (rec_counter + 1) % 10; // держим не более 10 файлов
        char path[20];
        snprintf(path, sizeof(path), "/rec_%03lu.wav", rec_counter);

        File file = SD_MMC.open(path, FILE_WRITE);
        if (!file) {
            Serial.printf("[REC] Cannot open %s for write!\n", path);
            return "";
        }

        // --- WAV Header (полный, по спецификации) ---
        // Поле:            Размер   Значение / Описание
        // ChunkID          4        "RIFF"
        // ChunkSize        4        36 + dataSize  (= fileSize - 8)
        // Format           4        "WAVE"
        // Subchunk1ID      4        "fmt "
        // Subchunk1Size    4        16  (PCM)
        // AudioFormat      2        1   (PCM = Linear quantization)
        // NumChannels      2        1   (Mono)
        // SampleRate       4        16000
        // ByteRate         4        32000  (= SampleRate * NumChannels * BitsPerSample/8)
        // BlockAlign       2        2      (= NumChannels * BitsPerSample/8)
        // BitsPerSample    2        16
        // Subchunk2ID      4        "data"
        // Subchunk2Size    4        dataSize  (= NumSamples * NumChannels * BitsPerSample/8)

        uint32_t dataSize   = (uint32_t)phrase_frames * REC_BLOCK_ALIGN; // bytes
        uint32_t chunkSize  = 36 + dataSize; // RIFF ChunkSize = fileSize - 8
        uint32_t sampleRate = REC_SAMPLE_RATE;
        uint32_t byteRate   = REC_BYTE_RATE;
        uint16_t blockAlign = REC_BLOCK_ALIGN;
        uint16_t channels   = REC_CHANNELS;
        uint16_t bps        = REC_BIT_DEPTH;
        uint16_t fmtTag     = 1;     // PCM
        uint32_t fmtSize    = 16;    // PCM fmt chunk is always 16 bytes

        file.write((const uint8_t*)"RIFF",    4);
        file.write((const uint8_t*)&chunkSize, 4);
        file.write((const uint8_t*)"WAVE",    4);
        file.write((const uint8_t*)"fmt ",    4);
        file.write((const uint8_t*)&fmtSize,  4);
        file.write((const uint8_t*)&fmtTag,   2);
        file.write((const uint8_t*)&channels, 2);
        file.write((const uint8_t*)&sampleRate, 4);
        file.write((const uint8_t*)&byteRate, 4);
        file.write((const uint8_t*)&blockAlign, 2);
        file.write((const uint8_t*)&bps,      2);
        file.write((const uint8_t*)"data",    4);
        file.write((const uint8_t*)&dataSize, 4);
        // Итого заголовок: 44 байта (стандартный минимальный WAV)

        // TASK-005: пишем PCM данные чанками по 4KB, а не 320KB одним вызовом
        // Один вызов file.write(320KB) может вызвать watchdog timeout на SD
        const size_t WRITE_CHUNK = 4096;
        uint8_t* ptr = (uint8_t*)phrase_buf;
        uint32_t remaining = dataSize;
        while (remaining > 0) {
            size_t toWrite = (remaining > WRITE_CHUNK) ? WRITE_CHUNK : remaining;
            size_t written = file.write(ptr, toWrite);
            if (written != toWrite) {
                Serial.printf("[REC] SD write error! wrote %d of %d\n", written, toWrite);
                break;
            }
            ptr += written;
            remaining -= written;
        }

        file.close();
        Serial.printf("[REC] Saved %s (%lu bytes PCM, %.1f sec)\n",
                      path, dataSize, (float)phrase_frames / REC_SAMPLE_RATE);
        return String(path);
    }
};

// Статический счётчик — inline C++17, нет ODR-конфликтов при включении из нескольких .cpp
inline uint32_t MelvinRecorder::rec_counter = 0;

#endif
