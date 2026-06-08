# Отчёт: TTS Synthesizer — Google TTS & Audio Synthesis Specialist
**Файл:** `MelvinTTS.h` + `main.cpp::playWavFromSD`  
**Дата аудита:** 2026-06-05  
**Аудитор:** Google TTS & Audio Synthesis Specialist (Совет Мелвина)

---

## 1. Итоговая оценка

| Критерий | Статус | Комментарий |
|---|---|---|
| `sampleRateHertz = 16000` | ✅ ЕСТЬ | Строка 223, принудительно |
| Streaming base64 декодер | ✅ ДА | `decodeTtsStreamToWav()` |
| Буфер декодирования в PSRAM | ✅ ДА | `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` |
| Language Code из имени голоса | ⚠️ ЖЁСТКО ЗАШИТ | `"ru-RU"` без разбора `cfg.tts_voice` |
| Truncation текста > maxTtsChars | ✅ ДА | Обрезка до 200 символов |
| Redraw дисплея при скачивании TTS | ✅ ДА | Внутри цикла декодирования |
| `delay()` внутри stream-петли | ⚠️ ЕСТЬ | `delay(1)` при отсутствии данных |
| `client.setInsecure()` | ⚠️ ПРЕДУПРЕЖДЕНИЕ | TLS без верификации сертификата |
| Имя файла на SD | ⚠️ КОНФЛИКТ | Всегда `/resp.wav` — перезаписывается |
| `languageCode` не из голоса | 🔴 БАГ | Может выдать ошибку 400 при смене голоса |

---

## 2. Детальный анализ

### 2.1 `sampleRateHertz = 16000` — ✅ КОРРЕКТНО

```cpp
// MelvinTTS.h, строка 223
doc["audioConfig"]["sampleRateHertz"] = 16000; // Force 16kHz format
```

Значение присутствует явно. Формат: `LINEAR16` (WAV PCM, 16-bit). 
Это точно соответствует аппаратной конфигурации ES8311 / I2S (SAMPLE_RATE = 16000 в main.cpp).

---

### 2.2 Streaming декодер base64 — ✅ РАБОТАЕТ

Функция `decodeTtsStreamToWav()` реализует правильный потоковый подход:

1. **Не буферизует весь JSON** — ищет маркер `"audioContent"` посимвольно прямо в потоке.
2. **Декодирует base64 по 4 байта** → 3 байта PCM через `mbedtls_base64_decode`.
3. **Пишет на SD через промежуточный буфер** 64KB (PSRAM) для снижения количества операций записи.
4. **Обрабатывает хвост** (padding `=` при неполном блоке).
5. **Защита от зависания:** таймаут 15 сек на поиск маркера и 15 сек на чтение данных.

Качество реализации выше среднего. Подходит для ESP32-S3 с PSRAM.

---

### 2.3 Буфер декодирования — ✅ PSRAM, с fallback

```cpp
// MelvinTTS.h, строки 96-108
const size_t decodeBufSize = 64 * 1024; // 64KB
uint8_t* decodeBuf = (uint8_t*)heap_caps_malloc(decodeBufSize, MALLOC_CAP_SPIRAM);
if (!decodeBuf) {
    // fallback на внутренний heap 8KB
    decodeBuf = (uint8_t*)malloc(8 * 1024);
}
```

**Оценка:** Правильно. PSRAM — первый приоритет, internal heap — резерв.  
При работе на 8KB fallback — запись на SD будет происходить значительно чаще (каждые ~8KB вместо 64KB), что замедляет работу, но не ломает.

---

### 2.4 Language Code — 🔴 БАГ (жёсткая привязка к `ru-RU`)

```cpp
// MelvinTTS.h, строка 220 — ПРОБЛЕМА
doc["voice"]["languageCode"] = "ru-RU";   // ЖЁСТКО ЗАШИТО!
doc["voice"]["name"]         = cfg.tts_voice;
```

**Проблема:** если пользователь настроит в WebUI голос `en-US-Neural2-A` или любой другой не-русский голос, Google TTS вернёт ошибку HTTP 400, потому что `languageCode` и `name` не совпадают.

По документации Google TTS: `languageCode` **должен соответствовать** первым двум частям имени голоса (например, `en-US-Neural2-A` → `en-US`).

**Исправление:**

```cpp
// Извлечь languageCode из имени голоса (первые 5 символов, e.g., "ru-RU")
String langCode = "ru-RU"; // Дефолт
if (cfg.tts_voice.length() >= 5) {
    langCode = cfg.tts_voice.substring(0, 5); // "ru-RU" из "ru-RU-Wavenet-A"
}
doc["voice"]["languageCode"] = langCode;
doc["voice"]["name"]         = cfg.tts_voice;
```

> [!CAUTION]
> Этот баг латентный: пока голос `ru-RU-*` — всё работает. При смене голоса через WebUI — TTS перестанет работать без видимых причин (только лог `Google Error 400`).

---

### 2.5 Truncation текста — ✅ КОРРЕКТНО

```cpp
// MelvinTTS.h, строки 202-206
const size_t maxTtsChars = 200;
if (text.length() > maxTtsChars) {
    Serial.printf("[TTS] Text too long (%d chars), truncating to %d\n", ...);
    text = text.substring(0, maxTtsChars);
}
```

Обрезка работает. 200 символов — разумный предел для ESP32.  

**Незначительное замечание:** обрезка не учитывает границы слов. Текст может обрываться посередине слова. Рекомендуется обрезать до последнего пробела в пределах лимита:

```cpp
if (text.length() > maxTtsChars) {
    text = text.substring(0, maxTtsChars);
    int lastSpace = text.lastIndexOf(' ');
    if (lastSpace > 100) text = text.substring(0, lastSpace); // Обрезка по границе слова
    Serial.printf("[TTS] Truncated to %d chars\n", text.length());
}
```

---

### 2.6 Redraw дисплея во время скачивания — ✅ РАБОТАЕТ

```cpp
// MelvinTTS.h, строки 119-125 (внутри цикла while(true))
uint32_t now = millis();
if (now - lastRedrawMs >= 80) {
    animTick++;
    drawFace(canvas, currentState, animTick);
    canvas.pushSprite(0, 0);
    lastRedrawMs = now;
}
```

Периодичность 80ms — совпадает с `REDRAW_MS` из main.cpp. Лицо Мелвина анимируется во время скачивания TTS. ✅

---

## 3. Найденные проблемы

### 🔴 Проблема #1: `delay(1)` внутри stream-петли

```cpp
// MelvinTTS.h, строки 66, 84, 163
} else {
    delay(1);  // ← БЛОКИРУЮЩИЙ ВЫЗОВ
}
```

`delay()` вызывается в основном потоке при ожидании данных из WiFi-стрима. Это не ISR/callback, поэтому технически не нарушает критические правила прошивки. Однако при медленном соединении анимация дисплея может "дёргаться" (delay срабатывает чаще, чем 80ms такт redraw).

**Рекомендация:** заменить на `yield()` или `taskYIELD()`, чтобы уступать CPU другим задачам FreeRTOS:

```cpp
} else {
    yield(); // Уступаем CPU вместо блокировки
}
```

---

### ⚠️ Проблема #2: `client.setInsecure()` — TLS без верификации

```cpp
// MelvinTTS.h, строка 210
client.setInsecure(); // Пропуск верификации TLS сертификата
```

Google TTS API использует HTTPS. Без верификации сертификата возможна MITM-атака в недоверенной сети. На ESP32-S3 хранение CA root certificate допустимо.

**Рекомендация (опциональная, для продакшен-версии):** добавить `client.setCACert(google_root_ca)`.

---

### ⚠️ Проблема #3: Статическое имя файла `/resp.wav`

```cpp
// MelvinTTS.h, строка 231
bool success = decodeTtsStreamToWav(stream, "/resp.wav");
```

Файл всегда перезаписывается. При параллельном запросе (если агент запустит два TTS подряд) — второй запрос перезапишет файл до окончания воспроизведения первого.

**Рекомендация:** это некритично для однопоточного ESP32, но лучше использовать:
```cpp
const char* ttsPath = "/tts_" + String(millis() % 1000) + ".wav";
// или просто задокументировать ограничение
```

---

### ℹ️ Проблема #4: `rawBuf` на стеке в `playWavFromSD`

```cpp
// main.cpp, строка 255
uint8_t rawBuf[bufSize]; // 1024 байт на стеке
```

1024 байта на стеке — приемлемо для ESP32-S3 (стек по умолчанию 8KB+). Но при добавлении вложенных вызовов может стать проблемой.

**Рекомендация:** перенести в PSRAM если `bufSize` вырастет:
```cpp
uint8_t* rawBuf = (uint8_t*)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
```

---

## 4. Рекомендуемые исправления (приоритет)

| Приоритет | Проблема | Действие |
|---|---|---|
| 🔴 ВЫСОКИЙ | `languageCode` жёстко зашит | Извлекать из `cfg.tts_voice.substring(0, 5)` |
| 🟡 СРЕДНИЙ | `delay(1)` в stream-петле | Заменить на `yield()` |
| 🟡 СРЕДНИЙ | Обрезка не по границе слова | Обрезать до последнего пробела |
| 🟢 НИЗКИЙ | `setInsecure()` | Добавить CA cert (опционально) |
| 🟢 НИЗКИЙ | `/resp.wav` статическое имя | Задокументировать ограничение |

---

## 5. Итог

`MelvinTTS.h` реализован грамотно с учётом ограничений ESP32-S3:
- Streaming декодирование правильно экономит RAM
- PSRAM используется для крупных буферов
- PA_CTRL (GPIO46) управляется корректно в `playWavFromSD`
- Redraw дисплея работает во время скачивания

**Единственный баг, требующий немедленного исправления** — жёсткая привязка `languageCode = "ru-RU"`. Это сломает TTS при любой смене голоса через WebUI.
