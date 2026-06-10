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

#include "Display.h"
#include "RabbitFace.h"
#include "MelvinState.h"
#include "Config.h"
#include "WebUI.h"
#include "MelvinTTS.h"
#include "Agent.h"
#include "Recorder.h"
#include "esp_vad.h"

// ============================================================
// I2S handles
// ============================================================
i2s_chan_handle_t tx_handle = NULL;
i2s_chan_handle_t rx_handle = NULL;

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
// Прототипы
// ============================================================
bool playWavFromSD(const char* path);
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
        if (currentState == STATE_IDLE || currentState == STATE_RECORDING) {
            switchToRX();
        }
        return false;
    }

    // Only 16-bit PCM is supported directly, but let's check
    if (bitsPerSample != 16) {
        Serial.printf("[WAV] Unsupported bit depth: %u-bit (only 16-bit is supported)\n", bitsPerSample);
        file.close();
        if (currentState == STATE_IDLE || currentState == STATE_RECORDING) {
            switchToRX();
        }
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
        if (currentState == STATE_IDLE || currentState == STATE_RECORDING) {
            switchToRX();
        }
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
        if (currentState == STATE_IDLE || currentState == STATE_RECORDING) {
            switchToRX();
        }
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

    while (file.available()) {
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

        // --- Keep Rabbit Face Redraw Ticking ---
        uint32_t now = millis();
        if (now - lastRedrawMs >= REDRAW_MS) {
            animTick++;
            drawFace(canvas, currentState, animTick);
            canvas.pushSprite(0, 0);
            lastRedrawMs = now;
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
        i2s_channel_reconfig_std_clock(tx_handle, &clk_cfg);
    }

    // Switch back to RX if FSM is in listening state (IDLE or RECORDING)
    if (currentState == STATE_IDLE || currentState == STATE_RECORDING) {
        switchToRX(); // Restore listening mode
    }
    return true;
}

// ============================================================
// Robot States & UI
// ============================================================
// VAD buffers declared at file scope so setState() can reset the index cleanly
static int16_t s_vad_buf[480] = {0}; // 30ms @ 16kHz
static int     s_vad_buf_idx  = 0;

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
    }

    currentState = s;
    drawFace(canvas, currentState, animTick);
    canvas.pushSprite(0, 0);
    lastRedrawMs = millis();
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
        doc["tts_voice"]        = configMgr.config.tts_voice;
        doc["personality"]      = configMgr.config.personality;
        doc["wake_word"]        = configMgr.config.wake_word;
        doc["rss_url"]          = configMgr.config.rss_url;
        doc["system_prompt"]    = configMgr.config.system_prompt;
        doc["api_proxy"]        = configMgr.config.api_proxy;
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
                    if (new_wifi_pass.length() > 0) {
                        configMgr.config.wifi_pass = new_wifi_pass;
                    }
                    configMgr.config.gemini_keys      = doc["gemini_keys"]      | configMgr.config.gemini_keys;
                    configMgr.config.groq_keys        = doc["groq_keys"]        | configMgr.config.groq_keys;
                    configMgr.config.openrouter_keys  = doc["openrouter_keys"]  | configMgr.config.openrouter_keys;
                    configMgr.config.yandex_keys      = doc["yandex_keys"]      | configMgr.config.yandex_keys;
                    configMgr.config.llm_provider     = doc["llm_provider"]     | configMgr.config.llm_provider;
                    configMgr.config.tts_key          = doc["tts_key"]          | configMgr.config.tts_key;
                    configMgr.config.tts_provider     = doc["tts_provider"]     | configMgr.config.tts_provider;
                    configMgr.config.tts_voice        = doc["tts_voice"]        | configMgr.config.tts_voice;
                    configMgr.config.personality      = doc["personality"]      | configMgr.config.personality;
                    configMgr.config.wake_word        = doc["wake_word"]        | configMgr.config.wake_word;
                    configMgr.config.rss_url          = doc["rss_url"]          | configMgr.config.rss_url;
                    configMgr.config.system_prompt    = doc["system_prompt"]    | configMgr.config.system_prompt;
                    configMgr.config.api_proxy        = doc["api_proxy"]        | configMgr.config.api_proxy;
                    configMgr.save();
                    
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
    WiFi.begin(configMgr.config.wifi_ssid.c_str(), configMgr.config.wifi_pass.c_str());
    
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
        
        if (MDNS.begin("melvin")) {
            MDNS.addService("http", "tcp", 80);
            Serial.println("[mDNS] Responder started. Hostname: http://melvin.local");
        }
        
        startWebServer();
        setState(STATE_SPEAKING);
        if (SD_MMC.exists("/ready.wav")) {
            playWavFromSD("/ready.wav");
        } else {
            delay(100); // Короткая пауза
        }
        setState(STATE_IDLE);
    } else {
        Serial.println("[WiFi] Connection timeout. Switching to AP mode...");
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

    pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
    pinMode(PA_CTRL_PIN,  OUTPUT);
    digitalWrite(PA_CTRL_PIN, LOW); // Mute at start

    // 1. Display
    lcd.init();
    canvas.createSprite(240, 240);
    drawFace(canvas, STATE_BOOT, 0);
    canvas.pushSprite(0, 0);

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

    // 5. Play startup greeting now that I2S and Codec are fully ready
    if (sdReady && SD_MMC.exists("/hello.wav")) {
        playWavFromSD("/hello.wav");
    }

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
    
    // Background auto-reconnection setup
    WiFi.setAutoReconnect(true);
    connectWifi();
}

// VAD buffer shared between loop() and setState() for idle-reset
// (VAD buffers declared above setState(), before loop())

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

    if (now - lastRedrawMs >= REDRAW_MS) {
        animTick++;
        drawFace(canvas, currentState, animTick);
        canvas.pushSprite(0, 0);
        lastRedrawMs = now;
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
    if (currentState != STATE_CONFIG_AP && now - lastWifiCheckMs > 15000) {
        lastWifiCheckMs = now;
        if (WiFi.status() != WL_CONNECTED) {
            wifiConnected = false;
            Serial.println("[WiFi] Connection lost! Triggering reconnect...");
            WiFi.reconnect(); // Явный реконнект — setAutoReconnect может не сработать после долгого разрыва
        } else if (!wifiConnected) {
            wifiConnected = true;
            Serial.println("[WiFi] Reconnected successfully!");
        }
    }

    // --- Interaction Logic ---
    if (currentState == STATE_IDLE) {
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
                        recorder.startRecording();
                        recStartMs = now;
                        silenceStartMs = now + 1000;
                        setState(STATE_RECORDING);
                    }
                    s_vad_buf_idx = 0;
                }
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
                if (now > silenceStartMs && now - silenceStartMs > 1500) { // 1.5 seconds of silence
                    Serial.println("[VAD] Silence detected → stop");
                    String path = recorder.stopAndSave();
                    
                    if (path.length() > 0 && wifiConnected) {
                        setState(STATE_THINKING);
                        String answer = agent.askAI(path, configMgr.config);
                        setState(STATE_SPEAKING);
                        tts.speak(answer, configMgr.config);
                    }
                    setState(STATE_IDLE);
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
            String path = recorder.stopAndSave();
            
            if (path.length() > 0 && wifiConnected) {
                setState(STATE_THINKING);
                String answer = agent.askAI(path, configMgr.config);
                setState(STATE_SPEAKING);
                tts.speak(answer, configMgr.config);
            }
            setState(STATE_IDLE);
        }
    }

    taskYIELD(); // Уступаем CPU другим задачам FreeRTOS без фиксированного delay
}
