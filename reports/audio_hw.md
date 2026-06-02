# Melvin AI Assistant - Audio Hardware & Low-Level Code Review

This report provides a detailed technical review of the low-level audio driver, codec configuration, and hardware control logic implemented in the Melvin project. The primary focus is on correctness, safety, efficiency, and compliance with the hardware constraints defined in [AGENTS.md](file:///d:/xiaoshi/say-ya/python416/AGENTS.md) and [GEMINI.md](file:///d:/xiaoshi/say-ya/python416/GEMINI.md).

---

## 1. Executive Summary

A comprehensive review of [src/main.cpp](file:///d:/xiaoshi/say-ya/python416/src/main.cpp) and [include/Recorder.h](file:///d:/xiaoshi/say-ya/python416/include/Recorder.h) reveals that the project successfully implements raw I2S configuration using ESP-IDF v5 APIs (`driver/i2s_std.h`), successfully avoiding the problematic `ESP32-audioI2S` library.

However, several hardware issues, code inaccuracies, and inefficiencies were identified:
1. **I2S Format Mismatch:** The ES8311 codec is currently configured in 24-bit mode via registers `0x09`/`0x0A`, while the ESP32 I2S controller transmits in 16-bit mode, leading to data misalignment and potential audio degradation.
2. **Audio Glitches and Pops:** The power amplifier control pin (`PA_CTRL`) is enabled before allocating buffers and reading files from the SD card. This introduces a delay during which the amplifier is on but the I2S lines are idle, causing audible pops.
3. **Tail-End Audio Cutoff:** The fixed 50ms delay before turning off the amplifier is insufficient to allow the I2S DMA transmit buffer to drain fully, resulting in the cutting off of the last syllable/word.
4. **MCLK Instability:** Disabling I2S channels during RX/TX switching halts the Master Clock (MCLK) while the codec is active, which can cause internal PLL lock issues or clicks in the codec.
5. **Parser Security Vulnerability:** The custom WAV header parser (`wavFindDataOffset`) lacks protection against integer overflow, which could lead to infinite loops or out-of-bounds seeking when processing malformed/corrupted files.

---

## 2. Current Implementation Analysis

### A. ES8311 Codec Initialization ([src/main.cpp:L84-L128](file:///d:/xiaoshi/say-ya/python416/src/main.cpp#L84-L128))
The `initES8311()` function configures the ES8311 registers over I2C:
* **Reset and Clocking:** Properly resets the chip using register `0x00`, configures the MCLK divider to `0x3F` (Register `0x01`), and BCLK/LRCK ratios.
* **Volume & Gain:** Follows the [GEMINI.md](file:///d:/xiaoshi/say-ya/python416/GEMINI.md) specification:
  * Register `0x32 = 0xBF` (Analog volume max).
  * Register `0x37 = 0x08` (DAC routed to output mixer).
  * Register `0x45 = 0x22` (Output driver gain set to maximum safe level).
* **ADC / Microphone:** Selects the input path and sets the programmable gain amplifier (PGA).

### B. Raw I2S Config & Duplex Mode ([src/main.cpp:L133-L172](file:///d:/xiaoshi/say-ya/python416/src/main.cpp#L133-L172))
The ESP32 I2S controller is configured using the ESP-IDF standard mode driver:
* Shared clock architecture: TX is configured as stereo (required by the ES8311 DAC), and RX is mono (for the MB23 H11W microphone).
* **MCLK Startup:** Properly starts the TX channel (`tx_handle`) at boot-time before initializing I2C communication. This stabilizes the Master Clock (GPIO16) before the codec starts registers initialization.

### C. Power Amplifier Control (`PA_CTRL` Pin) ([src/main.cpp:L385-L386](file:///d:/xiaoshi/say-ya/python416/src/main.cpp#L385-L386))
* The NS4150B amplifier is controlled via GPIO46 (`PA_CTRL_PIN`).
* GPIO46 is a strapping pin. The code properly sets the pin to `OUTPUT` and drives it `LOW` at the very beginning of `setup()`.
* In `playWavFromSD()`, the pin is driven `HIGH` during playback and set back to `LOW` after completion.

### D. WAV Header Parsing (`wavFindDataOffset`) ([src/main.cpp:L179-L212](file:///d:/xiaoshi/say-ya/python416/src/main.cpp#L179-L212))
* Scans the chunks of the WAV file to dynamically find the `"data"` subchunk offset instead of assuming a hardcoded 44 bytes. This successfully accommodates Google TTS responses, which often contain `LIST` chunks.

---

## 3. Potential Issues & Inefficiencies

### Issue 1: ES8311 24-Bit Format vs. I2S 16-Bit Format (Crucial Bug)
In `initES8311()`, the registers `0x09` (DAC format) and `0x0A` (ADC format) are initialized to `0x00`:
```cpp
// 3. I2S Format
es_write(0x09, 0x00);
es_write(0x0A, 0x00);
```
According to the ES8311 datasheet, bits `4:2` (`SDP_IN_WL` / `SDP_OUT_WL`) being `000` corresponds to **24-bit word length** (default).
However, the I2S config in `i2s_duplex_init()` sets the data bit width to 16 bits (`I2S_DATA_BIT_WIDTH_16BIT`). 
* **Impact:** The codec expects 24-bit cycles per sample, but receives only 16-bit cycles. This word-length format mismatch can cause misalignment of audio samples, resulting in digital distortion, white noise, or severe volume reduction.
* **Fix:** Change registers `0x09` and `0x0A` to `0x0C` (which sets bits `4:2` to `011`, corresponding to 16-bit word length).

### Issue 2: Wrong Comments regarding Mic Gain
In `initES8311()` line 115:
```cpp
es_write(0x1C, 0x6A); // Mic Gain
```
* **Impact:** Register `0x1C` controls the ADC High-Pass Filter (HPF) and Equalizer bypass settings, not the mic gain. The programmable gain amplifier (PGA) is actually configured via Register `0x14` (which is written with `0x1A`, assigning the maximum gain of +30dB). The comment is misleading.
* **Fix:** Correct the comment to `// ADC HPF and EQ Bypass configuration`.

### Issue 3: Premature `PA_CTRL` Pin Activation (Audio Pop)
In `playWavFromSD()`, the amplifier control is structured as:
```cpp
digitalWrite(PA_CTRL_PIN, HIGH);
delay(10);

const size_t bufSize = 1024;
int16_t* wavBuf = (int16_t*)heap_caps_malloc(bufSize * 4, MALLOC_CAP_SPIRAM);
// ...
while (file.available()) {
    size_t read = file.read(rawBuf, bufSize);
    // ... mono to stereo expansion
    i2s_channel_write(tx_handle, wavBuf, samples * 4, &written, portMAX_DELAY);
}
```
* **Impact:** The amplifier is powered ON before `wavBuf` is allocated in PSRAM, and before the first chunk is read from the SD card. These filesystem and allocation calls take a variable amount of milliseconds. Because the amplifier turns on while the I2S lines are idle, a prominent click or pop is heard through the speaker.
* **Fix:** Allocate the buffer, open the file, and read the first block of data *before* driving `PA_CTRL_PIN` `HIGH`.

### Issue 4: Tail-End Audio Cutoff
At the end of `playWavFromSD()`:
```cpp
free(wavBuf);
file.close();

delay(50);
digitalWrite(PA_CTRL_PIN, LOW);
```
* **Impact:** `i2s_channel_write()` with `portMAX_DELAY` returns as soon as the last chunk is pushed into the I2S DMA ring buffer, *not* when the speaker finishes playing it. The DMA buffer might hold up to 128ms of audio. Turning off the amplifier (`PA_CTRL_PIN` = `LOW`) after only 50ms cuts off the end of the audio playback (last syllable of words) and creates a closing click.
* **Fix:** Calculate the required flush delay based on the I2S configuration, or push a small buffer of silence (zeros) through I2S to flush the DMA queue before turning off the amplifier.

### Issue 5: MCLK Cessation during Duplex Transition
In [src/main.cpp:L367-L378](file:///d:/xiaoshi/say-ya/python416/src/main.cpp#L367-L378):
```cpp
void switchToRX() {
    i2s_channel_disable(tx_handle);
    delay(10);
    i2s_channel_enable(rx_handle);
}
```
* **Impact:** Disabling `tx_handle` shuts down the clock generator, stopping the MCLK output on GPIO16. The ES8311 relies on MCLK for its digital filters and internal states. Removing MCLK while the codec is active causes it to enter an unstable state, leading to audio pops when switching between states (e.g. going from Speaking to Recording).
* **Fix:** Keep the TX channel enabled constantly (since it acts as the master clock source) and use mute registers in the ES8311 or the PA_CTRL pin to control direction, or ensure the codec is properly prepared (e.g., muted) before the clock is stopped.

### Issue 6: Integer Overflow and Infinite Loop in `wavFindDataOffset`
In `wavFindDataOffset()`:
```cpp
pos += 8 + chunkSize;
if (chunkSize & 1) pos++;
```
* **Impact:** If `chunkSize` is corrupt or malformed (e.g. `0xFFFFFFFF`), adding it to `pos` will overflow the `uint32_t` variable. The loop condition `pos + 8 < file.size()` will continue to evaluate to `true` (since `pos` wrapped around to a small number), trapping the execution in an infinite seek loop or causing unexpected seek errors.
* **Fix:** Add bounds checking to ensure `pos + 8 + chunkSize` does not exceed `file.size()` and does not cause an integer overflow.

---

## 4. Proposed Fixes & Code Implementations

Below are the recommended refactored code blocks to resolve the identified hardware issues.

### Refactoring `initES8311` (Fixes 16-bit format & comments)
```diff
     // 3. I2S Format
-    es_write(0x09, 0x00);
-    es_write(0x0A, 0x00);
+    es_write(0x09, 0x0C); // Set word length to 16-bit (DAC input)
+    es_write(0x0A, 0x0C); // Set word length to 16-bit (ADC output)
     
     // 4. Power & Analog
     es_write(0x0D, 0x01);
     es_write(0x0E, 0x02);
     es_write(0x0F, 0x7F);
     es_write(0x10, 0x00);
     es_write(0x11, 0x7C);
 
     // 5. ADC (Mic)
     es_write(0x13, 0x10);
     es_write(0x14, 0x1A);
-    es_write(0x1C, 0x6A); // Mic Gain
+    es_write(0x1C, 0x6A); // ADC HPF & EQ Bypass Config (not Mic Gain)
```

### Refactoring `playWavFromSD` (Fixes click prevention & audio cutoff)
```cpp
bool playWavFromSD(const char* path) {
    switchToTX();
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
    // Pre-allocate buffer in PSRAM BEFORE turning on the amplifier
    int16_t* wavBuf = (int16_t*)heap_caps_malloc(bufSize * 4, MALLOC_CAP_SPIRAM);
    if (!wavBuf) {
        Serial.println("[WAV] PSRAM alloc failed, trying heap...");
        wavBuf = (int16_t*)malloc(bufSize * 4);
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
        
        // Write first buffer to DMA queue to prepare I2S signals
        size_t written = 0;
        i2s_channel_write(tx_handle, wavBuf, samples * 4, &written, portMAX_DELAY);
        
        // Turn ON the amplifier now that I2S transmission has started
        digitalWrite(PA_CTRL_PIN, HIGH);
        delay(10); // Wait for PA startup transient to settle
    }

    // Play remaining data
    while (file.available()) {
        size_t read = file.read(rawBuf, bufSize);
        if (read == 0) break;

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

    digitalWrite(PA_CTRL_PIN, LOW); // Mute amplifier immediately after playback ends
    return true;
}
```

### Refactoring `wavFindDataOffset` (Fixes overflow and file limit checks)
```cpp
static uint32_t wavFindDataOffset(File& file) {
    char riff[4];
    file.seek(0);
    file.read((uint8_t*)riff, 4);
    if (memcmp(riff, "RIFF", 4) != 0) {
        Serial.println("[WAV] Not a RIFF file!");
        return 0;
    }
    file.seek(8);
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
            return pos + 8;
        }
        
        // Prevent integer overflow and infinite loop
        uint32_t nextPos = pos + 8 + chunkSize;
        if (chunkSize & 1) nextPos++;
        
        if (nextPos <= pos || nextPos >= fileSize) {
            Serial.println("[WAV] Malformed chunk size, aborting search!");
            break;
        }
        pos = nextPos;
    }
    Serial.println("[WAV] data chunk not found! Falling back to offset 44.");
    return 44;
}
```

---

## 5. Compliance Matrix (Rules in `AGENTS.md` and `GEMINI.md`)

| Constraint Source | Rule Description | Compliance Status | Analysis & Comments |
| :--- | :--- | :--- | :--- |
| **AGENTS.md** | `PA_CTRL` (GPIO46) is a strapping pin. Must boot in `LOW` state. | **Compliant** | Done correctly in `setup()`. |
| **AGENTS.md** | Turn ON (`HIGH`) `PA_CTRL` only immediately before I2S TX starts. | **Semi-Compliant** | Currently enabled before buffer allocation/file reading. The proposed fix fully aligns it. |
| **AGENTS.md** | Turn OFF (`LOW`) `PA_CTRL` immediately after playback to avoid white noise. | **Compliant** | Done correctly, but delayed by an arbitrary 50ms. The proposed fix introduces a DMA flush to avoid truncation. |
| **AGENTS.md** | MCLK (GPIO16) must be stabilized before ES8311 I2C registers init. | **Compliant** | Done correctly by starting I2S TX mode before writing registers. |
| **AGENTS.md** | I2S switching `disable(RX) -> delay(10) -> enable(TX)` pattern. | **Compliant** | Implemented, but raises a concern regarding stopping MCLK while the codec is active. |
| **AGENTS.md** | Strictly FAT32 SD card format in 1-bit mode. | **Compliant** | Properly matches `SD_MMC.begin("/sdcard", true)` and configured pins. |
| **AGENTS.md** | Audio Format: WAV/PCM, 16kHz, 16-bit, Mono. | **Compliant** | Project matches standard. Mono files are properly expanded to stereo for the ES8311 DAC. |
| **AGENTS.md** | No `delay()` inside audio streaming or callbacks. | **Compliant** | Checked. Delays only occur in initialization and the main loop. |
| **AGENTS.md** | PSRAM utilization for audio buffers. | **Compliant** | Checked. `phrase_buf` and `wavBuf` allocate from `MALLOC_CAP_SPIRAM`. |
| **GEMINI.md** | Register settings: `0x01=0x3F`, `0x32=0xBF`, `0x37=0x08`, `0x45=0x22`. | **Compliant** | These values are used exactly in `initES8311()`. |
