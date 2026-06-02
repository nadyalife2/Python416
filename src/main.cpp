#include <Arduino.h>
#include <SD_MMC.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include <ArduinoJson.h>

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
bool              shouldReboot  = false; // Asynchronous reboot flag
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
    es_write(0x08, 0x40); // LRCK divider = 64

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
    es_write(0x14, 0x1A);
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

    i2s_channel_enable(tx_handle); 
    Serial.println("[I2S] Duplex Ready (MCLK started)");
}

// ============================================================
// WAV Header Parser (TASK-002)
// Google TTS WAV может содержать LIST chunk перед data,
// поэтому seek(44) — ненадёжен. Ищем "data" subchunk явно.
// ============================================================
static uint32_t wavFindDataOffset(File& file) {
    char riff[4];
    file.seek(0);
    file.read((uint8_t*)riff, 4);
    if (memcmp(riff, "RIFF", 4) != 0) {
        Serial.println("[WAV] Not a RIFF file!");
        return 0;
    }
    file.seek(8); // skip ChunkSize, jump to "WAVE"
    char wave[4];
    file.read((uint8_t*)wave, 4);
    if (memcmp(wave, "WAVE", 4) != 0) {
        Serial.println("[WAV] Not a WAVE file!");
        return 0;
    }
    
    uint32_t fileSize = file.size();
    uint32_t pos = 12;
    while (pos + 8 < fileSize) {
        file.seek(pos);
        char id[4];
        file.read((uint8_t*)id, 4);
        uint32_t chunkSize = 0;
        file.read((uint8_t*)&chunkSize, 4);
        if (memcmp(id, "data", 4) == 0) {
            Serial.printf("[WAV] data chunk at offset %u, size %u bytes\n", pos + 8, chunkSize);
            return pos + 8; // начало PCM данных
        }
        
        // Prevent integer overflow and infinite loop
        uint32_t nextPos = pos + 8 + chunkSize;
        if (chunkSize & 1) nextPos++; // выравнивание на чётный байт
        
        if (nextPos <= pos || nextPos >= fileSize) {
            Serial.println("[WAV] Malformed chunk size, aborting search!");
            break;
        }
        pos = nextPos;
    }
    Serial.println("[WAV] data chunk not found! Falling back to offset 44.");
    return 44;
}

// ============================================================
// Manual WAV Player (Replacing Audio.h)
// ============================================================
bool playWavFromSD(const char* path) {
    if (!sdReady) return false;
    File file = SD_MMC.open(path, FILE_READ);
    if (!file) {
        Serial.printf("[WAV] File not found: %s\n", path);
        return false;
    }

    uint32_t dataOffset = wavFindDataOffset(file);
    if (dataOffset == 0) { file.close(); return false; }
    file.seek(dataOffset);
    Serial.printf("[WAV] Playing from offset %u, file size %u\n", dataOffset, (uint32_t)file.size());

    const size_t bufSize = 1024;
    // Optimized memory allocation (holds 512 stereo samples = 2048 bytes)
    int16_t* wavBuf = (int16_t*)heap_caps_malloc(bufSize * 2, MALLOC_CAP_SPIRAM);
    if (!wavBuf) {
        Serial.println("[WAV] PSRAM alloc failed, trying heap...");
        wavBuf = (int16_t*)malloc(bufSize * 2);
    }
    if (!wavBuf) {
        file.close();
        return false;
    }
    uint8_t rawBuf[bufSize];

    // Read the first block of data BEFORE turning on the amplifier
    size_t firstRead = file.read(rawBuf, bufSize);
    if (firstRead > 0) {
        size_t samples = firstRead / 2;
        int16_t* src = (int16_t*)rawBuf;
        for (size_t i = 0; i < samples; i++) {
            wavBuf[i*2]     = src[i];
            wavBuf[i*2 + 1] = src[i];
        }
        
        size_t written = 0;
        i2s_channel_write(tx_handle, wavBuf, samples * 4, &written, portMAX_DELAY);
        
        // Turn ON the amplifier now that I2S transmission has started
        digitalWrite(PA_CTRL_PIN, HIGH);
        delay(10);
    }

    while (file.available()) {
        size_t read = file.read(rawBuf, bufSize);
        if (read == 0) break;

        // Expand Mono→Stereo (16-bit samples)
        size_t samples = read / 2;
        int16_t* src = (int16_t*)rawBuf;
        for (size_t i = 0; i < samples; i++) {
            wavBuf[i*2]     = src[i]; // Left
            wavBuf[i*2 + 1] = src[i]; // Right
        }

        size_t written = 0;
        esp_err_t err = i2s_channel_write(tx_handle, wavBuf, samples * 4, &written, portMAX_DELAY);
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

    free(wavBuf);
    file.close();

    // DMA Drain Flush: Send 1024 samples of silence to flush the ring buffer before disabling PA
    int16_t* silenceBuf = (int16_t*)calloc(bufSize * 2, sizeof(int16_t));
    if (silenceBuf) {
        size_t written = 0;
        i2s_channel_write(tx_handle, silenceBuf, bufSize * 2 * sizeof(int16_t), &written, portMAX_DELAY);
        free(silenceBuf);
    } else {
        delay(120); // Fallback delay to allow DMA to clear
    }

    digitalWrite(PA_CTRL_PIN, LOW); // Mute amplifier immediately
    return true;
}

// ============================================================
// Robot States & UI
// ============================================================
void setState(RobotState s) {
    if (currentState == s) return;
    Serial.printf("[FSM] %s → %s\n", stateName(currentState), stateName(s));
    
    // Centralized I2S Mode Switching (TASK-003)
    if (s == STATE_IDLE || s == STATE_RECORDING) {
        switchToRX();
    } else {
        switchToTX();
    }

    if (s == STATE_RECORDING) {
        vad_processed_frames = 0; // Reset VAD processing frames on recording start
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
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", WIFI_PAGE);
    });
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["wifi_ssid"]   = configMgr.config.wifi_ssid;
        doc["gemini_keys"] = configMgr.config.gemini_keys;
        doc["tts_key"]     = configMgr.config.tts_key;
        doc["tts_voice"]   = configMgr.config.tts_voice;
        doc["personality"] = configMgr.config.personality;
        doc["wake_word"]   = configMgr.config.wake_word;
        doc["rss_url"]     = configMgr.config.rss_url;
        String body; serializeJson(doc, body);
        req->send(200, "application/json", body);
    });
    server.on("/api/save", HTTP_POST,
        [](AsyncWebServerRequest* req) { req->send(200, "text/plain", "OK"); },
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
                configMgr.config.wifi_ssid   = doc["wifi_ssid"]   | configMgr.config.wifi_ssid;
                configMgr.config.wifi_pass   = doc["wifi_pass"]   | configMgr.config.wifi_pass;
                configMgr.config.gemini_keys = doc["gemini_keys"] | configMgr.config.gemini_keys;
                configMgr.config.tts_key     = doc["tts_key"]     | configMgr.config.tts_key;
                configMgr.config.tts_voice   = doc["tts_voice"]   | configMgr.config.tts_voice;
                configMgr.config.personality = doc["personality"] | configMgr.config.personality;
                configMgr.config.wake_word   = doc["wake_word"]   | configMgr.config.wake_word;
                configMgr.config.rss_url     = doc["rss_url"]     | configMgr.config.rss_url;
                configMgr.save();
            }
            shouldReboot = true; // Asynchronous safe reboot set
        }
    );
    server.begin();
}

void startAPMode() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Melvin-Setup", "melvin123");
    IPAddress ip = WiFi.softAPIP();
    dnsServer.start(DNS_PORT, "*", ip);
    startWebServer();
    setState(STATE_CONFIG_AP);
}

void connectWifi() {
    if (configMgr.config.wifi_ssid.length() < 2) {
        startAPMode();
        return;
    }
    setState(STATE_CONNECTING);
    WiFi.mode(WIFI_STA);
    WiFi.begin(configMgr.config.wifi_ssid.c_str(), configMgr.config.wifi_pass.c_str());
    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 12000) {
        delay(400);
        animTick++;
        drawFace(canvas, currentState, animTick);
        canvas.pushSprite(0, 0);
    }
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        startWebServer();
        setState(STATE_IDLE);
        tts.speak("Я готов к работе!", configMgr.config);
    } else {
        startAPMode();
    }
}

// ============================================================
// I2S Switching
// ============================================================
void switchToRX() {
    i2s_channel_disable(tx_handle);
    delay(10);
    i2s_channel_enable(rx_handle);
}

void switchToTX() {
    i2s_channel_disable(rx_handle);
    delay(10);
    i2s_channel_enable(tx_handle);
}

// ============================================================
// SETUP
// ============================================================
void setup() {
    Serial.begin(115200);
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
        configMgr.load();
    }

    // 3. I2S - Starts MCLK on GPIO16
    i2s_duplex_init();
    delay(200); // Wait for MCLK to stabilize

    // 4. Codec - Init WHILE MCLK is active
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    initES8311();

    // 5. VAD Init
    vad_inst = vad_create(VAD_MODE_3);

    // 6. Recorder (PSRAM allocation — 320KB)
    if (!recorder.begin()) {
        Serial.println("[SETUP] FATAL: Recorder PSRAM alloc failed! Check BOARD_HAS_PSRAM flag.");
    }
    
    // Background auto-reconnection setup
    WiFi.setAutoReconnect(true);
    connectWifi();
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

    // --- Asynchronous Background WiFi Reconnect (non-blocking) ---
    static uint32_t lastWifiCheckMs = 0;
    if (currentState == STATE_IDLE && (now - lastWifiCheckMs > 15000)) {
        lastWifiCheckMs = now;
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Connection lost! Waiting for background auto-reconnect...");
            wifiConnected = false;
        } else if (!wifiConnected) {
            wifiConnected = true;
            Serial.println("[WiFi] Reconnected successfully!");
        }
    }

    // --- Interaction Logic ---
    if (currentState == STATE_IDLE) {
        static int16_t vad_buf[480]; // 30ms at 16kHz
        static int vad_buf_idx = 0;

        int16_t temp_buf[64];
        size_t br = 0;
        
        // Read with 5ms timeout to block slightly, avoiding stack garbage on partially filled buffers
        if (i2s_channel_read(rx_handle, temp_buf, sizeof(temp_buf), &br, pdMS_TO_TICKS(5)) == ESP_OK && br > 0) {
            int samples_read = br / 2;
            for (int i = 0; i < samples_read; i++) {
                vad_buf[vad_buf_idx++] = temp_buf[i];
                if (vad_buf_idx >= 480) {
                    if (vad_process(vad_inst, vad_buf, SAMPLE_RATE, 30) == VAD_SPEECH) {
                        Serial.println("[VAD] Speech detected!");
                        recorder.startRecording();
                        recStartMs = now;
                        silenceStartMs = now;
                        setState(STATE_RECORDING);
                    }
                    vad_buf_idx = 0; // Clear index for next window
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
                if (now - silenceStartMs > 1500) { // 1.5 seconds of silence
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

        bool btnStop = (digitalRead(BOOT_BTN_PIN) == LOW); 
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

    delay(5);
}
