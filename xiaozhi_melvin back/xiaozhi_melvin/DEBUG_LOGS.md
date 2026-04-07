# Melvin AI - Debug Logs & Failure Analysis

This file documents the critical I2S and DMA failures observed during the stabilization of the Melvin AI firmware on the SpotPear MUMA (ESP32-S3) hardware.

## 1. DMA Starvation (Silent Audio)
**Error**: `[E][Audio.cpp:275] bytesWasRead(): readSpace < br, rspc 0, br 1`

**Analysis**: This error sequence, repeating every 100ms, indicates that the MP3 decoder expects to move 1 byte (br 1), but the DMA ringbuffer reports 0 bytes available (rspc 0). This happens because the I2S clocks (MCLK/BCLK/WS) are not successfully driving the ES8311 DAC, causing the output DMA to stall.

```text
[  7367][E][Audio.cpp:275] bytesWasRead(): readSpace < br, rspc 0, br 1
[  7474][E][Audio.cpp:275] bytesWasRead(): readSpace < br, rspc 0, br 1
[  7581][E][Audio.cpp:275] bytesWasRead(): readSpace < br, rspc 0, br 1
... (stalled for 15 seconds) ...
```

## 2. I2S Channel Exhaustion (Crash)
**Error**: `E (37562) i2s_common: i2s_new_channel(1032): no available channel found`

**Analysis**: Occurs when repeatedly calling `new Audio()` and `delete audio`. On the ESP32-S3, the `i2s_std` driver handles are not always fully released by the library destructor, leading to a resource leak. After 2-3 speech cycles, the system runs out of I2S peripherals and crashes with a Null Pointer Dereference.

```text
[AUDIO] TX MODE Start
E (37562) i2s_common: i2s_new_channel(1032): no available channel found
[ 37586][E][Audio.h:632] AUDIO_LOG_IMPL(): Audio.cpp:5782 I2S channel: invalid argument
E (37587) i2s_std: i2s_channel_reconfig_std_gpio(456): input parameter 'handle' is NULL
Guru Meditation Error: Core  1 panic'ed (Interrupt wdt timeout on CPU1). 
```

## 3. Resolution (v3.9.29)
The firmware now uses a **Persistent Global Singleton** for the Audio object. 
- We never `delete` the audio object once created.
- We forcibly re-route the GPIO Matrix using `setPinout` during every Speaker transition to reclaim the clocks from the Microphone.
- A **Deadlock Watchdog** monitors `getAudioCurrentTime()`. If the timer doesn't advance for 1500ms while "running", the system aborts the I2S stream to prevent a WDT reboot.
