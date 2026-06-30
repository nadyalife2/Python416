# Отчёт: Audio Hardware & ES8311 — Melvin (SpotPear ESP32-S3 V2.0)

> **Дата:** 2026-06-05  
> **Аналитик:** ESP-IDF Audio Hardware & ES8311 Expert  
> **Файлы:** `src/main.cpp` (627 строк), `include/Recorder.h` (194 строки)

---

## Executive Summary

Код в целом построен корректно и демонстрирует хорошее понимание аппаратных требований платы SpotPear ESP32-S3 V2.0. Основные решения — continuous full-duplex I2S, правильная последовательность MCLK→initES8311, chunked WAV-запись в PSRAM — реализованы верно. Тем не менее выявлено **3 проблемы уровня HIGH**, **4 проблемы уровня MEDIUM** и **3 наблюдения уровня LOW**, способных приводить к артефактам звука, потере данных или нестабильной работе.

---

## 1. Проверка инициализации I2S и ES8311

### 1.1 Порядок инициализации в `setup()`

```
i2s_duplex_init()   → MCLK GPIO16 стартует  ✅
delay(200)          → ожидание стабилизации  ✅
Wire.begin()
initES8311()        → I2C инит ПОСЛЕ MCLK    ✅
```

**Вывод:** Порядок правильный. MCLK гарантированно активен до первого I2C-обращения к ES8311.

### 1.2 I2S duplex конфигурация

| Параметр | TX | RX | Ожидаемое |
|---|---|---|---|
| Sample Rate | 16000 | 16000 | 16000 ✅ |
| Bit Width | 16-bit | 16-bit | 16-bit ✅ |
| Slot Mode | STEREO | MONO | Stereo TX / Mono RX ✅ |
| MCLK Multiple | 256 | 256 | 256 ✅ |
| MCLK Pin | GPIO16 | GPIO16 | GPIO16 ✅ |
| BCLK Pin | GPIO9 | GPIO9 | GPIO9 ✅ |
| WS Pin | GPIO45 | GPIO45 | GPIO45 ✅ |
| DOUT | GPIO8 | UNUSED | ✅ |
| DIN | UNUSED | GPIO10 | ✅ |

**Вывод:** Конфигурация пинов и параметров I2S соответствует схемотехнике SpotPear V2.0.

---

## 2. Проверка регистров ES8311

### 2.1 Таблица регистров

| Регистр | Значение | Назначение | Статус |
|---|---|---|---|
| `0x00` | `0x1F` → `0x00` → `0x80` | Reset → Normal → Master Start | ✅ |
| `0x01` | `0x3F` | MCLK divider | ✅ Соответствует требованию |
| `0x08` | `0xFF` | LRCK divider | ⚠️ ПРОБЛЕМА — см. HIGH-001 |
| `0x09` | `0x0C` | DAC I2S format — 16-bit Philips | ✅ |
| `0x0A` | `0x0C` | ADC I2S format — 16-bit Philips | ✅ |
| `0x17` | `0xCF` | ADC Digital Volume (+8dB, unmute) | ✅ |
| `0x32` | `0xBF` | DAC Digital Volume MAX | ✅ Соответствует требованию |
| `0x37` | `0x08` | Route DAC → output | ✅ Соответствует требованию |
| `0x45` | `0x22` | Driver Gain | ✅ Соответствует требованию |
| `0x12` | `0x00` | Unmute DAC | ✅ |

### 2.2 Детальный анализ Reg 0x08

**Значение `0xFF` (255 decimal).**  
В ES8311 регистр `0x08` задаёт LRCK (WS) делитель: `LRCK_DIV = 0x08[7:0] + 1 = 256`.  
При MCLK = 16000 × 256 = 4 096 000 Гц → LRCK = MCLK / 256 = 16 000 Гц. Математически корректно.  
Однако в ES8311 биты 7:0 регистра `0x08` содержат **LRCK_H[7:0]** (старший байт 11-битного делителя), а **LRCK_L[7:0]** — в регистре `0x07`.  
Итоговый делитель = `{0x08[2:0], 0x07[7:0]}` = `{0x07[2:0], 0xFF}` при `0x07=0x00` → `{0b000, 0xFF}` = **255**.  
LRCK = MCLK / 256 = 16 000 Гц — **совпадает**. Значение верное.

---

## 3. Проверка PA_CTRL (GPIO46)

### Схема управления в коде:

```
setup():
    pinMode(PA_CTRL_PIN, OUTPUT)
    digitalWrite(PA_CTRL_PIN, LOW)     ← LOW при boot ✅

playWavFromSD():
    [1] switchToTX()
    [2] file.read → rawBuf
    [3] i2s_channel_write(...)          ← первый write ДО HIGH ✅
    [4] digitalWrite(PA_CTRL_PIN, HIGH) ← HIGH после начала передачи ✅
    [5] ... playback loop ...
    [6] flush silence buf
    [7] digitalWrite(PA_CTRL_PIN, LOW)  ← LOW после завершения ✅
```

**Вывод:** Логика PA_CTRL соответствует требованию: LOW при boot, HIGH только во время воспроизведения, LOW после. ✅

> [!NOTE]
> GPIO46 — strapping pin. На плате SpotPear V2.0 он подтянут к GND через NS4150B.  
> `pinMode(OUTPUT)` + `digitalWrite(LOW)` в `setup()` корректны и не конфликтуют с boot ROM.

---

## 4. Найденные проблемы

---

### 🔴 HIGH-001: Гонка данных в `getPeakLevel()` — двойной I2S read

**Файл:** `Recorder.h`, строки 83–97  
**Описание:**  
`getPeakLevel()` вызывает `i2s_channel_read()` независимо от `process()`. Если обе функции вызываются в одном цикле loop, один и тот же I2S RX DMA-буфер будет прочитан **дважды**, часть данных будет потеряна для записи. В режиме непрерывной записи это приведёт к пропускам в PCM-данных.

**Текущий код:**
```cpp
// Recorder.h:88
if (i2s_channel_read(rx_handle, dma_buf, sizeof(dma_buf),
                     &br, pdMS_TO_TICKS(5)) == ESP_OK && br > 0) {
```

**Исправление:**
```cpp
float getPeakLevel() {
    // Вычисляем peak по уже записанным данным в phrase_buf,
    // а НЕ делаем дополнительный read из I2S
    if (!phrase_buf || phrase_frames == 0) return 0.0f;
    int start = (phrase_frames > 128) ? phrase_frames - 128 : 0;
    float max_val = 0;
    for (int i = start; i < phrase_frames; i++) {
        float val = fabsf((float)phrase_buf[i]);
        if (val > max_val) max_val = val;
    }
    return max_val / 32768.0f;
}
```

---

### 🔴 HIGH-002: Stack overflow риск — `uint8_t rawBuf[bufSize]` на стеке задачи

**Файл:** `main.cpp`, строка 255  
**Описание:**  
`bufSize = 1024`, `rawBuf[1024]` выделяется на **стеке** внутри `playWavFromSD()`. Стек задачи Arduino на ESP-IDF по умолчанию — 8192 байт. С учётом вложенных вызовов (File, SD_MMC, I2S) буфер в 1 КБ на стеке создаёт высокий риск stack overflow, особенно если вызов идёт из глубокого контекста.

**Текущий код:**
```cpp
const size_t bufSize = 1024;
uint8_t rawBuf[bufSize];  // ← стек!
```

**Исправление:**
```cpp
const size_t bufSize = 1024;
// Выделяем rawBuf в PSRAM вместе с wavBuf
uint8_t* rawBuf = (uint8_t*)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
if (!rawBuf) rawBuf = (uint8_t*)malloc(bufSize);
if (!rawBuf) { free(wavBuf); file.close(); return false; }

// ... (в конце функции)
free(rawBuf);
free(wavBuf);
```

---

### 🔴 HIGH-003: Потенциальный PA_CTRL HIGH при пустом файле

**Файл:** `main.cpp`, строки 258–273  
**Описание:**  
Если `firstRead > 0` но `i2s_channel_write` возвращает ошибку (таймаут, закрытый канал), `PA_CTRL` всё равно переходит в `HIGH` (строка 271). При последующем закрытии файла (строка 304) `PA_CTRL` будет снят только в строке 316, но DMA уже мог не запуститься — усилитель будет включён без сигнала, что даёт характерный щелчок/шум.

**Текущий код:**
```cpp
i2s_channel_write(tx_handle, wavBuf, samples * 4, &written, portMAX_DELAY);

// Turn ON the amplifier now that I2S transmission has started
digitalWrite(PA_CTRL_PIN, HIGH);
```

**Исправление:**
```cpp
esp_err_t err = i2s_channel_write(tx_handle, wavBuf, samples * 4, &written, portMAX_DELAY);
if (err == ESP_OK && written > 0) {
    digitalWrite(PA_CTRL_PIN, HIGH);
    delay(10);
} else {
    Serial.printf("[WAV] First write failed: %d, written=%d\n", err, written);
    // PA остаётся LOW — нет щелчка
}
```

---

### 🟡 MEDIUM-001: RX I2S channel использует `MONO`, но ES8311 передаёт стерео-фрейм

**Файл:** `main.cpp`, строка 162  
**Описание:**  
ES8311 как I2S Slave передаёт данные АЦП в обоих слотах (L+R) стерео-фрейма, даже если микрофон физически один (MEMS моно). При `I2S_SLOT_MODE_MONO` ESP-IDF I2S RX driver захватывает **только левый слот**. Это означает, что если ES8311 помещает ADC-данные в правый слот (зависит от настройки регистра `0x0A`), запись окажется тишиной.

**Рекомендация:**  
Проверить регистр `0x0A` (ADC format). Бит `[3]` = `ADC_LEFT_MUTE` / `ADC_RIGHT_MUTE` определяет, какой слот активен. Для надёжности переключить RX на `STEREO` и брать только левый канал в `process()`:

```cpp
// i2s_duplex_init(): изменить RX на STEREO
.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
    I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),

// Recorder.h process(): брать только чётные сэмплы (левый канал)
int n = br / 2; // теперь n = stereo пар
for (int i = 0; i < n && phrase_frames < REC_PHRASE_MAX; i += 2) {
    phrase_buf[phrase_frames++] = dma_buf[i]; // только L
}
```

---

### 🟡 MEDIUM-002: `silenceBuf` на куче (calloc) при flush — не PSRAM

**Файл:** `main.cpp`, строки 307–314  
**Описание:**  
`calloc(bufSize * 2, sizeof(int16_t))` = `calloc(2048, 2)` = 4096 байт — выделяется из внутренней heap. При фрагментированной памяти после большой сессии это может вернуть `nullptr`. Код это обрабатывает через `delay(120)`, но именно тогда, когда DMA flush критичен, можно получить щелчок.

**Исправление:**
```cpp
// Вместо calloc — статический нулевой буфер или выделение в PSRAM:
static const int16_t silence_buf[1024] = {0}; // ROM-секция, 0 байт SRAM
size_t written = 0;
// flush несколькими маленькими блоками чтобы не залочить DMA надолго
for (int k = 0; k < 2; k++) {
    i2s_channel_write(tx_handle, silence_buf, sizeof(silence_buf), &written, pdMS_TO_TICKS(50));
}
```

---

### 🟡 MEDIUM-003: `delay(5)` в `loop()` — лишняя задержка при VAD

**Файл:** `main.cpp`, строка 625  
**Описание:**  
`delay(5)` в конце `loop()` означает, что I2S RX буфер не опрашивается 5 мс каждый цикл. При 16 кГц / 256 фреймов/DMA-буфер = 16 мс заполнение буфера. С 5 мс задержкой + временем выполнения loop ~2–3 мс существует риск переполнения DMA FIFO и потери сэмплов.

**Рекомендация:**  
Убрать `delay(5)`. Если требуется отдача CPU другим задачам, заменить на `vTaskDelay(1)` (1 тик = 1 мс в FreeRTOS):
```cpp
// Строка 625:
// delay(5);       ← убрать
taskYIELD();       // позволить другим задачам выполниться без фиксированного delay
```

---

### 🟡 MEDIUM-004: `rec_counter` сбрасывается после 10 — перезапись активного файла

**Файл:** `Recorder.h`, строки 116–118  
**Описание:**  
`rec_counter = (rec_counter + 1) % 10` цикличен. Если `Agent.h` медленно читает `/rec_009.wav` через SD, а следующая запись начнётся в тот же файл — данные будут повреждены. Счётчик не синхронизирован с завершением чтения агентом.

**Рекомендация:**  
Не делать циклический сброс, либо проверять занятость файла:
```cpp
// Вариант 1: монотонный счётчик (1000 сессий до переполнения uint32_t — практически никогда)
rec_counter++;
snprintf(path, sizeof(path), "/rec_%04lu.wav", (unsigned long)(rec_counter % 10000));

// Вариант 2: ввести флаг agent_reading и блокировать stopAndSave если файл занят
```

---

### 🔵 LOW-001: `es_write()` не проверяет возврат при init

**Файл:** `main.cpp`, строки 80–131  
**Описание:**  
`es_write()` возвращает `bool`, но все вызовы в `initES8311()` игнорируют возвращаемое значение. При обрыве I2C (например, SDA не подтянут) инициализация пройдёт без ошибок в логе, но кодек останется в undefined state.

**Рекомендация:**  
Минимально — проверить критические регистры:
```cpp
if (!es_write(0x00, 0x1F)) {
    Serial.println("[CODEC] FATAL: ES8311 I2C communication failed!");
    // setState(STATE_ERROR); или halt
    return;
}
```

---

### 🔵 LOW-002: `vad_buf` объявлен как `static` внутри `if (currentState == STATE_IDLE)`

**Файл:** `main.cpp`, строки 555–556  
**Описание:**  
`static int16_t vad_buf[480]` и `static int vad_buf_idx = 0` — переменные не сбрасываются при выходе из STATE_IDLE и возврате. Если запись завершилась, и loop снова вошёл в IDLE, `vad_buf_idx` может содержать стale-значение, что даст ложный VAD trigger с данными из предыдущей сессии.

**Исправление:**
```cpp
// В setState() при переходе в STATE_IDLE добавить:
// (или при старте VAD-блока явно сбрасывать):
// vad_buf_idx = 0; // сброс при входе в IDLE
```

---

### 🔵 LOW-003: `getLatestFrame()` читает не свежие, а старые данные

**Файл:** `Recorder.h`, строки 78–81  
**Описание:**  
`getLatestFrame()` возвращает `&phrase_buf[phrase_frames - 480]` — последние 480 сэмплов из буфера записи. Но в основном цикле VAD использует `phrase_buf[vad_processed_frames]`, а `getLatestFrame()` нигде не вызывается. Функция является мёртвым кодом. При случайном использовании даст сдвиг данных.

**Рекомендация:**  
Удалить `getLatestFrame()` или задокументировать её назначение, если она планируется к использованию в будущем.

---

## 5. Таблица compliance с правилами AGENTS.md

| Правило | Статус | Детали |
|---|---|---|
| НЕ использовать `Audio.h` / ESP32-audioI2S | ✅ PASS | Используется нативный `driver/i2s_std.h` |
| НЕ вызывать `delay()` внутри I2S callback или ISR | ✅ PASS | delay() только в setup() и main loop |
| НЕ аллоцировать память в ISR | ✅ PASS | malloc/heap_caps_malloc только вне ISR |
| Крупные буферы (>10KB) — ТОЛЬКО в PSRAM | ✅ PASS | `wavBuf` (2KB via PSRAM), `phrase_buf` (320KB via PSRAM) |
| `uint8_t rawBuf[1024]` на стеке | ⚠️ PARTIAL | rawBuf[1024] на стеке — нарушение HIGH-002 |
| MCLK (GPIO16) активен ДО initES8311() | ✅ PASS | i2s_duplex_init() → delay(200) → initES8311() |
| PA_CTRL: LOW при boot | ✅ PASS | setup() строка 449 |
| PA_CTRL: HIGH перед i2s_channel_write | ✅ PASS | Первый write, затем HIGH (строка 271) |
| PA_CTRL: LOW после воспроизведения | ✅ PASS | Строка 316 |
| WAV PCM 16000 Гц 16-bit Mono | ✅ PASS | REC_SAMPLE_RATE=16000, REC_BIT_DEPTH=16, REC_CHANNELS=1 |
| SD буферизованная запись (chunked) | ✅ PASS | WRITE_CHUNK=4096, цикл в stopAndSave() |
| Reg 0x01 = 0x3F | ✅ PASS | Строка 94 |
| Reg 0x32 = 0xBF | ✅ PASS | Строка 123 |
| Reg 0x37 = 0x08 | ✅ PASS | Строка 124 |
| Reg 0x45 = 0x22 | ✅ PASS | Строка 125 |
| Reg 0x08 = 0xFF | ✅ PASS | Строка 101 — LRCK divider корректен |
| Continuous MCLK (full-duplex) | ✅ PASS | TX+RX enable в i2s_duplex_init(), switchToTX() = no-op |

---

## 6. Приоритетный план исправлений

| Приоритет | Проблема | Файл | Строки | Усилие |
|---|---|---|---|---|
| 🔴 1 | HIGH-001: getPeakLevel() двойной read | Recorder.h | 83–97 | 15 мин |
| 🔴 2 | HIGH-002: rawBuf на стеке | main.cpp | 255 | 10 мин |
| 🔴 3 | HIGH-003: PA HIGH без проверки write | main.cpp | 268–272 | 5 мин |
| 🟡 4 | MEDIUM-001: RX MONO vs STEREO | main.cpp + Recorder.h | 162, 60–65 | 30 мин |
| 🟡 5 | MEDIUM-003: delay(5) в loop | main.cpp | 625 | 2 мин |
| 🟡 6 | MEDIUM-002: calloc silence flush | main.cpp | 307–314 | 10 мин |
| 🟡 7 | MEDIUM-004: rec_counter циклический | Recorder.h | 116 | 10 мин |
| 🔵 8 | LOW-001: es_write без проверки | main.cpp | 90 | 15 мин |
| 🔵 9 | LOW-002: static vad_buf не сбрасывается | main.cpp | 555 | 5 мин |
| 🔵 10 | LOW-003: getLatestFrame() мёртвый код | Recorder.h | 78–81 | 2 мин |

---

*Отчёт подготовлен советником ESP-IDF Audio Hardware & ES8311 Expert, Совет разработчиков Мелвин.*
