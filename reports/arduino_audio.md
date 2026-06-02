# Melvin AI Robot - Audio Pipeline & VAD Review Report

This report provides a detailed review of the ESP32-S3 Arduino-based audio pipeline for Melvin, focusing on files [Recorder.h](file:///d:/xiaoshi/say-ya/python416/include/Recorder.h) and [main.cpp](file:///d:/xiaoshi/say-ya/python416/src/main.cpp). The review analyzes the VAD-triggered recording pipeline, PSRAM buffer management, WAV header formatting in [stopAndSave()](file:///d:/xiaoshi/say-ya/python416/include/Recorder.h#L102), and hardware/protocol compliance.

---

## 1. Current Implementation Analysis

### 1.1 VAD-Triggered Recording Pipeline
- **VAD Initialization**: In [setup()](file:///d:/xiaoshi/say-ya/python416/src/main.cpp#L382), the VAD instance is initialized via `vad_inst = vad_create(VAD_MODE_3)`. `VAD_MODE_3` represents the most aggressive filtering mode (highest speech detection threshold).
- **Idle Speech Detection**: In [loop()](file:///d:/xiaoshi/say-ya/python416/src/main.cpp#L426), when the system is in `STATE_IDLE`, it attempts to read a 30ms window (480 samples of `int16_t` at 16kHz) from the I2S RX channel (`rx_handle`) using `i2s_channel_read` with a timeout of `0` (non-blocking). If `vad_process` detects speech (`VAD_SPEECH`), the state switches to `STATE_RECORDING`.
- **Recording Process**: In `STATE_RECORDING`, [process()](file:///d:/xiaoshi/say-ya/python416/include/Recorder.h#L56) is called periodically to fetch 256-sample chunks (512 bytes) from I2S and append them to `phrase_buf`.
- **Silence/End-of-Speech Detection**: The system checks for silence by calling [getLatestFrame()](file:///d:/xiaoshi/say-ya/python416/include/Recorder.h#L76), which returns the last 480 samples of the recording buffer. If VAD registers `VAD_SILENCE` continuously for 1500ms, the recording is stopped, saved, and processed by the AI Agent.

### 1.2 Buffer Sizes in PSRAM
- **Phrase Recording Buffer**: The recording buffer `phrase_buf` in [MelvinRecorder](file:///d:/xiaoshi/say-ya/python416/include/Recorder.h#L23) is allocated in PSRAM using `heap_caps_malloc(REC_PHRASE_MAX * sizeof(int16_t), MALLOC_CAP_SPIRAM)` inside [begin()](file:///d:/xiaoshi/say-ya/python416/include/Recorder.h#L31).
- **Size Verification**: 
  - `REC_PHRASE_MAX` is set to `160000` (16kHz * 10 seconds).
  - Each sample is 16-bit (`int16_t`), equivalent to 2 bytes.
  - Total buffer size = $160000 \times 2\text{ bytes} = 320000\text{ bytes}$ (~312.5 KB).
  - This is well within the 2MB/8MB PSRAM limits of typical ESP32-S3 boards and prevents exhausting the limited internal SRAM (~328KB total available for user tasks).
- **Stereo Playback Expansion Buffer**: In `playWavFromSD`, a temporary buffer `wavBuf` of `bufSize * 4` bytes (4KB) is dynamically allocated in PSRAM (with fallback to internal SRAM) to expand mono audio into stereo for the ES8311 DAC.

### 1.3 WAV Header Formatting in [stopAndSave()](file:///d:/xiaoshi/say-ya/python416/include/Recorder.h#L102)
- **Header Structure**: Writes a standard 44-byte WAV (PCM/RIFF) header to the SD card.
- **Parameters Used**:
  - Sample Rate: 16000 Hz
  - Channels: 1 (Mono)
  - Bit Depth: 16 bits
  - Byte Rate: 32000 bytes/sec ($16000 \times 1 \times 2$)
  - Block Align: 2 bytes ($1 \times 2$)
- **Chunk-based Writing**: To avoid blocking the FreeRTOS watchdog during SD operations, the PCM payload is written in 4KB chunks (`WRITE_CHUNK = 4096`) rather than dumping the entire 320KB buffer in a single call.

---

## 2. Potential Issues & Bugs Found

### 2.1 Issue 1: Unsafe Non-Blocking I2S Reads in Idle VAD
In [loop()](file:///d:/xiaoshi/say-ya/python416/src/main.cpp#L426), VAD checking is performed as follows:
```cpp
int16_t vad_buf[480]; // 30ms at 16kHz
size_t br = 0;
if (i2s_channel_read(rx_handle, vad_buf, sizeof(vad_buf), &br, 0) == ESP_OK && br > 0) {
    if (vad_process(vad_inst, vad_buf, SAMPLE_RATE, 30) == VAD_SPEECH) { ... }
}
```
**Problem**: The timeout is set to `0`. If there are fewer than 960 bytes (480 samples) available in the I2S DMA buffer, `i2s_channel_read` will return immediately with `br < 960`. The code checks `br > 0`, meaning it will proceed to call `vad_process` with a partially filled `vad_buf`. The remaining samples in the array will contain uninitialized stack garbage or old data. This leads to erratic voice detection, false triggers, or crash loops in the ESP-VAD library.

### 2.2 Issue 2: Duplicate/Sliding VAD Processing in Recording State
In [loop()](file:///d:/xiaoshi/say-ya/python416/src/main.cpp#L484):
```cpp
int16_t* latest_buf = recorder.getLatestFrame();
if (latest_buf && vad_process(vad_inst, latest_buf, SAMPLE_RATE, 30) == VAD_SILENCE) {
```
**Problem**: The loop runs continuously with a `delay(5)` at the end. During recording, `recorder.process()` reads 256-sample chunks (16ms) from I2S and appends them. Because the loop ticks faster (~5-15ms) than a VAD frame length (30ms), `vad_process` is called multiple times on overlapping or identical frames. 
ESP-VAD is stateful; it updates internal noise profiles and filters based on the assumption that it receives sequential, non-overlapping contiguous chunks of audio. Repeatedly processing duplicate samples shifts the internal noise estimation algorithms and renders the silence detection timer highly unreliable.

### 2.3 Issue 3: Ignored Return Value of [process()](file:///d:/xiaoshi/say-ya/python416/include/Recorder.h#L56)
In `MelvinRecorder::process()`:
```cpp
if (phrase_frames >= REC_PHRASE_MAX) {
    Serial.println("[REC] Max recording length reached (10s)");
    return true; // сигнал — буфер полон
}
```
**Problem**: In `main.cpp`, `recorder.process()` is called as a void function, ignoring its boolean return value. The recording loop relies solely on `bool timeout = (now - recStartMs > 10000)` to stop when full. If the buffer is filled quickly, it stops accepting new data, but the FSM stays in `STATE_RECORDING` until the 10-second timer expires. During this time, the VAD check keeps evaluating the exact same stale 480 samples at the end of the buffer, potentially preventing silence detection.

### 2.4 Issue 4: Permanent Failure to Reconnect to WiFi
In [loop()](file:///d:/xiaoshi/say-ya/python416/src/main.cpp#L437):
```cpp
static uint32_t lastWifiCheckMs = 0;
if (currentState == STATE_IDLE && wifiConnected && (now - lastWifiCheckMs > 15000)) {
    ...
    if (WiFi.status() != WL_CONNECTED) {
        wifiConnected = false;
        ...
```
**Problem**: Once a disconnect happens and the initial reconnect attempt fails, `wifiConnected` is set to `false`. Because the outer condition requires `wifiConnected` to be `true`, the reconnection logic will **never run again** after a single failed attempt, leaving Melvin permanently offline.

### 2.5 Issue 5: Misleading Telemetry in [begin()](file:///d:/xiaoshi/say-ya/python416/include/Recorder.h#L31)
```cpp
Serial.printf("[REC] PSRAM buf allocated: %d bytes, free PSRAM: %d\n",
    REC_PHRASE_MAX * sizeof(int16_t), esp_get_free_heap_size());
```
**Problem**: `esp_get_free_heap_size()` returns the total free internal heap. It does not measure the actual remaining PSRAM space. To accurately monitor PSRAM space, `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` must be used.

---

## 3. Proposed Changes & Fixes

### 3.1 VAD Idle Accumulation Fix
Ensure we read exactly 480 samples (960 bytes) before passing it to `vad_process`:
```cpp
// In STATE_IDLE
static int16_t vad_buf[480];
static int vad_buf_idx = 0;

int16_t temp_buf[64];
size_t br = 0;
if (i2s_channel_read(rx_handle, temp_buf, sizeof(temp_buf), &br, 0) == ESP_OK && br > 0) {
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
```

### 3.2 Recording State VAD Fix
Avoid overlapping or duplicate frame evaluations by keeping track of the processed frame index:
```cpp
// Declare a global or static tracker
static int vad_processed_frames = 0;

// In loop(), inside STATE_RECORDING entry (where startRecording is called):
vad_processed_frames = 0;

// Inside loop() for STATE_RECORDING:
bool buffer_full = recorder.process();
int current_frames = recorder.getFrameCount();

if (current_frames - vad_processed_frames >= 480) {
    int16_t* frame_ptr = &phrase_buf[vad_processed_frames]; // accessible if phrase_buf exposed
    if (vad_process(vad_inst, frame_ptr, SAMPLE_RATE, 30) == VAD_SILENCE) {
        if (now - silenceStartMs > 1500) {
            // Stop and save
        }
    } else {
        silenceStartMs = now; // Speech detected, reset silence timer
    }
    vad_processed_frames += 480;
}

if (buffer_full || (now - recStartMs > 10000)) {
    // Stop and save
}
```

### 3.3 WiFi Reconnection Fix
Modify the check to run periodically regardless of `wifiConnected` status if we expect to be connected:
```cpp
if (currentState == STATE_IDLE && (now - lastWifiCheckMs > 15000)) {
    lastWifiCheckMs = now;
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Connection lost! Reconnecting...");
        wifiConnected = false;
        WiFi.disconnect();
        WiFi.begin(configMgr.config.wifi_ssid.c_str(), configMgr.config.wifi_pass.c_str());
        // ... reconnect check loop ...
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
        }
    }
}
```

---

## 4. Compliance Checks

| Requirement | Reference Source | Status | Comments |
|---|---|---|---|
| **PA_CTRL (GPIO46) low during boot** | [AGENTS.md](file:///d:/xiaoshi/say-ya/python416/AGENTS.md) | **PASS** | Set to `OUTPUT` and `LOW` in [setup()](file:///d:/xiaoshi/say-ya/python416/src/main.cpp#L385-L386). |
| **PA_CTRL driven HIGH immediately before write** | [AGENTS.md](file:///d:/xiaoshi/say-ya/python416/AGENTS.md) | **PASS** | Set to `HIGH` in `playWavFromSD` right before `i2s_channel_write`. |
| **PA_CTRL driven LOW after playback** | [AGENTS.md](file:///d:/xiaoshi/say-ya/python416/AGENTS.md) | **PASS** | Muted to `LOW` in `playWavFromSD` after writing ends. |
| **MCLK active before ES8311 init** | [AGENTS.md](file:///d:/xiaoshi/say-ya/python416/AGENTS.md) / [GEMINI.md](file:///d:/xiaoshi/say-ya/python416/GEMINI.md) | **PASS** | `i2s_duplex_init()` (starts MCLK) is executed and delayed by 200ms before `initES8311()`. |
| **I2S Switch timing (delay 10ms)** | [AGENTS.md](file:///d:/xiaoshi/say-ya/python416/AGENTS.md) | **PASS** | `switchToTX` and `switchToRX` implement `delay(10)` between disabling/enabling handles. |
| **Raw I2S standard APIs (no Audio.h)** | [GEMINI.md](file:///d:/xiaoshi/say-ya/python416/GEMINI.md) | **PASS** | Employs ESP-IDF's `<driver/i2s_std.h>` for both input and output. |
| **Audio Format (16kHz / 16-bit / Mono)** | [AGENTS.md](file:///d:/xiaoshi/say-ya/python416/AGENTS.md) | **PASS** | Configurations for recording, playback search, and VAD are fixed at 16kHz mono. |
| **Dynamic PSRAM Buffering** | [AGENTS.md](file:///d:/xiaoshi/say-ya/python416/AGENTS.md) | **PASS** | The 320KB recording buffer is allocated with `MALLOC_CAP_SPIRAM`. |
| **WAV Header Layout** | [Recorder.h](file:///d:/xiaoshi/say-ya/python416/include/Recorder.h) | **PASS** | Generates compliant 44-byte PCM headers with proper sizes. |
| **WAV Finder robustness** | [main.cpp](file:///d:/xiaoshi/say-ya/python416/src/main.cpp#L179) | **PASS** | Parses `data` chunk explicitly using `wavFindDataOffset()`, avoiding static 44-byte offset assumptions. |
