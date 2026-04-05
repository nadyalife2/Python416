# MELVIN v3.7.4 — Полный Даташит
## Робот на базе ESP32-S3 (Spotpear MUMA 1.54 / ESP32-1.54inch-AI-V2)
### Составлен на основе: схемы платы, бинарника прошивки, исходного кода main.cpp

---

## 1. АППАРАТНАЯ ЧАСТЬ — ПИНЫ

### 1.1 Все задействованные GPIO

| GPIO | Имя в коде     | Куда подключён             | Назначение                                    |
|------|----------------|----------------------------|-----------------------------------------------|
| 0    | BOOT_BTN       | Кнопка K2 на плате         | Кнопка управления (short/double/long press)   |
| 8    | I2S_DOUT       | ES8311 DSDIN (pin 9)       | I2S данные: ESP32 → кодек (воспроизведение)   |
| 9    | I2S_BCLK       | ES8311 SCLK (pin 6)        | I2S бит-клок для обоих каналов TX и RX        |
| 10   | I2S_DIN        | ES8311 ASDOUT (pin 7)      | I2S данные: кодек → ESP32 (запись с микрофона)|
| 14   | I2C_SCL        | ES8311 CCLK (pin 1)        | I2C clock — настройка регистров кодека        |
| 15   | I2C_SDA        | ES8311 CDATA (pin 19)      | I2C data — настройка регистров кодека         |
| 16   | I2S_MCLK       | ES8311 MCLK (pin 2)        | Мастер-клок аудиокодека (обязателен!)         |
| 17   | SDMMC_CLK_PIN  | SD-карта CLK               | Тактирование SD/MMC                           |
| 18   | SDMMC_CMD_PIN  | SD-карта CMD               | Команды SD/MMC                                |
| 21   | SDMMC_D0_PIN   | SD-карта D0                | Данные SD/MMC                                 |
| 45   | I2S_LRC        | ES8311 LRCK (pin 8)        | I2S word-select (left/right channel clock)    |
| 46   | PA_ENABLE      | NS4150B CTRL               | Включение усилителя динамика (HIGH = вкл)     |

### 1.2 Дополнительные пины (дисплей, из схемы)

| GPIO | Сигнал        | Куда идёт                  | Назначение                                    |
|------|---------------|----------------------------|-----------------------------------------------|
| 3    | Bat-t         | Делитель R14/R17           | АЦП мониторинга заряда батареи (VBAT_ADC)     |
| 48   | GP48          | LED WS2812B                | RGB светодиод (статус)                        |
| 33   | LCD_CS        | TFT1 (LCD-1.54inch) CS     | Chip Select дисплея                           |
| 34   | LCD_SCL       | TFT1 SCL                   | SPI clock дисплея                             |
| 35   | LCD_SDA       | TFT1 SDA                   | SPI data дисплея                              |
| 36   | LCD_DC        | TFT1 DC                    | Data/Command дисплея                          |
| 37   | LCD_RST       | TFT1 RST                   | Reset дисплея                                 |
| 38   | LCD_BL        | LCD подсветка через Q4     | Управление яркостью подсветки                 |
| 39   | TP_SCL        | TFT1 TP_SCL                | I2C clock тачскрина                           |
| 40   | TP_SDA        | TFT1 TP_SDA                | I2C data тачскрина                            |
| 41   | TP_INT        | TFT1 TP_INT                | Прерывание тачскрина                          |
| 42   | TP_RST        | TFT1 TP_RST / LCD_RST      | Reset тачскрина и дисплея                     |

---

## 2. СХЕМА АУДИОТРАКТА

### 2.1 Тракт воспроизведения (ESP32 → Динамик)

```
ESP32-S3
  GP16 (MCLK) ─────────────► ES8311 pin2 (MCLK)
  GP9  (BCLK) ─────────────► ES8311 pin6 (SCLK/BCLK)
  GP45 (LRCK) ─────────────► ES8311 pin8 (LRCK)
  GP8  (DOUT) ─────────────► ES8311 pin9 (DSDIN)   ← ДАННЫЕ АУДИО
  GP14 (SCL)  ─────────────► ES8311 pin1 (CCLK)    ← I2C настройка
  GP15 (SDA)  ─────────────► ES8311 pin19 (CDATA)  ← I2C данные

ES8311 (адрес I2C = 0x18)
  pin12 (OUTP) ────────────► NS4150B IN+
  pin13 (OUTN) ────────────► NS4150B IN-

NS4150B (усилитель класса D)
  CTRL pin  ◄──────────────── GP46 (PA_ENABLE)   ← HIGH = включён
  OUT+ ─────────────────────► Динамик (+)
  OUT- ─────────────────────► Динамик (-)
```

### 2.2 Тракт записи (Микрофон → ESP32)

```
На плате два микрофона AOS3729A (MEMS аналоговые):
  MIC1 — основной, подключён к ES8311 MIC1P/MIC1N (pins 18/17)
  MIC2 — дополнительный, подключён к ES8311 MIC2P/MIC2N (pins 16/15)

ES8311 усиливает аналоговый сигнал и оцифровывает в PCM:

  MIC1P ──────────────────► ES8311 pin18 (MICIP)   ← дифференциальный вход
  MIC1N ──────────────────► ES8311 pin17 (MICIN)

  ES8311: PGA → HPF → ADC → I2S
    Усиление PGA: регулируется регистром 0x14
    HPF: включён (регистр 0x1C = 0x6A)
    Формат вывода: 16-bit Philips I2S, стерео (данные в LEFT-канале)

  ES8311 pin7 (ASDOUT) ───► GP10 (I2S_DIN)   ← цифровые PCM данные
  ES8311 pin6 (SCLK)   ◄─── GP9  (BCLK)
  ES8311 pin8 (LRCK)   ◄─── GP45 (LRCK)
  ES8311 pin2 (MCLK)   ◄─── GP16 (MCLK)

ESP32-S3 читает стерео-фрейм (32 бита), берёт левый канал (16 бит) → моно PCM 16kHz
```

**⚠️ Важно — выбор канала:** ES8311 посылает данные в левом (L) канале фрейма.
В коде I2S читаем стерео (`I2S_SLOT_MODE_STEREO`), затем берём каждый чётный сэмпл:
```cpp
// dma_buf — стерео 16-bit: [L, R, L, R, ...]
for (int i = 0; i < n; i++) pcm[i] = dma_buf[i * 2];  // только L
```
Если голос не слышен — попробовать `dma_buf[i * 2 + 1]` (правый канал).

---

## 3. I2S КОНФИГУРАЦИЯ

### 3.1 Режим записи (RX) — новый драйвер ESP-IDF (i2s_std)

Используется **новый API ESP-IDF v5** (`driver/i2s_std.h`), не устаревший `driver/i2s.h`:

```cpp
// Пины
#define I2S_MCLK_PIN  GPIO_NUM_16
#define I2S_BCLK_PIN  GPIO_NUM_9
#define I2S_WS_PIN    GPIO_NUM_45
#define I2S_DIN_PIN   GPIO_NUM_10   // ← данные с кодека
#define I2S_DOUT_PIN  GPIO_NUM_8    // ← данные к кодеку (можно I2S_GPIO_UNUSED при RX-only)

// Канал — только RX
i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
chan_cfg.auto_clear = true;
i2s_new_channel(&chan_cfg, NULL, &rx_handle);  // NULL = без TX

// Клок
i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(16000);
clk.mclk_multiple = I2S_MCLK_MULTIPLE_256;   // MCLK = 16000 × 256 = 4.096 МГц

// Слот — Philips I2S, стерео, читаем LEFT
i2s_std_slot_config_t slot =
    I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
slot.slot_mask = I2S_STD_SLOT_LEFT;   // ← только левый канал активен

// GPIO
i2s_std_gpio_config_t pins = {};
pins.mclk = I2S_MCLK_PIN;
pins.bclk = I2S_BCLK_PIN;
pins.ws   = I2S_WS_PIN;
pins.dout = I2S_DOUT_PIN;
pins.din  = I2S_DIN_PIN;

// Сборка и запуск
i2s_std_config_t cfg = { clk, slot, pins };
i2s_channel_init_std_mode(rx_handle, &cfg);
i2s_channel_enable(rx_handle);
```

**Параметры итогового потока:**
| Параметр        | Значение                     |
|-----------------|------------------------------|
| Sample rate     | 16 000 Гц                    |
| Bit depth       | 16 бит                       |
| Каналов в DMA   | 2 (стерео), монозвук в LEFT  |
| MCLK            | 4.096 МГц (× 256)            |
| Формат          | Philips I2S (стандарт)       |
| DMA буфер       | 4096 байт (2048 int16 сэмпл) |

### 3.2 Чтение и деинтерлив каналов

```cpp
static int16_t dma_buf[2048];  // стерео: [L0, R0, L1, R1, ...]
static int16_t pcm[1024];      // моно: только L-канал

size_t br = 0;
i2s_channel_read(rx_handle, dma_buf, sizeof(dma_buf), &br, pdMS_TO_TICKS(200));

int n = (br / 2) / 2;                            // кол-во моно-сэмплов
for (int i = 0; i < n; i++) pcm[i] = dma_buf[i * 2];  // LEFT = чётные
```

### 3.3 Режим воспроизведения (TX) — через библиотеку Audio.h

```cpp
audio.setPinout(I2S_BCLK=9, I2S_LRC=45, I2S_DOUT=8, I2S_MCLK=16)
audio.setVolume(15)   // 0-21
```

### 3.4 Переключение RX → TX (критический момент!)

Драйвер I2S один — нужно переустанавливать при смене режима:
  1. audio.stopSong()
  2. i2s_install_rx()          — режим микрофона
  3. i2s_read(...)             — чтение PCM
  4. i2s_driver_uninstall()    — освобождение драйвера
  5. delay(50-100ms)           — ОБЯЗАТЕЛЬНО! иначе краш
  6. i2s_restore_tx()          — восстановление режима динамика
  7. digitalWrite(PA_ENABLE, HIGH) — включение усилителя

---

## 4. ES8311 — НАСТРОЙКА ЧЕРЕЗ I2C

Адрес I2C: **0x18**
Шина: Wire.begin(SDA=15, SCL=14), скорость 100 кГц

### 4.1 Рабочая конфигурация регистров (v15, проверена)

| Регистр | Значение | Назначение                                              |
|---------|----------|---------------------------------------------------------|
| 0x00    | 0x1F→0x80| Сброс кодека (0x1F), затем включение (0x80 = CSM_ON)    |
| 0x01    | 0x3F     | Power management — все блоки включены                   |
| 0x02    | 0x00     | Clocking control                                        |
| 0x03    | 0x10     | Clock divider                                           |
| 0x04    | 0x10     | Clock divider 2                                         |
| 0x05    | 0x00     | Clock divider 3                                         |
| 0x06    | 0x04     | Clock divider 4 (MCLK div)                              |
| 0x07    | 0x00     | Clock divider 5                                         |
| 0x08    | 0x40     | LRCK делитель Low — **0x40 = 64 такта/фрейм** (16-bit стерео) |
| 0x09    | 0x00     | LRCK делитель High                                      |
| 0x0A    | 0x00     | I2S control — нет mute                                  |
| 0x0D    | 0x01     | DAC timing                                              |
| 0x0E    | 0x02     | ADC timing — PGA power on                               |
| 0x0F    | 0x7F     | System control                                          |
| 0x10    | 0x00     | System control 2                                        |
| 0x11    | 0x7C     | Bias / power                                            |
| 0x12    | 0x00     | DAC control                                             |
| 0x13    | 0x10     | ADC control                                             |
| **0x14**| **0x1F** | **Маршрутизация и усиление МИК.** LINSEL=0 (MIC1P/N), PGA=+42dB |
| 0x15    | 0x40     | MIC boost                                               |
| 0x16    | 0x24     | ADC control                                             |
| **0x17**| **0xBF** | **ADC Digital Volume: 0xBF = 0 dB** (фиксированный)    |
| 0x18    | 0x00     | ALC control — **ALC ВЫКЛЮЧЕН** (критично для STT!)      |
| 0x19    | 0x00     | ALC control 2                                           |
| 0x1A    | 0x00     | ALC control 3                                           |
| 0x1B    | 0x00     | ALC control 4                                           |
| **0x1C**| **0x6A** | **HPF включён** — убирает постоянную составляющую (DC)  |
| 0x31    | 0x60     | ADC channel control                                     |
| 0x32    | 0xBF     | ADC digital volume 2                                    |
| 0x37    | 0x48     | ADC control                                             |
| 0x45    | 0x00     | GPIO/misc control                                       |

### 4.2 Регистр 0x14 — выбор входа микрофона и PGA

Биты регистра `0x14` (MIC Routing & PGA):

| Биты  | Назначение        | 0x1F (рабочее) | 0x3F (альтернатива) |
|-------|-------------------|----------------|---------------------|
| [7:6] | LINSEL — вход     | 00 = MIC1P/N   | 00 = MIC1P/N        |
| [5]   | —                 | 0              | 1 (MIC2 route)      |
| [4:0] | PGA gain          | 11111 = +42 dB | 11111 = +42 dB      |

**Схема PGA (шаг 3 дБ):**
```
0x00 =  0 dB  |  0x07 = +21 dB  |  0x0F = +33 dB
0x17 = +39 dB |  0x1F = +42 dB  |  (максимум)
```

> ⚠️ Если голос не слышен при `0x1F` (MIC1) — попробовать `0x3F` (MIC2).
> На плате ESP32-1.54inch-AI-V2 активен MIC1 (подключён к MICIP/MICIN).

### 4.3 ALC (Automatic Level Control) — **ВЫКЛЮЧИТЬ ДЛЯ STT!**

ALC включается регистрами 0x18–0x1B. При включённом ALC:
- уровень постоянно меняется → VAD работает нестабильно
- Whisper плохо распознаёт плавающий уровень

Всегда держать `0x18 = 0x00`, `0x19 = 0x00`, `0x1A = 0x00`, `0x1B = 0x00`.

### 4.4 Verify — контрольные значения после init

```
0x00 = 0x80  (CSM_ON)          ← кодек активен
0x08 = 0x40  (LRCK_L)          ← 64 такта на фрейм
0x0A = 0x00  (no mute)         ← нет mute
0x0E = 0x02  (PGA pwr)         ← PGA включён
0x14 = 0x1F  (MIC1 routing)    ← MIC1 + PGA +42dB
0x17 = 0xBF  (ADC vol 0dB)     ← цифровой уровень максимум
0x18 = 0x00  (ALC off)         ← ALC выключен
```

---

## 5. VAD — ДЕТЕКТОР ГОЛОСОВОЙ АКТИВНОСТИ

### 5.1 Алгоритм (Peak dBFS)

```cpp
// Вычисление пикового уровня в dBFS
int32_t pk = 0;
for (int i = 0; i < n; i++) {
    int32_t a = abs((int32_t)pcm[i]);
    if (a > pk) pk = a;
}
float pkdb = pk > 0 ? 20.f * log10f(pk / 32767.f) : -96.f;

// VAD решение (гистерезис)
bool loud = (pkdb > (in_speech ? VAD_SILENCE_DB : VAD_SPEECH_DB));
```

### 5.2 Рабочие пороги (v15)

| Параметр          | Значение | Назначение                                  |
|-------------------|----------|---------------------------------------------|
| VAD_SPEECH_DB     | −30 dBFS | Выше = начало речи                          |
| VAD_SILENCE_DB    | −37 dBFS | Ниже = тишина (гистерезис между порогами)   |
| VAD_HANGOVER_MS   | 800 мс   | Тишины подряд → конец фразы                 |
| PHRASE_BUF_SEC    | 8 сек    | Максимальная длина захватываемой фразы      |

> **История настройки порогов:**
> - v10: −38/−48 — слишком низко, VAD не срабатывал (фон −37..−42 dBFS)
> - v15: −30/−37 — текущие рабочие значения
> - При шумном помещении рекомендуется −25/−32

### 5.3 Типичные уровни на плате

| Ситуация              | Peak dBFS   | Комментарий                           |
|-----------------------|-------------|---------------------------------------|
| Полная тишина         | −80 .. −90  | Цифровые нули (z>50%)                 |
| Фон помещения         | −40 .. −53  | Кондиционер, ПК и т.п.                |
| Речь в 30 см          | −30 .. −20  | Нормальный разговор                   |
| Речь вплотную (2 см)  | −15 .. −5   | Громкая речь рядом                    |
| Клиппинг              | > −3 dBFS   | Снизить PGA или отдалиться от микрофона|

### 5.4 Диагностика по Serial

```
[silence] pk= -80.8 rms= -91.3 z=96%  !! >50% zeros   ← нет сигнала, нет MCLK?
[silence] pk= -42.1 rms= -56.7  тишина                ← фон помещения (норма)
[silence] pk= -37.6 rms= -56.3                         ← граница порога
[SPEECH ] pk= -18.2 rms= -32.5  Хороший сигнал         ← речь засечена
[SPEECH ] pk=  -4.1 rms= -12.0  !! КЛИПИНГ             ← слишком громко / близко
```

---

## 6. API И СЕТЕВЫЕ ПОДКЛЮЧЕНИЯ

### 6.1 Архитектура взаимодействия

```
ESP32-S3
    │
    ├─► [STT] Запись 5 сек → WAV → POST HTTPS
    │         └─► HuggingFace Whisper → текст
    │
    ├─► [LLM] Текст → POST HTTPS + JSON
    │         └─► OpenRouter LLaMA-3.1 → ответ
    │
    └─► [TTS] Текст → POST HTTPS + JSON
              └─► HuggingFace MMS-TTS → WAV
                      │
                      └─► SD-карта /tts_out.wav
                              │
                              └─► audio.connecttoFS() → I2S → ES8311 → динамик
```

### 6.2 API 1: STT — распознавание речи

```
URL:        https://router.huggingface.co/models/openai/whisper-large-v3-turbo
Метод:      POST
Заголовки:  Authorization: Bearer {HF_API_KEY}
            Content-Type: audio/wav
Тело:       WAV-файл (44 байт заголовок + PCM 16bit 16kHz mono, 5 сек = 160000 байт)
Ответ:      JSON {"text": "распознанная речь"}
Таймаут:    30 сек
```

Формат WAV-заголовка:
```
RIFF / WAVE / fmt: PCM=1, channels=1, sampleRate=16000,
byteRate=32000, blockAlign=2, bitsPerSample=16
data chunk: 160000 байт (5 сек × 16000 Гц × 2 байта)
```

### 6.3 API 2: LLM — языковая модель

```
URL:        https://openrouter.ai/api/v1/chat/completions
Метод:      POST
Заголовки:  Authorization: Bearer {OR_API_KEY}
            Content-Type: application/json
Тело:       {
              "model": "meta-llama/llama-3.1-8b-instruct:free",
              "messages": [
                {"role": "system", "content": "Ты — Рик Санчес C-137..."},
                {"role": "user",   "content": "текст пользователя"}
              ]
            }
Ответ:      {"choices":[{"message":{"content":"ответ LLM"}}]}
Таймаут:    30 сек
```

### 6.4 API 3: TTS — синтез речи

```
URL:        https://router.huggingface.co/models/facebook/mms-tts-rus
Метод:      POST
Заголовки:  Authorization: Bearer {HF_API_KEY}
            Content-Type: application/json
Тело:       {"inputs": "текст для озвучки"}
Ответ:      WAV-файл (бинарный поток)
Таймаут:    25 сек

Сохранение:      SD-карта → /tts_out.wav
Воспроизведение: audio.connecttoFS(SD_MMC, "/tts_out.wav")

АЛЬТЕРНАТИВА (бесплатно, без API ключа):
  audio.connecttospeech("Текст", "ru");  // Google Translate TTS
```

### 6.5 API 4: OTA (прошивка по воздуху)

```
URL: https://api.tenclass.net/xiaozhi/ota/
Назначение: Обновление прошивки XiaoZhi по воздуху
```

### 6.6 Локальный Web-сервер (настройка WiFi)

```
Порт:    80 (AsyncWebServer)
AP IP:   192.168.4.1
Режим:   WiFi AP + STA (точка доступа + клиент)
Страницы:
  /         — главная (настройка WiFi, API ключи)
  /saved    — список сохранённых сетей
  /scan     — сканирование WiFi
  /submit   — сохранение настроек
  /advanced — расширенные настройки (RSS-ленты)
```

---

## 7. ЛОГИКА РАБОТЫ РОБОТА

### 7.1 Полный цикл разговора

```
1. ЗАГРУЗКА
   └─► initES8311()       — инициализация кодека по I2C
   └─► i2s_restore_tx()   — I2S в режим воспроизведения
   └─► speakText("Привет! Я Мелвин...") — приветствие

2. НАЖАТИЕ КНОПКИ (GP0)
   ├─► Короткое  → askRick("Скажи что-нибудь") → speakText()
   ├─► Двойное   → fetchRSS() → askRick() → speakText()
   └─► Долгое    → recordAndTranscribe() → askRick() → speakText()

3. ЗАПИСЬ (recordAndTranscribe):
   └─► audio.stopSong()
   └─► i2s_install_rx()        — переключить I2S на микрофон
   └─► i2s_read() × N          — читать PCM 5 сек (160000 байт)
   └─► i2s_driver_uninstall()
   └─► delay(50ms)             — обязательная пауза!
   └─► i2s_restore_tx()        — вернуть I2S на динамик
   └─► POST WAV → HuggingFace Whisper
   └─► return text

4. ВОСПРОИЗВЕДЕНИЕ (speakText):
   └─► PA_ENABLE = HIGH         — включить усилитель
   └─► POST text → HuggingFace MMS-TTS
   └─► Сохранить WAV на SD (/tts_out.wav)
   └─► i2s_restore_tx()
   └─► audio.connecttoFS(SD_MMC, "/tts_out.wav")
   └─► while audio.isRunning() { audio.loop() }
   └─► audio.stopSong()
```

### 7.2 Состояния эмоций (анимация дисплея)

| Состояние   | Цвет      | Когда активно              |
|-------------|-----------|----------------------------|
| NEUTRAL     | Белый     | Ожидание                   |
| THINKING    | Cyan      | Запрос к LLM / обработка   |
| HAPPY       | Зелёный   | После ответа               |
| NEWS        | Оранжевый | Чтение новостей             |
| LISTENING   | —         | Запись с микрофона          |
| SPEAKING    | Cyan      | Воспроизведение TTS         |
| ERROR_STATE | Красный   | Ошибка API / SD / сети      |
| EMO_WIFI_AP | —         | Режим точки доступа WiFi    |

---

## 8. ЗАВИСИМОСТИ И БИБЛИОТЕКИ

| Библиотека              | Версия   | Назначение                            |
|-------------------------|----------|---------------------------------------|
| LovyanGFX               | ^1.1.12  | Управление дисплеем (анимации)        |
| ArduinoJson             | ^7.0.4   | Парсинг JSON (API запросы/ответы)     |
| esphome/ESP32-audioI2S  | ^2.0.7   | Воспроизведение WAV/MP3 через I2S     |
| ESP Async WebServer     | ^1.2.3   | Локальный сервер настройки            |
| AsyncTCP                | ^1.1.1   | Зависимость AsyncWebServer            |
| Wire (built-in)         | —        | I2C управление ES8311                 |
| SD_MMC (built-in)       | —        | SD-карта (хранение TTS WAV)           |
| WiFiClientSecure        | —        | HTTPS соединения                      |
| driver/i2s_std (IDF v5) | —        | Новый I2S API для записи (RX)         |

---

## 9. КРИТИЧЕСКИЕ МОМЕНТЫ И KNOWN ISSUES

### ⚠️ GPIO 45 — Страп-пин!
GP45 используется как I2S_LRC, но это страп-пин ESP32-S3.
При буте он влияет на режим загрузки. В коде добавлена задержка
после инициализации для стабилизации.

### ⚠️ I2S — один драйвер на TX и RX
ESP32-S3 имеет один I2S порт (I2S_NUM_0) в этой прошивке.
Переключение между записью (RX) и воспроизведением (TX) требует:
- полного uninstall драйвера
- задержки 50-100ms
- переустановки

### ⚠️ Новый API i2s_std (ESP-IDF v5)
Старый `#include <driver/i2s.h>` (функции `i2s_driver_install`, `i2s_read`) устарел.
Используется новый `#include <driver/i2s_std.h>` с `i2s_new_channel` / `i2s_channel_read`.
Смешивать оба API нельзя!

### ⚠️ Микрофон — выбор канала
ES8311 посылает аудио в LEFT-канале стерео-фрейма.
DMA буфер: [L0, R0, L1, R1, ...] — брать `dma_buf[i*2]`.
Если голос не детектируется — попробовать правый канал `dma_buf[i*2+1]`.

### ⚠️ Фоновый шум −37..−42 dBFS
Плата имеет умеренный уровень фонового шума от помещения.
VAD порог речи должен быть не ниже −30 dBFS, иначе ложные срабатывания.
При работе рядом с ПК/кондиционером уровень фона может достигать −37 dBFS.

### ⚠️ ALC — выключить для STT!
ALC (Automatic Level Control) в ES8311 регистры 0x18–0x1B.
При включённом ALC Whisper распознаёт хуже из-за плавающего уровня.
Всегда: `0x18=0x00, 0x19=0x00, 0x1A=0x00, 0x1B=0x00`.

### ⚠️ BOD (Brown-Out Detector)
При питании от USB возможны ложные перезагрузки.
В коде отключён через `RTC_CNTL_BROWN_OUT_REG`:
```cpp
static void __attribute__((constructor(101))) disable_bod() {
    REG_CLR_BIT(RTC_CNTL_BROWN_OUT_REG, RTC_CNTL_BROWN_OUT_ENA);
}
```

### ⚠️ PA_ENABLE (GP46) = усилитель NS4150B
- При буте: LOW (усилитель выключен)
- При TTS: HIGH (включить до воспроизведения)
- Если GP46 не поднят — звука нет!

### ⚠️ SD-карта обязательна для TTS
TTS сохраняет WAV на SD (/tts_out.wav) перед воспроизведением.
Без SD-карты — TTS не работает!

### ⚠️ PSRAM обязателен
Запись 5 сек PCM = 160000 байт в RAM.
WAV буфер = 160044 байт.
Фраза 8 сек = 256000 байт (ps_malloc из PSRAM).
Без PSRAM (ps_malloc) — Out of Memory!

---

## 10. DATA-ФАЙЛЫ НА SD-КАРТЕ

### 10.1 Структура файлов

| Файл          | Где лежит        | Назначение                              |
|---------------|------------------|-----------------------------------------|
| config.txt    | Корень SD-карты  | WiFi, API ключи, RSS ленты              |
| networks.txt  | Корень SD-карты  | Сохранённые WiFi сети (SSID:ПАРОЛЬ)     |
| tts_out.wav   | Корень SD-карты  | Временный WAV от TTS (перезаписывается) |

### 10.2 Формат config.txt

```
Строка 1: SSID WiFi сети
Строка 2: Пароль WiFi
Строка 3: HuggingFace API Key (hf_...)
Строка 4: OpenRouter API Key (sk-or-...)
Строка 5+: RSS ленты (опционально)
```

Пример:
```
MyHomeWifi
password123
hf_xxxxxxxxxxxxxxxxxxxxxxxxxxxx
sk-or-v1-xxxxxxxxxxxxxxxxxxxx
https://lenta.ru/rss/news
```

---

## 11. БЫСТРЫЙ СТАРТ — ЧТО ПОПРАВИТЬ В ПРОШИВКЕ

### Шаг 1: API ключи
- HuggingFace: https://huggingface.co/settings/tokens → токен "hf_..."
- OpenRouter:  https://openrouter.ai/keys → токен "sk-or-..."

### Шаг 2: Проверить пины
Все пины соответствуют схеме платы (см. раздел 1).
Если другая плата — проверить GP45 (страп-пин!).

### Шаг 3: SD-карта
Скопировать config.txt в корень SD-карты с реальными данными.

### Шаг 4: PA_ENABLE обязателен
```cpp
pinMode(46, OUTPUT);
digitalWrite(46, HIGH);  // Без этого — нет звука!
```

### Шаг 5: Персонаж робота
```cpp
const String SYSTEM_PROMPT =
  "Ты — [имя]. [характер]. Отвечай по-русски, 2-3 предложения.";
```

### Шаг 6: Приветствие
```cpp
// В setup():
speakText("Привет! Я [имя], твой ИИ-ассистент. Готов к работе!");
```

### Шаг 7: Бесплатный TTS (без HuggingFace)
```cpp
// Вместо HuggingFace MMS-TTS — бесплатный Google TTS:
audio.connecttospeech("Текст для озвучки", "ru");
```

### Шаг 8: Диагностика микрофона
Если VAD не срабатывает — проверить по Serial Monitor:
```
pk > -30 dBFS при речи?   → нет  → сменить MIC1↔MIC2 (рег. 0x14: 0x1F↔0x3F)
                           → нет  → сменить L↔R канал  (dma_buf[i*2+1])
                           → нет  → проверить GPIO10 (I2S_DIN) физически
pk > -30 dBFS при речи?   → да   → VAD порог слишком высок, снизить до -25 dBFS
```

---

*Версия документа: 1.1 | Дата: апрель 2026*
*Плата: Spotpear MUMA 1.54 / ESP32-1.54inch-AI-V2*
*Составлен по: схеме платы (ESP32-1.54inch-AI-V2-1.pdf) + main.cpp v3.7.4 + сессия отладки апрель 2026*
