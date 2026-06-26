#include <Arduino.h>
#include <SD_MMC.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include "MP3DecoderHelix.h"
#include <esp_system.h>


#include "Display.h"
#include "RabbitFace.h"
#include "MelvinState.h"
#include "Config.h"
#include "WebUI.h"
#include "MelvinTTS.h"
#include "Agent.h"
#include "Recorder.h"
#include "esp_vad.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "lwip/netif.h"

// ============================================================
// Russian & ASCII UTF-8 Lowercase Helper (Russian characters support)
// ============================================================
String toLowerRu(String s) {
    for (int i = 0; i < (int)s.length() - 1; i++) {
        uint8_t b0 = s[i], b1 = s[i+1];
        if (b0 == 0xD0 && b1 >= 0x90 && b1 <= 0x9F) { s[i+1] = b1 + 0x20; i++; }
        else if (b0 == 0xD0 && b1 >= 0xA0 && b1 <= 0xAF) { s[i] = 0xD1; s[i+1] = b1 - 0x20; i++; }
        else if (b0 == 0xD0 && b1 == 0x81) { s[i] = 0xD1; s[i+1] = 0x91; i++; }
    }
    for (int i = 0; i < (int)s.length(); i++) {
        if (s[i] >= 'A' && s[i] <= 'Z') {
            s[i] = s[i] + 32;
        }
    }
    return s;
}

// ============================================================
// I2S handles
// ============================================================
i2s_chan_handle_t tx_handle = NULL;
i2s_chan_handle_t rx_handle = NULL;

// Global Helix variables
bool mp3_abort = false;
i2s_chan_handle_t mp3_tx_handle = NULL;
uint32_t lastMp3Rate = 16000;

// ============================================================
// Глобальные объекты
// ============================================================
LGFX              lcd;
LGFX_Sprite       canvas(&lcd);
ConfigManager     configMgr;
MelvinAgent       agent;
MelvinRecorder    recorder;
MelvinTTS         tts;
AsyncWebServer    server(80);
DNSServer         dnsServer;
vad_handle_t      vad_inst = NULL;

#define DNS_PORT 53

RobotState        currentState  = STATE_BOOT;
uint32_t          animTick      = 0;
uint32_t          lastRedrawMs  = 0;
bool              sdReady       = false;
bool              wifiConnected = false;
volatile bool     shouldReboot  = false; // Asynchronous reboot flag (volatile for cross-core safety)
int               vad_processed_frames = 0; // VAD frame index tracker

// ============================================================
// ПИНЫ (SpotPear ESP32-S3-1.54 V2.0)
// ============================================================
#define I2C_SCL_PIN     14
#define I2C_SDA_PIN     15
#define ES8311_ADDR     0x18
#define I2S_MCLK_PIN    GPIO_NUM_16
#define I2S_BCLK_PIN    GPIO_NUM_9
#define I2S_WS_PIN      GPIO_NUM_45
#define I2S_DOUT_PIN    GPIO_NUM_8   
#define I2S_DIN_PIN     GPIO_NUM_10  
#define PA_CTRL_PIN     46
#define BOOT_BTN_PIN    0
#define SAMPLE_RATE     16000
#define REDRAW_MS       80   

// ============================================================
bool playWavFromSD(const char* path);
bool playMp3FromSD(const char* path);
void setState(RobotState s);
void startWebServer();
void startAPMode();
void connectWifi();
void switchToTX();
void switchToRX();

// ============================================================
// ES8311 — Конфигурация из рабочего проекта v6.1
// ============================================================
static bool es_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg); Wire.write(val);
    return Wire.endTransmission() == 0;
}

void initES8311() {
    Serial.println("[CODEC] ES8311 MCLK-Safe Init...");
    
    // 1. Reset
    es_write(0x00, 0x1F); delay(50);
    es_write(0x00, 0x00); delay(50);

    // 2. Clocking (v6.1 Working Values)
    es_write(0x01, 0x3F); // MCLK divider
    es_write(0x02, 0x00);
    es_write(0x03, 0x10);
    es_write(0x04, 0x10);
    es_write(0x05, 0x00);
    es_write(0x06, 0x04);
    es_write(0x07, 0x00);
    es_write(0x08, 0xFF); // LRCK divider = 256 (0xFF + 1 = 256 ratio for 16kHz with 256x MCLK)

    // 3. I2S Format (Fix word length to 16-bit to match I2S)
    es_write(0x09, 0x0C); // DAC format (16-bit)
    es_write(0x0A, 0x0C); // ADC format (16-bit)
    
    // 4. Power & Analog
    es_write(0x0D, 0x01);
    es_write(0x0E, 0x02);
    es_write(0x0F, 0x7F);
    es_write(0x10, 0x00);
    es_write(0x11, 0x7C);

    // 5. ADC (Mic)
    es_write(0x13, 0x10);
    es_write(0x14, 0x16); // Pre-gain = +18dB (вместо 0x1A = +30dB, чтобы убрать шум и ложные срабатывания)
    es_write(0x17, 0xBF); // ADC Digital Volume = 0dB (без буста, вместо +8dB 0xCF)
    es_write(0x1C, 0x6A); // ADC HPF & EQ Bypass configuration (not Mic Gain)

    // 6. DAC & Output Routing (КРИТИЧНО - ИСПРАВЛЕНО ПО GEMINI.md)
    es_write(0x12, 0x00); // Unmute
    es_write(0x31, 0x00);
    es_write(0x32, 0xBF); // Vol MAX
    es_write(0x37, 0x08); // Route DAC to output (GEMINI.md standard)
    es_write(0x45, 0x22); // Driver Gain (GEMINI.md standard)

    // 7. Master Start
    es_write(0x00, 0x80);
    delay(50);
    Serial.println("[CODEC] ES8311 Ready");
}

// ============================================================
// I2S Duplex (16kHz, Stereo Output, Mono Input)
// ============================================================
void i2s_duplex_init() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));

    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
    clk.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    // TX - Stereo Output (ES8311 needs stereo I2S usually)
    i2s_std_config_t tx_cfg = {
        .clk_cfg  = clk,
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_PIN,
            .bclk = I2S_BCLK_PIN,
            .ws   = I2S_WS_PIN,
            .dout = I2S_DOUT_PIN,
            .din  = I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &tx_cfg));

    // RX - Mono Input
    i2s_std_config_t rx_cfg = {
        .clk_cfg  = clk,
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_PIN,
            .bclk = I2S_BCLK_PIN,
            .ws   = I2S_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din  = I2S_DIN_PIN,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &rx_cfg));

    // Enable BOTH channels at startup to ensure continuous MCLK
    i2s_channel_enable(tx_handle); 
    i2s_channel_enable(rx_handle); 
    Serial.println("[I2S] Duplex Ready (TX & RX enabled, MCLK started)");
}

// ============================================================
// WAV Header Parser (ISSUE-004)
// Google TTS WAV может содержать LIST chunk перед data,
// поэтому seek(44) — ненадёжен. Читаем RIFF, fmt, ищем data chunk.
// ============================================================
static bool wavParseHeader(File& file, uint16_t& channels, uint32_t& sampleRate, uint16_t& bitsPerSample, uint32_t& dataSize, uint32_t& dataOffset) {
    char id[4];
    
    // 1. Verify RIFF header
    file.seek(0);
    if (file.read((uint8_t*)id, 4) != 4 || memcmp(id, "RIFF", 4) != 0) {
        Serial.println("[WAV] Not a RIFF file!");
        return false;
    }
    
    // Skip 4 bytes of RIFF chunk size
    file.seek(8);
    // 2. Verify WAVE format
    if (file.read((uint8_t*)id, 4) != 4 || memcmp(id, "WAVE", 4) != 0) {
        Serial.println("[WAV] Not a WAVE file!");
        return false;
    }
    
    uint32_t fileSize = file.size();
    uint32_t pos = 12;
    bool fmtFound = false;
    bool dataFound = false;
    
    while (pos + 8 < fileSize) {
        file.seek(pos);
        if (file.read((uint8_t*)id, 4) != 4) {
            break;
        }
        uint32_t chunkSize = 0;
        if (file.read((uint8_t*)&chunkSize, 4) != 4) {
            break;
        }
        
        if (memcmp(id, "fmt ", 4) == 0) {
            if (chunkSize >= 16) {
                uint16_t audioFormat = 0;
                file.read((uint8_t*)&audioFormat, 2);
                file.read((uint8_t*)&channels, 2);
                file.read((uint8_t*)&sampleRate, 4);
                // Skip byteRate (4) and blockAlign (2)
                file.seek(pos + 8 + 14);
                file.read((uint8_t*)&bitsPerSample, 2);
                
                fmtFound = true;
                Serial.printf("[WAV] fmt chunk: Format=%u, Channels=%u, Rate=%u Hz, Bits=%u\n",
                              audioFormat, channels, sampleRate, bitsPerSample);
                if (audioFormat != 1) {
                    Serial.printf("[WAV] Compression format %u not supported! Only uncompressed PCM (1) is supported.\n", audioFormat);
                    return false;
                }
            }
        } else if (memcmp(id, "data", 4) == 0) {
            dataSize = chunkSize;
            dataOffset = pos + 8;
            dataFound = true;
            Serial.printf("[WAV] data chunk found at offset %u, size %u bytes\n", dataOffset, dataSize);
            break; // Stop parsing after data chunk is found
        }
        
        // Skip this chunk
        uint32_t nextPos = pos + 8 + chunkSize;
        if (chunkSize & 1) nextPos++; // Align to even byte boundary
        
        if (nextPos <= pos || nextPos >= fileSize) {
            Serial.println("[WAV] Malformed chunk size, aborting search!");
            break;
        }
        pos = nextPos;
    }
    
    if (fmtFound && dataFound) {
        return true;
    }
    
    Serial.println("[WAV] Parsing failed! Missing fmt or data chunk.");
    return false;
}

// ============================================================
// Manual WAV Player (Replacing Audio.h)
// ============================================================
bool playWavFromSD(const char* path) {
    switchToTX(); // Switch to TX mode for playback
    if (!sdReady) return false;
    File file = SD_MMC.open(path, FILE_READ);
    if (!file) {
        Serial.printf("[WAV] File not found: %s\n", path);
        return false;
    }

    uint16_t channels = 1;
    uint32_t sampleRate = 16000;
    uint16_t bitsPerSample = 16;
    uint32_t dataSize = 0;
    uint32_t dataOffset = 0;

    if (!wavParseHeader(file, channels, sampleRate, bitsPerSample, dataSize, dataOffset)) {
        Serial.println("[WAV] Failed to parse WAV header!");
        file.close();
        switchToRX();
        return false;
    }

    // Only 16-bit PCM is supported directly, but let's check
    if (bitsPerSample != 16) {
        Serial.printf("[WAV] Unsupported bit depth: %u-bit (only 16-bit is supported)\n", bitsPerSample);
        file.close();
        switchToRX();
        return false;
    }

    // Dynamic I2S Clock Reconfiguration if file sample rate differs
    bool sampleRateChanged = false;
    if (sampleRate != SAMPLE_RATE) {
        Serial.printf("[WAV] Reconfiguring I2S clock to %u Hz\n", sampleRate);
        i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate);
        clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
        esp_err_t err = i2s_channel_reconfig_std_clock(tx_handle, &clk_cfg);
        if (err == ESP_OK) {
            sampleRateChanged = true;
        } else {
            Serial.printf("[WAV] Failed to reconfigure I2S clock: %d\n", err);
        }
    }

    file.seek(dataOffset);
    Serial.printf("[WAV] Playing: %u channels, %u Hz, %u-bit. Data size %u bytes, offset %u\n",
                  channels, sampleRate, bitsPerSample, dataSize, dataOffset);

    const size_t bufSize = 1024;
    // ISSUE-005: Allocate small buffers in internal DMA SRAM instead of PSRAM
    int16_t* wavBuf = (int16_t*)heap_caps_malloc(bufSize * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!wavBuf) {
        Serial.println("[WAV] Failed to allocate wavBuf in DMA SRAM, trying standard heap...");
        wavBuf = (int16_t*)malloc(bufSize * 2);
    }
    if (!wavBuf) {
        file.close();
        if (sampleRateChanged) {
            i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
            clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
            i2s_channel_reconfig_std_clock(tx_handle, &clk_cfg);
        }
        switchToRX();
        return false;
    }

    uint8_t* rawBuf = (uint8_t*)heap_caps_malloc(bufSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!rawBuf) {
        Serial.println("[WAV] Failed to allocate rawBuf in DMA SRAM, trying standard heap...");
        rawBuf = (uint8_t*)malloc(bufSize);
    }
    if (!rawBuf) {
        free(wavBuf);
        file.close();
        if (sampleRateChanged) {
            i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
            clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
            i2s_channel_reconfig_std_clock(tx_handle, &clk_cfg);
        }
        switchToRX();
        return false;
    }

    // Read the first block of data BEFORE turning on the amplifier
    size_t firstRead = file.read(rawBuf, bufSize);
    if (firstRead > 0) {
        size_t bytesToWrite = 0;
        void* dataToWrite = nullptr;
        
        if (channels == 2) {
            bytesToWrite = firstRead;
            dataToWrite = rawBuf;
        } else {
            size_t samples = firstRead / 2;
            int16_t* src = (int16_t*)rawBuf;
            for (size_t i = 0; i < samples; i++) {
                wavBuf[i*2]     = src[i];
                wavBuf[i*2 + 1] = src[i];
            }
            bytesToWrite = samples * 4;
            dataToWrite = wavBuf;
        }
        
        size_t written = 0;
        esp_err_t firstErr = i2s_channel_write(tx_handle, dataToWrite, bytesToWrite, &written, portMAX_DELAY);
        
        // Turn ON the amplifier ONLY если данные фактически переданы в I2S
        if (firstErr == ESP_OK && written > 0) {
            digitalWrite(PA_CTRL_PIN, HIGH);
            delay(10);
        }
    }

    bool aborted = false;
    while (file.available()) {
        if (digitalRead(BOOT_BTN_PIN) == LOW) {
            Serial.println("[WAV] Playback aborted by button press!");
            aborted = true;
            break;
        }

        size_t read = file.read(rawBuf, bufSize);
        if (read == 0) break;

        size_t bytesToWrite = 0;
        void* dataToWrite = nullptr;

        if (channels == 2) {
            bytesToWrite = read;
            dataToWrite = rawBuf;
        } else {
            // Expand Mono→Stereo (16-bit samples)
            size_t samples = read / 2;
            int16_t* src = (int16_t*)rawBuf;
            for (size_t i = 0; i < samples; i++) {
                wavBuf[i*2]     = src[i]; // Left
                wavBuf[i*2 + 1] = src[i]; // Right
            }
            bytesToWrite = samples * 4;
            dataToWrite = wavBuf;
        }

        size_t written = 0;
        esp_err_t err = i2s_channel_write(tx_handle, dataToWrite, bytesToWrite, &written, portMAX_DELAY);
        if (err != ESP_OK) {
            Serial.printf("[WAV] i2s_channel_write error: %d\n", err);
        }
    }

    free(rawBuf);
    free(wavBuf);
    file.close();

    // DMA Drain Flush: Push enough silence to push the valid audio out of the I2S DMA buffers
    // ESP-IDF default DMA is usually 6 buffers of 1024 frames = 24KB.
    // We push 24KB (24 * 1024 bytes) of silence to ensure real audio is actually played!
    static const int16_t silenceBuf[512] = {0}; // 1024 bytes
    size_t _silWritten = 0;
    for (int i = 0; i < 24; i++) {
        i2s_channel_write(tx_handle, silenceBuf, sizeof(silenceBuf), &_silWritten, portMAX_DELAY);
    }

    digitalWrite(PA_CTRL_PIN, LOW); // Mute amplifier immediately
    delay(150);                     // Ждем 150 мс, чтобы переходный процесс/щелчок по питания УНЧ угас
    
    // Restore original sample rate if changed
    if (sampleRateChanged) {
        Serial.printf("[WAV] Restoring I2S clock to %u Hz\n", SAMPLE_RATE);
        i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
        clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
        i2s_channel_disable(tx_handle);
        i2s_channel_reconfig_std_clock(tx_handle, &clk_cfg);
        i2s_channel_enable(tx_handle);
    }

    switchToRX();
    return !aborted;
}

// ============================================================
// MP3 Playback using Helix decoder
// ============================================================
using namespace libhelix;



void helixCallback(MP3FrameInfo &info, int16_t *pcm_buffer, size_t len, void* ref) {
    if (mp3_abort) return;
    if (digitalRead(BOOT_BTN_PIN) == LOW) {
        Serial.println("[MP3] Playback aborted by button press inside decoder callback!");
        mp3_abort = true;
        return;
    }

    // Dynamically adjust I2S sample rate if it changes
    if (info.samprate != lastMp3Rate) {
        Serial.printf("[MP3] Config I2S clock to %d Hz, %d channels\n", info.samprate, info.nChans);
        i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(info.samprate);
        clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
        i2s_channel_disable(mp3_tx_handle);
        esp_err_t err = i2s_channel_reconfig_std_clock(mp3_tx_handle, &clk_cfg);
        i2s_channel_enable(mp3_tx_handle);
        if (err == ESP_OK) {
            lastMp3Rate = info.samprate;
        } else {
            Serial.printf("[MP3] Failed to reconfig I2S clock: %d\n", err);
        }
    }

    size_t written = 0;
    if (info.nChans == 1) {
        // Expand Mono to Stereo
        static int16_t stereoBuf[1152 * 2];
        for (size_t i = 0; i < len; i++) {
            stereoBuf[i*2]     = pcm_buffer[i];
            stereoBuf[i*2 + 1] = pcm_buffer[i];
        }
        i2s_channel_write(mp3_tx_handle, stereoBuf, len * 4, &written, portMAX_DELAY);
    } else {
        // Stereo PCM
        i2s_channel_write(mp3_tx_handle, pcm_buffer, len * 2, &written, portMAX_DELAY);
    }
}

bool playMp3FromSD(const char* path) {
    switchToTX();
    if (!sdReady) return false;
    File file = SD_MMC.open(path, FILE_READ);
    if (!file) {
        Serial.printf("[MP3] File not found: %s\n", path);
        switchToRX();
        return false;
    }

    mp3_tx_handle = tx_handle;
    mp3_abort = false;
    lastMp3Rate = SAMPLE_RATE; // reset

    MP3DecoderHelix mp3(helixCallback);
    mp3.begin();

    // Turn ON amplifier
    digitalWrite(PA_CTRL_PIN, HIGH);
    delay(10);

    const size_t chunk_size = 1024;
    uint8_t* file_buf = (uint8_t*)heap_caps_malloc(chunk_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!file_buf) {
        file_buf = (uint8_t*)malloc(chunk_size);
    }
    if (!file_buf) {
        file.close();
        digitalWrite(PA_CTRL_PIN, LOW);
        switchToRX();
        return false;
    }

    Serial.printf("[MP3] Starting playback of %s...\n", path);

    while (file.available() && !mp3_abort) {
        if (digitalRead(BOOT_BTN_PIN) == LOW) {
            Serial.println("[MP3] Playback aborted by button press!");
            mp3_abort = true;
            break;
        }

        size_t bytesRead = file.read(file_buf, chunk_size);
        if (bytesRead == 0) break;

        mp3.write(file_buf, bytesRead);
    }

    free(file_buf);
    file.close();

    // DMA Drain Flush: Push enough silence
    static const int16_t silenceBuf[512] = {0};
    size_t _silWritten = 0;
    for (int i = 0; i < 24; i++) {
        i2s_channel_write(tx_handle, silenceBuf, sizeof(silenceBuf), &_silWritten, portMAX_DELAY);
    }

    digitalWrite(PA_CTRL_PIN, LOW); // Mute amplifier
    delay(150);

    // Restore standard I2S clock
    Serial.printf("[MP3] Restoring I2S clock to %u Hz\n", SAMPLE_RATE);
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
    clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    i2s_channel_disable(tx_handle);
    i2s_channel_reconfig_std_clock(tx_handle, &clk_cfg);
    i2s_channel_enable(tx_handle);

    switchToRX();

    return !mp3_abort;
}

// ============================================================
// PCM Stream Playback


// ============================================================
// Robot States & UI
// ============================================================
// VAD buffers declared at file scope so setState() can reset the index cleanly
static int16_t s_vad_buf[480] = {0}; // 30ms @ 16kHz
static int     s_vad_buf_idx  = 0;
static int16_t* s_pre_buf     = nullptr;
static int     s_pre_buf_idx  = 0;
static uint32_t wakeCheckStartMs = 0;

void setState(RobotState s) {
    if (currentState == s) return;
    Serial.printf("[FSM] %s → %s\n", stateName(currentState), stateName(s));
    
    // Centralized I2S Mode Switching
    if (s == STATE_IDLE || s == STATE_RECORDING) {
        switchToRX();
    } else if (s == STATE_SPEAKING) {
        switchToTX();
    }
    // STATE_THINKING, STATE_CONNECTING, STATE_CONFIG_AP — I2S не переключаем

    if (s == STATE_RECORDING) {
        vad_processed_frames = 0; // Reset VAD processing frames on recording start
    }
    if (s == STATE_IDLE) {
        s_vad_buf_idx = 0; // Flush stale VAD window on re-entry to IDLE
        s_pre_buf_idx = 0;
    }

    currentState = s;
    // DisplayTask handles drawing asynchronously
}

// ============================================================
// Web Server & WiFi
// ============================================================
void startWebServer() {
    // --------------------------------------------------------
    // GET / — Главная страница (Web UI)
    // --------------------------------------------------------
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", WIFI_PAGE);
    });

    // --------------------------------------------------------
    // GET /api/config — Чтение конфига (все поля)
    // --------------------------------------------------------
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["wifi_ssid"]        = configMgr.config.wifi_ssid;
        doc["gemini_keys"]      = configMgr.config.gemini_keys;
        doc["groq_keys"]        = configMgr.config.groq_keys;
        doc["openrouter_keys"]  = configMgr.config.openrouter_keys;
        doc["yandex_keys"]      = configMgr.config.yandex_keys;
        doc["llm_provider"]     = configMgr.config.llm_provider;
        doc["tts_key"]          = configMgr.config.tts_key;
        doc["tts_provider"]     = configMgr.config.tts_provider;
        doc["tts_provider2"]    = configMgr.config.tts_provider2;
        doc["tts_voice"]        = configMgr.config.tts_voice;
        doc["tts_voice_yandex"] = configMgr.config.tts_voice_yandex;
        doc["tts_voice_google"] = configMgr.config.tts_voice_google;
        doc["personality"]      = configMgr.config.personality;
        doc["wake_word"]        = configMgr.config.wake_word;
        doc["wake_word_enabled"]= configMgr.config.wake_word_enabled;
        doc["vad_silence_ms"]   = configMgr.config.vad_silence_ms;
        doc["rss_url"]          = configMgr.config.rss_url;
        doc["system_prompt"]    = configMgr.config.system_prompt;
        doc["api_proxy"]        = configMgr.config.api_proxy;
        doc["tts_language"]     = configMgr.config.tts_language;
        String body;
        serializeJson(doc, body);
        AsyncWebServerResponse* res = req->beginResponse(200, "application/json", body);
        res->addHeader("Access-Control-Allow-Origin", "*");
        req->send(res);
    });

    // --------------------------------------------------------
    // POST /api/save — Сохранение конфига (все поля) + reboot
    // --------------------------------------------------------
    server.on("/api/save", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            String* body = (String*)req->_tempObject;
            if (body) {
                Serial.printf("[HTTP] POST /api/save: Received full body of size %d bytes\n", body->length());
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, *body);
                if (err == DeserializationError::Ok) {
                    configMgr.config.wifi_ssid        = doc["wifi_ssid"]        | configMgr.config.wifi_ssid;
                    String new_wifi_pass = doc["wifi_pass"] | "";
                    if (new_wifi_pass != "__KEEP__" && new_wifi_pass.length() > 0) {
                        configMgr.config.wifi_pass = new_wifi_pass;
                    }
                    configMgr.config.gemini_keys      = doc["gemini_keys"]      | configMgr.config.gemini_keys;
                    configMgr.config.groq_keys        = doc["groq_keys"]        | configMgr.config.groq_keys;
                    configMgr.config.openrouter_keys  = doc["openrouter_keys"]  | configMgr.config.openrouter_keys;
                    configMgr.config.yandex_keys      = doc["yandex_keys"]      | configMgr.config.yandex_keys;
                    configMgr.config.llm_provider     = doc["llm_provider"]     | configMgr.config.llm_provider;
                    configMgr.config.tts_key          = doc["tts_key"]          | configMgr.config.tts_key;
                    configMgr.config.tts_provider     = doc["tts_provider"]     | configMgr.config.tts_provider;
                    configMgr.config.tts_provider2    = doc["tts_provider2"]    | configMgr.config.tts_provider2;
                    configMgr.config.tts_voice        = doc["tts_voice"]        | configMgr.config.tts_voice;
                    configMgr.config.tts_voice_yandex = doc["tts_voice_yandex"] | configMgr.config.tts_voice_yandex;
                    configMgr.config.tts_voice_google = doc["tts_voice_google"] | configMgr.config.tts_voice_google;
                    configMgr.config.personality      = doc["personality"]      | configMgr.config.personality;
                    configMgr.config.wake_word        = doc["wake_word"]        | configMgr.config.wake_word;
                    configMgr.config.wake_word_enabled = doc["wake_word_enabled"] | configMgr.config.wake_word_enabled;
                    configMgr.config.vad_silence_ms   = doc["vad_silence_ms"]   | configMgr.config.vad_silence_ms;
                    configMgr.config.rss_url          = doc["rss_url"]          | configMgr.config.rss_url;
                    configMgr.config.system_prompt    = doc["system_prompt"]    | configMgr.config.system_prompt;
                    configMgr.config.api_proxy        = doc["api_proxy"]        | configMgr.config.api_proxy;
                    configMgr.config.tts_language     = doc["tts_language"]     | configMgr.config.tts_language;
                    
                    Serial.println("=== SAVING NEW CONFIG ===");
                    Serial.printf("LLM Provider: %s\n", configMgr.config.llm_provider.c_str());
                    Serial.printf("Gemini Key length: %d\n", configMgr.config.gemini_keys.length());
                    Serial.printf("Groq Key length: %d\n", configMgr.config.groq_keys.length());
                    Serial.printf("TTS Provider: %s\n", configMgr.config.tts_provider.c_str());
                    
                    bool success = configMgr.save();
                    if (!success) {
                        Serial.println("[ERROR] Failed to write config.json to SD card!");
                    } else {
                        Serial.println("[CONFIG] Saved to SD card successfully!");
                    }
                    
                    Serial.println("[CONFIG] Saved successfully!");
                    AsyncWebServerResponse* res = req->beginResponse(200, "text/plain", "OK");
                    res->addHeader("Access-Control-Allow-Origin", "*");
                    req->send(res);
                    
                    shouldReboot = true;
                } else {
                    Serial.printf("[HTTP] JSON Deserialization error: %s\n", err.c_str());
                    AsyncWebServerResponse* res = req->beginResponse(400, "text/plain", "Bad Request: JSON parse failed");
                    res->addHeader("Access-Control-Allow-Origin", "*");
                    req->send(res);
                }
                delete body;
                req->_tempObject = nullptr;
            } else {
                Serial.println("[HTTP] POST /api/save: Request body is empty!");
                AsyncWebServerResponse* res = req->beginResponse(400, "text/plain", "Bad Request: No body");
                res->addHeader("Access-Control-Allow-Origin", "*");
                req->send(res);
            }
        },
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                req->_tempObject = new String();
                Serial.printf("[HTTP] POST /api/save: Receiving body, total size %d bytes\n", total);
            }
            String* body = (String*)req->_tempObject;
            if (body) {
                body->concat((const char*)data, len);
            }
        }
    );

    // OPTIONS handler for /api/save (CORS preflight)
    server.on("/api/save", HTTP_OPTIONS, [](AsyncWebServerRequest* req) {
        AsyncWebServerResponse* res = req->beginResponse(204);
        res->addHeader("Access-Control-Allow-Origin", "*");
        res->addHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        res->addHeader("Access-Control-Allow-Headers", "Content-Type");
        req->send(res);
    });

    // --------------------------------------------------------
    // GET /api/status — Состояние системы (heap, uptime, wifi)
    // --------------------------------------------------------
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        uint32_t freeHeap     = esp_get_free_heap_size();
        uint32_t freeInternal = esp_get_free_internal_heap_size();
        doc["heap"]           = freeHeap;
        doc["heap_internal"]  = freeInternal;
        doc["psram"]          = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        doc["uptime"]         = millis() / 1000;
        doc["state"]          = stateName(currentState);
        doc["rssi"]           = WiFi.RSSI();
        doc["ip"]             = WiFi.localIP().toString();
        doc["sd_ok"]          = sdReady;
        doc["sd_total_kb"]    = sdReady ? (uint32_t)(SD_MMC.totalBytes() / 1024) : 0;
        doc["sd_used_kb"]     = sdReady ? (uint32_t)(SD_MMC.usedBytes() / 1024) : 0;
        doc["history_kb"]     = (sdReady && SD_MMC.exists("/history.jsonl"))
                                 ? (uint32_t)(SD_MMC.open("/history.jsonl").size() / 1024) : 0;
        String body;
        serializeJson(doc, body);
        AsyncWebServerResponse* res = req->beginResponse(200, "application/json", body);
        res->addHeader("Access-Control-Allow-Origin", "*");
        req->send(res);
    });

    // --------------------------------------------------------
    // POST /api/restart — Асинхронная перезагрузка
    // --------------------------------------------------------
    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* req) {
        shouldReboot = true; // Безопасно: volatile флаг, обрабатывается в loop()
        AsyncWebServerResponse* res = req->beginResponse(200, "text/plain", "OK");
        res->addHeader("Access-Control-Allow-Origin", "*");
        req->send(res);
    });

    // OPTIONS handler for /api/restart
    server.on("/api/restart", HTTP_OPTIONS, [](AsyncWebServerRequest* req) {
        AsyncWebServerResponse* res = req->beginResponse(204);
        res->addHeader("Access-Control-Allow-Origin", "*");
        res->addHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        res->addHeader("Access-Control-Allow-Headers", "Content-Type");
        req->send(res);
    });

    // --------------------------------------------------------
    // POST /api/reset-wifi — Сброс WiFi + перезагрузка
    // --------------------------------------------------------
    server.on("/api/reset-wifi", HTTP_POST, [](AsyncWebServerRequest* req) {
        configMgr.resetWiFi();
        shouldReboot = true;
        AsyncWebServerResponse* res = req->beginResponse(200, "text/plain", "OK");
        res->addHeader("Access-Control-Allow-Origin", "*");
        req->send(res);
    });

    // OPTIONS handler for /api/reset-wifi
    server.on("/api/reset-wifi", HTTP_OPTIONS, [](AsyncWebServerRequest* req) {
        AsyncWebServerResponse* res = req->beginResponse(204);
        res->addHeader("Access-Control-Allow-Origin", "*");
        res->addHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        res->addHeader("Access-Control-Allow-Headers", "Content-Type");
        req->send(res);
    });

    // --------------------------------------------------------
    // POST /api/delete-history — Удаление файла истории с SD
    // --------------------------------------------------------
    server.on("/api/delete-history", HTTP_POST, [](AsyncWebServerRequest* req) {
        SD_MMC.remove("/history.jsonl");
        AsyncWebServerResponse* res = req->beginResponse(200, "text/plain", "OK");
        res->addHeader("Access-Control-Allow-Origin", "*");
        req->send(res);
    });

    // OPTIONS handler for /api/delete-history
    server.on("/api/delete-history", HTTP_OPTIONS, [](AsyncWebServerRequest* req) {
        AsyncWebServerResponse* res = req->beginResponse(204);
        res->addHeader("Access-Control-Allow-Origin", "*");
        res->addHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        res->addHeader("Access-Control-Allow-Headers", "Content-Type");
        req->send(res);
    });

    // --------------------------------------------------------
    // GET /api/files — Список файлов в корне SD-карты
    // --------------------------------------------------------
    server.on("/api/files", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        File root = SD_MMC.open("/");
        if (root && root.isDirectory()) {
            File entry = root.openNextFile();
            while (entry) {
                if (!entry.isDirectory()) {
                    JsonObject obj = arr.add<JsonObject>();
                    obj["name"] = String(entry.name());
                    obj["size"] = (uint32_t)entry.size();
                }
                entry = root.openNextFile();
            }
            root.close();
        }
        String body;
        serializeJson(doc, body);
        AsyncWebServerResponse* res = req->beginResponse(200, "application/json", body);
        res->addHeader("Access-Control-Allow-Origin", "*");
        req->send(res);
    });

    // --------------------------------------------------------
    // POST /api/test-tts — Тестовое воспроизведение TTS
    // --------------------------------------------------------
    server.on("/api/test-tts", HTTP_POST, [](AsyncWebServerRequest* req) {
        tts.speakAsync("Привет, я Мелвин. TTS работает нормально.", configMgr.config);
        AsyncWebServerResponse* res = req->beginResponse(200, "text/plain", "OK");
        res->addHeader("Access-Control-Allow-Origin", "*");
        req->send(res);
    });

    server.on("/api/test-tts", HTTP_OPTIONS, [](AsyncWebServerRequest* req) {
        AsyncWebServerResponse* res = req->beginResponse(204);
        res->addHeader("Access-Control-Allow-Origin", "*");
        res->addHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        res->addHeader("Access-Control-Allow-Headers", "Content-Type");
        req->send(res);
    });

    server.begin();
}

void startAPMode() {
    Serial.println("[WiFi] Starting Access Point mode...");
    WiFi.disconnect(true, true); // Принудительно разрываем старые соединения и сбрасываем конфиг в памяти
    delay(100);
    WiFi.mode(WIFI_AP);
    WiFi.setAutoReconnect(false); // Отключаем автоподключение в режиме точки доступа
    
    bool apCreated = WiFi.softAP("Melvin-Setup", "melvin123");
    if (apCreated) {
        IPAddress ip = WiFi.softAPIP();
        Serial.print("[WiFi] AP Started Successfully! SSID: Melvin-Setup, Pass: melvin123, IP: ");
        Serial.println(ip);
        
        if (MDNS.begin("melvin")) {
            MDNS.addService("http", "tcp", 80);
            Serial.println("[mDNS] Responder started. Hostname: http://melvin.local");
        }
        
        dnsServer.start(DNS_PORT, "*", ip);
        startWebServer();
        setState(STATE_CONFIG_AP);
    } else {
        Serial.println("[WiFi] FATAL: Failed to create SoftAP!");
        setState(STATE_ERROR);
    }
}

void connectWifi() {
    if (configMgr.config.wifi_ssid.length() < 2) {
        Serial.println("[WiFi] SSID is empty or too short. Switching to AP mode...");
        startAPMode();
        return;
    }
    
    Serial.printf("[WiFi] Connecting to STA: '%s'...\n", configMgr.config.wifi_ssid.c_str());
    setState(STATE_CONNECTING);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false); // CRITICAL: Disable Wi-Fi sleep to prevent TCP connection drops during slow Base64 streaming uploads!
    WiFi.begin(configMgr.config.wifi_ssid.c_str(), configMgr.config.wifi_pass.c_str());
    WiFi.setTxPower(WIFI_POWER_8_5dBm); // Prevent power-induced WiFi disconnects (ASSOC_LEAVE) during heavy TLS TX
    
    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 12000) {
        delay(400);
        animTick++;
        drawFace(canvas, currentState, animTick);
        canvas.pushSprite(0, 0);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        IPAddress ip = WiFi.localIP();
        Serial.print("[WiFi] Connected successfully! IP: ");
        Serial.println(ip);
        
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif != NULL) {
            struct netif *lwip_netif = (struct netif *)esp_netif_get_netif_impl(netif);
            if (lwip_netif != NULL) {
                lwip_netif->mtu = 1300;
                Serial.println("[WiFi] lwIP netif MTU set to 1300");
            }
        }
        
        if (MDNS.begin("melvin")) {
            MDNS.addService("http", "tcp", 80);
            Serial.println("[mDNS] Responder started. Hostname: http://melvin.local");
        }
        
        startWebServer();
        setState(STATE_SPEAKING);
        if (sdReady && SD_MMC.exists("/ready.wav")) playWavFromSD("/ready.wav");
        else if (sdReady && SD_MMC.exists("/hello.wav")) playWavFromSD("/hello.wav");
        setState(STATE_IDLE);
    } else {
        Serial.println("[WiFi] Connection timeout. Switching to AP mode...");
        if (sdReady && SD_MMC.exists("/ap_mode.wav")) playWavFromSD("/ap_mode.wav");
        else if (sdReady && SD_MMC.exists("/hello.wav")) playWavFromSD("/hello.wav");
        startAPMode();
    }
}

// ============================================================
// I2S Switching
// ============================================================
void switchToRX() {
    // DO NOT call i2s_channel_disable(rx_handle) in full-duplex mode!
    // Disabling one channel while the other is active can break MCLK/BCLK sync on ESP32.
    
    // Just flush the RX channel ring buffer to discard any audio captured during playback:
    size_t br = 0;
    int16_t dummy[128];
    int limit = 200; // max 200 reads (200 * 256 bytes = 50KB) to ensure full flush
    while (limit-- > 0 && i2s_channel_read(rx_handle, dummy, sizeof(dummy), &br, 0) == ESP_OK && br > 0) {
        // Discard
    }
}

void switchToTX() {
    // No-op in continuous full-duplex mode to keep MCLK active
}

// ============================================================
// SETUP
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(500); // Give serial monitor time to connect
    Serial.println("\n============================================================");
    Serial.println("[SYSTEM] Melvin (ESP32-S3) Booting Up...");
    Serial.println("============================================================\n");
    // Print crash reason from previous boot
    esp_reset_reason_t reason = esp_reset_reason();
    const char* reasons[] = {"UNKNOWN","POWERON","EXT","SW","PANIC","INT_WDT","TASK_WDT","WDT","DEEPSLEEP","BROWNOUT","SDIO"};
    if (reason < 11) Serial.printf("[BOOT] Reset reason: %s (%d)\n", reasons[reason], reason);
    else Serial.printf("[BOOT] Reset reason: %d\n", reason);

    pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
    pinMode(PA_CTRL_PIN,  OUTPUT);
    digitalWrite(PA_CTRL_PIN, LOW); // Mute at start

    // 1. Display Init (Sequentially in setup to avoid race conditions on startup)
    lcd.init();
    canvas.createSprite(240, 240);
    drawFace(canvas, STATE_BOOT, 0);
    canvas.pushSprite(0, 0);

    // Create DisplayTask for async redrawing only
    xTaskCreatePinnedToCore(
        [](void* arg) {
            while (true) {
                uint32_t now = millis();
                if (now - lastRedrawMs >= REDRAW_MS) {
                    animTick++;
                    drawFace(canvas, currentState, animTick);
                    canvas.pushSprite(0, 0);
                    lastRedrawMs = now;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        },
        "DisplayTask", 4096, NULL, 1, NULL, 0
    );

    // 2. SD Card
    SD_MMC.setPins(17, 18, 21);
    if (SD_MMC.begin("/sdcard", true)) {
        sdReady = true;
        Serial.println("[SD] SD_MMC mounted successfully.");
        configMgr.load();

        // Debug dump config.json
        File f = SD_MMC.open("/config.json", FILE_READ);
        if (f) {
            Serial.println("[CONFIG] Current config.json on SD:");
            while (f.available()) {
                Serial.write(f.read());
            }
            Serial.println();
            f.close();
        } else {
            Serial.println("[CONFIG] config.json not found on SD card! Using default values.");
        }
    } else {
        Serial.println("[SD] FATAL: Failed to mount SD card! Using default built-in configuration.");
    }

    // Always log active configuration at startup
    Serial.println("[CONFIG] Active startup parameters:");
    Serial.printf("  - LLM Provider:  %s\n", configMgr.config.llm_provider.c_str());
    Serial.printf("  - TTS Provider:  %s\n", configMgr.config.tts_provider.c_str());
    Serial.printf("  - Wake Word:     %s\n", configMgr.config.wake_word.c_str());
    Serial.printf("  - API Proxy:     %s\n", configMgr.config.api_proxy.c_str());
    Serial.printf("  - Personality:   %s\n", configMgr.config.personality.c_str());
    Serial.println("------------------------------------------------------------");

    // 3. I2S - Starts MCLK on GPIO16
    i2s_duplex_init();
    delay(200); // Wait for MCLK to stabilize

    // 4. Codec - Init WHILE MCLK is active
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    initES8311();

    // Codec initialized

    // 5. VAD Init
    vad_inst = vad_create(VAD_MODE_2);

    // 6. Recorder (PSRAM allocation — 320KB)
    if (!recorder.begin()) {
        Serial.println("[SETUP] FATAL: Recorder PSRAM alloc failed! Check BOARD_HAS_PSRAM flag.");
        setState(STATE_ERROR);
        // Зависаем с лицом ошибки — дальнейшая работа без аудиобуфера невозможна
        while (true) {
            animTick++;
            drawFace(canvas, STATE_ERROR, animTick);
            canvas.pushSprite(0, 0);
            delay(500);
        }
    }

    // 6b. Pre-buffer allocation (PSRAM allocation — 48KB)
    s_pre_buf = (int16_t*)heap_caps_malloc(24000 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_pre_buf) {
        s_pre_buf = (int16_t*)malloc(24000 * sizeof(int16_t));
    }
    if (!s_pre_buf) {
        Serial.println("[SETUP] FATAL: Pre-buffer allocation failed!");
        setState(STATE_ERROR);
        while (true) {
            animTick++;
            drawFace(canvas, STATE_ERROR, animTick);
            canvas.pushSprite(0, 0);
            delay(500);
        }
    }
    
    // Background auto-reconnection setup
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false); // CRITICAL: Disable Wi-Fi sleep to prevent TCP connection drops during slow Base64 streaming uploads!
    connectWifi();

}

// VAD buffer shared between loop() and setState() for idle-reset
// (VAD buffers declared above setState(), before loop())

void handleRecordingDone() {
    String path = recorder.stopAndSave();
    if (path.length() == 0 || !wifiConnected) {
        setState(STATE_IDLE);
        return;
    }
    setState(STATE_THINKING);
    
    String answer;
    String transcribed = agent.lastTranscription;
    
    if (configMgr.config.api_proxy.length() > 9) {
        answer = agent.askAI(path, configMgr.config);
    } else {
        answer = agent.tryProvider(path, configMgr.config.getEffectivePrompt(),
                                   configMgr.config.llm_provider,
                                   configMgr.config.getActiveKeys(),
                                   configMgr.config);
    }
    
    setState(STATE_SPEAKING);
    
    if (answer.length() == 0 || answer.startsWith("Error")) {
        if (sdReady && SD_MMC.exists("/error.wav")) playWavFromSD("/error.wav");
        else tts.speakAsync("Не удалось получить ответ", configMgr.config);
    } else if (answer == "[PLAY_MP3]") {
        playMp3FromSD("/response.mp3");
    } else if (answer == "[PLAY_WAV]") {
        playWavFromSD("/response.wav");
    } else {
        tts.speakAsync(answer, configMgr.config);
        agent.recordExchange(transcribed, answer);
    }
    
    // We do NOT set STATE_IDLE here.
    // The loop() will monitor tts.tts_playing and transition to IDLE when playback finishes.
}

// ============================================================
// LOOP - With VAD and Auto-Interaction
// ============================================================
static bool recording = false;
static uint32_t silenceStartMs = 0;
static uint32_t recStartMs = 0;

void loop() {
    if (currentState == STATE_CONFIG_AP) dnsServer.processNextRequest();

    uint32_t now = millis();

    // Edge-triggered button detection to start/stop recording cleanly
    static bool lastBtnState = HIGH;
    bool currentBtnState = digitalRead(BOOT_BTN_PIN);
    bool btnClicked = (lastBtnState == HIGH && currentBtnState == LOW);
    lastBtnState = currentBtnState;
    
    // Safely trigger asynchronous reboot outside WebServer thread
    if (shouldReboot) {
        Serial.println("[SYSTEM] Safe rebooting in 500ms...");
        delay(500);
        ESP.restart();
    }



    // --- Heap low-memory warning ---
    static uint32_t lastHeapWarnMs = 0;
    if (now - lastHeapWarnMs > 10000) {
        lastHeapWarnMs = now;
        uint32_t freeHeap = esp_get_free_heap_size();
        if (freeHeap < 51200) {
            Serial.printf("[HEAP] WARNING: Free heap is critically low: %u bytes!\n", freeHeap);
        }
    }

    // --- Asynchronous Background WiFi Reconnect (non-blocking, works in ANY state) ---
    static uint32_t lastWifiCheckMs = 0;
    static int wifiFailCount = 0;
    if (currentState != STATE_CONFIG_AP && now - lastWifiCheckMs > 15000) {
        lastWifiCheckMs = now;
        if (WiFi.status() != WL_CONNECTED) {
            wifiConnected = false;
            Serial.println("[WiFi] Connection lost! Triggering reconnect...");
            WiFi.reconnect(); // Явный реконнект — setAutoReconnect может не сработать после долгого разрыва
            wifiFailCount++;
            if (wifiFailCount >= 20) { // 20 * 15s = 300s = 5 minutes
                wifiFailCount = 0;
                Serial.println("[WiFi] Too many failures, switching to AP mode");
                startAPMode();
            }
        } else {
            wifiFailCount = 0;
            if (!wifiConnected) {
                wifiConnected = true;
                WiFi.setSleep(false); // CRITICAL: Keep Wi-Fi sleep disabled on reconnection!
                esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                if (netif != NULL) {
                    struct netif *lwip_netif = (struct netif *)esp_netif_get_netif_impl(netif);
                    if (lwip_netif != NULL) {
                        lwip_netif->mtu = 1300;
                    }
                }
                Serial.println("[WiFi] Reconnected successfully!");
            }
        }
    }

    // --- Interaction Logic ---
    if (currentState == STATE_SPEAKING && !tts.tts_playing) {
        // Clear out the I2S RX buffer to prevent VAD from triggering on our own echo
        size_t br = 0;
        int16_t dummy[512];
        while (i2s_channel_read(rx_handle, dummy, sizeof(dummy), &br, 0) == ESP_OK && br > 0) {}
        
        setState(STATE_IDLE);
    }

    if (currentState == STATE_IDLE && !tts.tts_playing) {
        // Manual button trigger to start recording
        if (btnClicked) {
            Serial.println("[BUTTON] Manual recording triggered!");
            recorder.startRecording();
            recStartMs = now;
            silenceStartMs = now + 1000;
            setState(STATE_RECORDING);
            return;
        }

        int16_t temp_buf[64];
        size_t br = 0;
        
        // Read with 5ms timeout to block slightly, avoiding stack garbage on partially filled buffers
        if (i2s_channel_read(rx_handle, temp_buf, sizeof(temp_buf), &br, pdMS_TO_TICKS(5)) == ESP_OK && br > 0) {
            int samples_read = br / 2;
            for (int i = 0; i < samples_read; i++) {
                s_vad_buf[s_vad_buf_idx++] = temp_buf[i];
                if (s_vad_buf_idx >= 480) {
                    if (vad_process(vad_inst, s_vad_buf, SAMPLE_RATE, 30) == VAD_SPEECH) {
                        Serial.println("[VAD] Speech detected!");
                        if (!configMgr.config.wake_word_enabled) {
                            recorder.startRecording();
                            recorder.prependBuffer(s_vad_buf, 480);
                            recStartMs = now;
                            silenceStartMs = now;
                            setState(STATE_RECORDING);
                        } else {
                            setState(STATE_WAKE_CHECK);
                            if (s_pre_buf) {
                                memcpy(s_pre_buf, s_vad_buf, 480 * sizeof(int16_t));
                                s_pre_buf_idx = 480;
                            } else {
                                s_pre_buf_idx = 0;
                            }
                            wakeCheckStartMs = now;
                        }
                    }
                    s_vad_buf_idx = 0;
                }
            }
        }
    }

    if (currentState == STATE_WAKE_CHECK) {
        if (btnClicked) {
            Serial.println("[WAKE] Wake check aborted by button, starting manual recording...");
            recorder.startRecording();
            recStartMs = now;
            silenceStartMs = now + 1000;
            setState(STATE_RECORDING);
            return;
        }

        int16_t temp_buf[64];
        size_t br = 0;
        if (i2s_channel_read(rx_handle, temp_buf, sizeof(temp_buf), &br, pdMS_TO_TICKS(5)) == ESP_OK && br > 0) {
            int samples_read = br / 2;
            int space = 24000 - s_pre_buf_idx;
            int to_copy = min(samples_read, space);
            if (to_copy > 0 && s_pre_buf) {
                memcpy(s_pre_buf + s_pre_buf_idx, temp_buf, to_copy * sizeof(int16_t));
                s_pre_buf_idx += to_copy;
            }
        }
        
        // Timeout if transcription or collection takes too long (e.g. 8s)
        if (now - wakeCheckStartMs > 8000) {
            Serial.println("[WAKE] Wake check timeout, returning to IDLE");
            setState(STATE_IDLE);
        }
        else if (s_pre_buf_idx >= 24000) {
            Serial.println("[WAKE] 1.5s of pre-buffer collected, checking wake word...");
            String text = agent.transcribeRaw(s_pre_buf, 24000, configMgr.config);
            text.trim();
            Serial.printf("[WAKE] Transcribed text: '%s'\n", text.c_str());
            
            String cleanText = toLowerRu(text);
            String cleanWakeWord = toLowerRu(configMgr.config.wake_word);
            
            if (cleanText.length() > 0 && cleanText.indexOf(cleanWakeWord) != -1) {
                Serial.println("[WAKE] Wake word detected!");
                if (sdReady && SD_MMC.exists("/beep.wav")) {
                    playWavFromSD("/beep.wav");
                }
                recorder.startRecording();
                recorder.prependBuffer(s_pre_buf, 24000);
                recStartMs = millis();
                silenceStartMs = millis();
                setState(STATE_RECORDING);
                vad_processed_frames = recorder.getFrameCount();
            } else {
                Serial.println("[WAKE] Wake word NOT detected, returning to IDLE");
                setState(STATE_IDLE);
            }
        }
    }

    if (currentState == STATE_RECORDING) {
        bool buffer_full = recorder.process();
        int current_frames = recorder.getFrameCount();

        // Process VAD in contiguous 480-sample (30ms) steps without overlap (Issue 2)
        int16_t* phrase_buf = recorder.getBuffer();
        if (phrase_buf && (current_frames - vad_processed_frames >= 480)) {
            int16_t* frame_ptr = &phrase_buf[vad_processed_frames];
            if (vad_process(vad_inst, frame_ptr, SAMPLE_RATE, 30) == VAD_SILENCE) {
                if (now > silenceStartMs && now - silenceStartMs > configMgr.config.vad_silence_ms) {
                    Serial.println("[VAD] Silence detected → stop");
                    handleRecordingDone();
                    return;
                }
            } else {
                silenceStartMs = now; // Speech detected, reset silence timer
            }
            vad_processed_frames += 480;
        }

        bool btnStop = btnClicked; 
        bool timeout = (now - recStartMs > 10000);
        
        if (buffer_full || btnStop || timeout) {
            Serial.printf("[REC] Stop recording: buffer_full=%d, btnStop=%d, timeout=%d\n", buffer_full, btnStop, timeout);
            handleRecordingDone();
        }
    }

    taskYIELD(); // Уступаем CPU другим задачам FreeRTOS без фиксированного delay
}
