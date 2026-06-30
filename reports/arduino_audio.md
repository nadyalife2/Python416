# Отчёт: Arduino Audio Pipeline — MelvinRecorder
**Роль:** Arduino Framework & Audio Pipeline Engineer  
**Дата:** 2026-06-05  
**Файлы:** `include/Recorder.h`, `src/main.cpp`

---

## 1. PSRAM буфер (REC_PHRASE_MAX)

### ✅ КОРРЕКТНО

```cpp
// Recorder.h, строка 20
#define REC_PHRASE_MAX  (REC_SAMPLE_RATE * REC_MAX_SECONDS)  // 16000 * 10 = 160000 int16_t
```

- `160000 × sizeof(int16_t) = 320 000 байт = 312.5 KB` — соответствует ТЗ.  
- Выделение через `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` — **правильно**.  
- Проверка `nullptr` с выводом ошибки — **правильно**.  
- В `setup()` вызывается `recorder.begin()` с обработкой `false` — **правильно**.

---

## 2. VAD буфер: накопление 480 сэмплов (30ms @ 16kHz)

### ✅ КОРРЕКТНО (STATE_IDLE)

```cpp
// main.cpp, строки 555–576
static int16_t vad_buf[480]; // 30ms at 16kHz
static int vad_buf_idx = 0;

int16_t temp_buf[64];
// ... читаем по 64 сэмпла, накапливаем в vad_buf ...
if (vad_buf_idx >= 480) {
    if (vad_process(vad_inst, vad_buf, SAMPLE_RATE, 30) == VAD_SPEECH) { ... }
    vad_buf_idx = 0;
}
```

- Накапливается ровно **480 сэмплов** перед вызовом `vad_process()` — соответствует требованию.
- Чтение чанками по 64 сэмпла, аккумуляция в статический буфер — безопасно, без переполнения.
- После обработки `vad_buf_idx = 0` — буфер корректно сбрасывается.

### ✅ КОРРЕКТНО (STATE_RECORDING)

```cpp
// main.cpp, строки 586–606
if (phrase_buf && (current_frames - vad_processed_frames >= 480)) {
    int16_t* frame_ptr = &phrase_buf[vad_processed_frames];
    if (vad_process(vad_inst, frame_ptr, SAMPLE_RATE, 30) == VAD_SILENCE) { ... }
    vad_processed_frames += 480;
}
```

- Шаги ровно **480 сэмплов**, без наложения окон — соответствует требованию Issue 2.
- `vad_processed_frames` сбрасывается в 0 в `setState(STATE_RECORDING)` (строка 340) — правильно.

---

## 3. Логика STATE_RECORDING: 1.5 сек тишины → stopAndSave()

### ⚠️ ПРОБЛЕМА: silenceStartMs не отсчитывает реальную тишину

```cpp
// main.cpp, строка 589
if (now - silenceStartMs > 1500) { // 1.5 seconds of silence
```

**Условие запускается, только если текущий 30ms фрейм — SILENCE.**  
Если последовательно: `SPEECH → SILENCE → SPEECH → SILENCE`, таймер корректно сбрасывается.  
Но если очередной 30ms фрейм пришёл слишком поздно из-за задержки I2S,  
`silenceStartMs` может уже истечь к моменту проверки, хотя реальных 1.5 с тишины не было.

### ⚠️ ПРОБЛЕМА: silenceStartMs обновляется внутри `process()`, но только при SILENCE фрейме

Когда VAD говорит `SILENCE`, таймер **не сбрасывается** (не обновляется `silenceStartMs`).  
Когда VAD говорит `SPEECH` — `silenceStartMs = now` (строка 603). Это **корректная** семантика «сброс таймера при речи».

**Вывод:** логика в целом корректна. Риск: если `loop()` нагружен долгой задачей (например, отрисовкой) и 480-сэмпловые фреймы не успевают проверяться, тишина может продлиться дольше 1.5 с прежде чем проверка произойдёт.  
Критической ошибки нет, но рекомендуется уменьшить `delay(5)` или убрать его.

### ✅ Рекомендация

```cpp
// main.cpp, строка 625
// delay(5);  ← удалить или заменить на yield()
yield(); // Позволяет RTOS обслуживать фоновые задачи без блокировки
```

---

## 4. WAV заголовок в stopAndSave()

### ✅ ПОЛНОСТЬЮ КОРРЕКТНО

| Поле | Значение | Ожидается | Статус |
|---|---|---|---|
| ChunkID | `"RIFF"` | `"RIFF"` | ✅ |
| ChunkSize | `36 + dataSize` | `fileSize - 8` | ✅ |
| Format | `"WAVE"` | `"WAVE"` | ✅ |
| Subchunk1ID | `"fmt "` | `"fmt "` | ✅ |
| Subchunk1Size | `16` | `16` (PCM) | ✅ |
| AudioFormat | `1` (PCM) | `1` | ✅ |
| NumChannels | `1` (Mono) | `1` | ✅ |
| SampleRate | `16000` | `16000` | ✅ |
| ByteRate | `32000` | `32000` | ✅ |
| BlockAlign | `2` | `2` | ✅ |
| BitsPerSample | `16` | `16` | ✅ |
| Subchunk2ID | `"data"` | `"data"` | ✅ |
| Subchunk2Size | `phrase_frames × 2` | корректно | ✅ |

Запись PCM чанками по 4KB (`WRITE_CHUNK = 4096`) — защищает от watchdog.  
Итоговый заголовок: **44 байта** — стандартный минимальный WAV.

### ⚠️ НЕЗНАЧИТЕЛЬНО: формат строки `snprintf`

```cpp
// Recorder.h, строка 118
snprintf(path, sizeof(path), "/rec_%03lu.wav", rec_counter);
```

`rec_counter` — тип `uint32_t`. Спецификатор `%lu` корректен для `unsigned long` на большинстве платформ ESP32, но строго говоря, для `uint32_t` правильнее `PRIu32`:

```cpp
#include <inttypes.h>
snprintf(path, sizeof(path), "/rec_%03" PRIu32 ".wav", rec_counter);
```

На ESP32-S3 это некритично — `uint32_t == unsigned long`, но для переносимости рекомендуется исправить.

---

## 5. WiFi Reconnect логика в loop()

### ✅ КОРРЕКТНО (базовый механизм)

```cpp
// main.cpp, строки 530–541
static uint32_t lastWifiCheckMs = 0;
if (currentState == STATE_IDLE && (now - lastWifiCheckMs > 15000)) {
    lastWifiCheckMs = now;
    if (WiFi.status() != WL_CONNECTED) {
        wifiConnected = false;
    } else if (!wifiConnected) {
        wifiConnected = true;
    }
}
```

- Проверка раз в 15 секунд — разумный интервал, не нагружает цикл.
- `WiFi.setAutoReconnect(true)` в `setup()` — IDF фреймворк сам переподключится.
- `wifiConnected` синхронизируется с реальным состоянием через `WiFi.status()`.

### ⚠️ ПРОБЛЕМА: reconnect только в STATE_IDLE

Проверка обёрнута в `if (currentState == STATE_IDLE)`. Если робот долго находится в `STATE_THINKING` или `STATE_SPEAKING` (например, TTS занимает 10+ секунд), WiFi потеря не будет обнаружена своевременно.

**Рекомендация:**

```cpp
// Убрать условие STATE_IDLE, проверять всегда:
if (now - lastWifiCheckMs > 15000) {
    lastWifiCheckMs = now;
    if (WiFi.status() != WL_CONNECTED) {
        wifiConnected = false;
        Serial.println("[WiFi] Connection lost! AutoReconnect in progress...");
    } else if (!wifiConnected) {
        wifiConnected = true;
        Serial.println("[WiFi] Reconnected successfully!");
    }
}
```

### ⚠️ ПРОБЛЕМА: нет fallback при длительном отсутствии WiFi

Если WiFi не восстанавливается, робот молча продолжает записывать и отбрасывать ответы (`if (wifiConnected)` — агент не вызывается). Рекомендуется добавить визуальный сигнал или голосовое уведомление.

---

## 6. I2S RX → PSRAM пайплайн в process()

### ⚠️ ПОТЕНЦИАЛЬНАЯ ПРОБЛЕМА: getPeakLevel() конкурирует с process()

```cpp
// Recorder.h, строки 83–97
float getPeakLevel() {
    if (!rx_handle) return 0;
    size_t br = 0;
    int16_t dma_buf[128];
    if (i2s_channel_read(rx_handle, dma_buf, sizeof(dma_buf), &br, pdMS_TO_TICKS(5)) == ESP_OK ...
```

`getPeakLevel()` **читает из rx_handle напрямую**, пропуская эти сэмплы из `process()`. Если `getPeakLevel()` вызывается одновременно с записью, **сэмплы теряются** из phrase_buf, нарушая целостность аудио.

**Риск:** проверить, вызывается ли `getPeakLevel()` во время STATE_RECORDING. Если да — убрать вызов или заменить логикой анализа уже записанных данных.

### ✅ process() — накопление корректно

```cpp
// Recorder.h, строки 59–71
int16_t dma_buf[256]; // 512 байт на стеке — допустимо
if (i2s_channel_read(rx_handle, dma_buf, sizeof(dma_buf), &br, pdMS_TO_TICKS(10)) == ESP_OK && br > 0) {
    int n = br / 2;
    for (int i = 0; i < n && phrase_frames < REC_PHRASE_MAX; i++) {
        phrase_buf[phrase_frames++] = dma_buf[i];
    }
```

- `dma_buf[256]` (int16_t) = 512 байт — на стеке, не в ISR, допустимо.
- Проверка `phrase_frames < REC_PHRASE_MAX` — защита от переполнения.
- Таймаут `pdMS_TO_TICKS(10)` — не блокирует надолго.

---

## 7. getLatestFrame() — использование в VAD

```cpp
// Recorder.h, строки 78–81
int16_t* getLatestFrame() {
    if (phrase_frames < 480) return nullptr;
    return &phrase_buf[phrase_frames - 480];
}
```

> **Замечание:** `getLatestFrame()` **не используется** в main.cpp. VAD в STATE_RECORDING работает через прямой доступ к `phrase_buf[vad_processed_frames]`, что корректнее (не пересекающиеся окна). Метод `getLatestFrame()` возвращает **скользящее окно** (последние 480), что вызвало бы наложение окон — это была бы ошибка. Хорошо, что он не используется.

**Рекомендация:** Удалить или задокументировать `getLatestFrame()` как устаревший.

---

## 8. PA_CTRL (GPIO46 strapping pin)

### ✅ КОРРЕКТНО

```cpp
// main.cpp, строка 449
digitalWrite(PA_CTRL_PIN, LOW); // Mute at start

// playWavFromSD(), строки 270–272
i2s_channel_write(tx_handle, wavBuf, samples * 4, &written, portMAX_DELAY);
digitalWrite(PA_CTRL_PIN, HIGH); // HIGH только после начала I2S

// main.cpp, строка 316
digitalWrite(PA_CTRL_PIN, LOW); // LOW после окончания
```

- LOW при boot — ✅  
- HIGH только после первого `i2s_channel_write` — ✅ (защита от pop-noise)  
- LOW после воспроизведения — ✅  
- DMA flush (1024 сэмпла тишины) перед LOW — ✅ устраняет click при выключении  

---

## 9. MCLK активен до initES8311()

### ✅ КОРРЕКТНО

```cpp
// setup(), строки 478–483
i2s_duplex_init();    // ← MCLK стартует здесь (GPIO16)
delay(200);           // Ждём стабилизации MCLK
Wire.begin(...);
initES8311();         // ← I2C конфигурация ПОСЛЕ MCLK
```

Порядок инициализации соответствует критическому требованию: MCLK активен до I2C конфигурации ES8311.

---

## 10. Счётчик файлов rec_counter

### ⚠️ ПРОБЛЕМА: rec_counter не сохраняется между перезагрузками

```cpp
// Recorder.h, строка 116
rec_counter = (rec_counter + 1) % 10;
```

После reboot `rec_counter = 0`, и файлы начинают перезаписываться с `/rec_001.wav`. Если `Agent.h` ещё читает старый файл для отправки в Gemini — конфликт.

**Рекомендация:** Сканировать SD при `begin()` и инициализировать счётчик из максимального существующего номера:

```cpp
bool begin() {
    // ... PSRAM alloc ...
    // Найти максимальный существующий номер файла
    for (uint32_t i = 0; i < 10; i++) {
        char path[20];
        snprintf(path, sizeof(path), "/rec_%03" PRIu32 ".wav", i);
        if (SD_MMC.exists(path)) rec_counter = i;
    }
    return true;
}
```

---

## 11. Стековые буферы в loop()

```cpp
// main.cpp, строки 555–558
static int16_t vad_buf[480]; // 30ms at 16kHz  ← static! OK
static int vad_buf_idx = 0;   //                  ← static! OK
int16_t temp_buf[64];         //                  ← на стеке, 128 байт, OK
```

`vad_buf` и `vad_buf_idx` — `static`, не пересоздаются каждую итерацию — ✅.

---

## Итоговая таблица

| # | Проверка | Статус | Серьёзность |
|---|---|---|---|
| 1 | PSRAM буфер 320KB | ✅ Корректно | — |
| 2 | VAD: ровно 480 сэмплов перед vad_process() | ✅ Корректно | — |
| 3 | STATE_RECORDING: 1.5 с тишины → stopAndSave() | ✅ Корректно (с замечанием) | Низкая |
| 4 | WAV заголовок (RIFF, fmt, 16kHz, Mono, 16-bit) | ✅ Полностью корректно | — |
| 5 | WiFi reconnect только в STATE_IDLE | ⚠️ Ограничение | Средняя |
| 6 | getPeakLevel() крадёт сэмплы из rx_handle | ⚠️ Потенциальная потеря данных | Средняя |
| 7 | getLatestFrame() не используется (скользящее окно) | ⚠️ Мёртвый код | Низкая |
| 8 | PA_CTRL: LOW→HIGH→LOW правильно | ✅ Корректно | — |
| 9 | MCLK активен до initES8311() | ✅ Корректно | — |
| 10 | rec_counter сбрасывается после reboot | ⚠️ Риск перезаписи файлов | Средняя |
| 11 | snprintf: %lu для uint32_t | ⚠️ Незначительно | Низкая |
| 12 | delay(5) в loop() может задержать VAD | ⚠️ Замечание | Низкая |

---

## Рекомендуемые исправления (приоритет)

### P1 — Средний приоритет

**WiFi reconnect вне STATE_IDLE:**
```cpp
// main.cpp: убрать условие currentState == STATE_IDLE
if (now - lastWifiCheckMs > 15000) {  // без проверки стейта
```

**getPeakLevel() — не вызывать при STATE_RECORDING:**
```cpp
// Добавить проверку в вызывающем коде:
if (currentState != STATE_RECORDING) {
    float level = recorder.getPeakLevel();
}
```

**rec_counter — инициализация из SD:**
```cpp
// begin(): сканировать /rec_0xx.wav и восстановить счётчик
```

### P2 — Низкий приоритет

**Заменить delay(5) на yield():**
```cpp
// main.cpp, строка 625
yield(); // вместо delay(5)
```

**Удалить или задокументировать getLatestFrame():**
```cpp
// Recorder.h, строки 78-81: пометить как [[deprecated]] или удалить
```

**Исправить формат snprintf:**
```cpp
#include <inttypes.h>
snprintf(path, sizeof(path), "/rec_%03" PRIu32 ".wav", rec_counter);
```

---

*Отчёт подготовлен: Arduino Framework & Audio Pipeline Engineer*
