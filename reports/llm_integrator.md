# Отчёт: Multi-Provider LLM & Dialog Engineer
**Файлы:** `include/Agent.h`, `include/Config.h`  
**Дата:** 2026-06-05  
**Ревьюер:** Совет Мелвина — Multi-Provider LLM & Dialog Engineer

---

## 1. callGemini() — GeminiStream (zero-copy streaming)

### ✅ СТАТУС: РЕАЛИЗОВАН КОРРЕКТНО

`callGemini()` использует класс `GeminiStream : public Stream` (строки 24–122).
Аудио **не копируется в String** и **не буферизуется в SRAM**.

Схема работы:
```
SD-файл → readBytesImpl() → base64 по 3 байта → HTTP sendRequest(&gStream, totalLength)
```

**Как работает GeminiStream:**
- `jsonStart` / `jsonEnd` — строки-обёртки JSON (малые, на стеке)
- Аудио кодируется в base64 **побайтово** через `mbedtls_base64_encode()` порциями по 3 байта
- Никакой промежуточной копии всего base64-тела нет
- `http.sendRequest("POST", &gStream, totalLength)` — ESP32 HTTPClient читает данные из Stream напрямую

> **Замечание (некритично):** `b64Buf[8]` — стековый буфер на 8 символов.
> `mbedtls_base64_encode` для 3 байт даёт 4 символа + `\0`. Размер 8 — достаточен, но следует добавить assert на случай изменения логики:
> ```cpp
> static_assert(sizeof(b64Buf) >= 5, "b64Buf too small");
> ```

---

## 2. Каскад провайдеров: Gemini → Groq → OpenRouter

### ✅ СТАТУС: РЕАЛИЗОВАН, НО ЕСТЬ ПРОБЛЕМА

**Что работает:**
- `askAI()` (строки 263–303) сначала пробует `primaryProvider` из конфига
- Затем итерирует массив `{"gemini", "groq", "openrouter"}`, пропуская primary
- Внутри каждого провайдера — `tryProvider()` перебирает ключи через запятую

**⚠️ ПРОБЛЕМА — Порядок fallback жёстко захардкожен, а не определяется конфигом:**

```cpp
// Agent.h:281
String fallbackProviders[] = { "gemini", "groq", "openrouter" };
```

Если пользователь выставил `llm_provider = "groq"`, порядок fallback будет:
`groq → gemini → openrouter` (пропускает groq, затем gemini перед openrouter).
Это может быть нежелательно — пользователь может хотеть `groq → openrouter → gemini`.

**Рекомендация — добавить в `Config.h` поле `llm_fallback_order`:**

```cpp
// Config.h — добавить в struct MelvinConfig:
String llm_fallback_order; // e.g. "groq,openrouter" (без primary)

// ConfigManager::load() добавить:
config.llm_fallback_order = doc["llm_fallback_order"] | "";
```

```cpp
// Agent.h — askAI(), заменить жёсткий массив:
// БЫЛО:
String fallbackProviders[] = { "gemini", "groq", "openrouter" };
for (const String& provider : fallbackProviders) { ... }

// СТАЛО:
const char* defaultOrder[] = { "gemini", "groq", "openrouter" };
std::vector<String> fallbackList;
if (cfg.llm_fallback_order.length() > 2) {
    // Парсим пользовательский порядок
    int s = 0, e;
    String ord = cfg.llm_fallback_order;
    do {
        e = ord.indexOf(',', s);
        String p = (e == -1) ? ord.substring(s) : ord.substring(s, e);
        p.trim();
        if (p != primaryProvider && p.length() > 2) fallbackList.push_back(p);
        s = e + 1;
    } while (e != -1);
} else {
    for (auto& p : defaultOrder)
        if (String(p) != primaryProvider) fallbackList.push_back(p);
}
for (const String& provider : fallbackList) { ... }
```

---

## 3. История диалогов: JSONL на SD-карте

### ✅ СТАТУС: РЕАЛИЗОВАН КОРРЕКТНО

**appendToHistory()** (строки 178–191):
- Открывает `/history.jsonl` в режиме `FILE_APPEND`
- Пишет JSON-объект `{"role":"...","content":"..."}` + `println()` (символ `\n`)
- Файл корректно закрывается

**getRecentHistory()** (строки 193–234):
- Читает хвост файла (`maxBytes=2048` по умолчанию)
- Выравнивает по первому `\n` — не читает оборванные записи
- Формирует JSON-массив `[{...},{...}]` для контекста промпта

**⚠️ ПРОБЛЕМА #1 — `file.readString()` на 2048 байт в SRAM:**

```cpp
// строка 201
String lastChunk = file.readString(); // до 2048 байт в обычном heap!
```

`String` в Arduino хранится в обычном SRAM. 2048 байт — пограничный случай, но при частом вызове
и фрагментации heap возможен OOM. Рекомендуется:

```cpp
// БЫЛО:
String lastChunk = file.readString();

// СТАЛО (читаем только нужный хвост):
size_t toRead = (size > maxBytes) ? maxBytes : size;
char* rawBuf = (char*)heap_caps_malloc(toRead + 1, MALLOC_CAP_SPIRAM);
if (!rawBuf) { file.close(); return ""; }
file.read((uint8_t*)rawBuf, toRead);
rawBuf[toRead] = '\0';
String lastChunk = String(rawBuf);  // копия в SRAM, но исходный буфер в PSRAM
heap_caps_free(rawBuf);
```

**⚠️ ПРОБЛЕМА #2 — Отсутствует ротация файла истории:**

Файл `/history.jsonl` растёт бесконечно. На SD-карте это не критично, но возможно
замедление `file.seek()` на больших файлах FAT32. Рекомендуется добавить ограничение:

```cpp
void trimHistoryIfNeeded(size_t maxFileBytes = 512 * 1024) {
    if (!SD_MMC.exists("/history.jsonl")) return;
    File f = SD_MMC.open("/history.jsonl", FILE_READ);
    if (!f) return;
    size_t sz = f.size();
    f.close();
    if (sz < maxFileBytes) return;

    // Переименовываем старый в .bak, начинаем новый
    SD_MMC.remove("/history.jsonl.bak");
    SD_MMC.rename("/history.jsonl", "/history.jsonl.bak");
    Serial.println("[AGENT] History rotated.");
}
```

**⚠️ ПРОБЛЕМА #3 — `first` флаг не сбрасывается правильно:**

```cpp
// строки 208-228: переменная `first` инициализирована true,
// но сбрасывается только ПОСЛЕ вставки строки в formatted.
// При первой непустой строке `first` остаётся true → запятая не ставится. Логика верна.
// ОДНАКО: если после первой строки нет '\n' — цикл while завершится через break (строка 219),
// и `first` не сбросится — это не баг, но код неочевиден. Рекомендуется упростить.
```

---

## 4. Аллокация буферов — проверка PSRAM

### ⚠️ СТАТУС: ЧАСТИЧНО СООТВЕТСТВУЕТ ТРЕБОВАНИЯМ

| Буфер | Где | Размер | В PSRAM? | Оценка |
|---|---|---|---|---|
| `b64Buf[8]` | GeminiStream, стек | 8 байт | Нет (стек) | ✅ ОК |
| `rawBuf[3]` | GeminiStream, стек | 3 байта | Нет (стек) | ✅ ОК |
| `jsonStart` String | callGemini(), SRAM heap | ~200–500 байт | Нет | ✅ ОК (мало) |
| `jsonEnd` String | callGemini(), SRAM heap | ~20 байт | Нет | ✅ ОК |
| `headerStr` String | callGemini(), SRAM heap | ~200–500 байт | Нет | ✅ ОК |
| `resp` String | callGemini() строка 400 | **до 8–16KB** | ❌ НЕТ | ⚠️ РИСК |
| `body` String | callGroqChat() строка 482 | ~1–3KB | Нет | ⚠️ Умеренный |
| `body` String | callOpenRouterChat() строка 515 | ~1–3KB | Нет | ⚠️ Умеренный |
| `lastChunk` String | getRecentHistory() строка 201 | до 2048 байт | ❌ НЕТ | ⚠️ РИСК |
| `payload` String | getRSSHeadlines() строка 244 | **до 64KB** | ❌ НЕТ | 🔴 КРИТИЧНО |
| `formatted` String | getRecentHistory() строка 207 | до 2048 байт | ❌ НЕТ | ⚠️ РИСК |

### 🔴 КРИТИЧЕСКАЯ ПРОБЛЕМА — `getRSSHeadlines()` RSS payload в SRAM:

```cpp
// строка 244 — RSS XML может быть 50-100KB!
String payload = http.getString(); // → обычный SRAM → OOM crash
```

**Исправление:**

```cpp
String getRSSHeadlines(String url) {
    if (url.length() < 5) return "No news configured.";
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    String headlines = "";
    if (httpCode == HTTP_CODE_OK) {
        // Получаем Stream вместо копирования всего в String
        WiFiClient* stream = http.getStreamPtr();
        const size_t BUF_SIZE = 4096;
        char* buf = (char*)heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM);
        if (!buf) { http.end(); return ""; }
        
        String accum = "";
        int count = 0;
        int available;
        while ((available = stream->available()) && count < 5) {
            size_t toRead = min((size_t)available, BUF_SIZE - 1);
            size_t n = stream->readBytes(buf, toRead);
            buf[n] = '\0';
            accum += String(buf);
            // Парсим заголовки из накопленного
            int pos = 0;
            while ((pos = accum.indexOf("<title>", pos)) != -1 && count < 5) {
                int end = accum.indexOf("</title>", pos);
                if (end == -1) break; // неполный тег — ждём следующего чанка
                String title = accum.substring(pos + 7, end);
                if (title.indexOf("Lenta") == -1) {
                    headlines += "- " + title + "\n";
                    count++;
                }
                pos = end;
            }
            // Обрезаем уже обработанное начало accum
            int lastTitle = accum.lastIndexOf("<title>");
            if (lastTitle > 0) accum = accum.substring(lastTitle);
        }
        heap_caps_free(buf);
    }
    http.end();
    return headlines;
}
```

### ⚠️ ПРОБЛЕМА — `resp = http.getString()` в callGemini():

```cpp
// строка 400 — ответ Gemini может быть 4–16KB
String resp = http.getString();
```

**Исправление — десериализовать прямо из Stream:**

```cpp
// БЫЛО:
String resp = http.getString();
JsonDocument res;
DeserializationError err = deserializeJson(res, resp);

// СТАЛО (zero-copy, без String):
JsonDocument res;
WiFiClient* stream = http.getStreamPtr();
DeserializationError err = deserializeJson(res, *stream);
// resp больше не нужен — не выделяем String вообще
```

Аналогично применить в `callGroqChat()` (строка 487) и `callOpenRouterChat()` (строка 520).

---

## 5. Таймаут HTTP

### ✅ СТАТУС: ЧАСТИЧНО — только callGemini()

```cpp
// callGemini() строка 387:
http.setTimeout(30000); // ✅ 30 секунд
```

**❌ ПРОБЛЕМА — Остальные клиенты не устанавливают таймаут:**

| Метод | setTimeout()? | Умолчание |
|---|---|---|
| `callGemini()` | ✅ `30000ms` | — |
| `transcribeGroqWhisper()` | ❌ НЕТ | 5000ms (по умолч.) |
| `callGroqChat()` | ❌ НЕТ | 5000ms (по умолч.) |
| `callOpenRouterChat()` | ❌ НЕТ | 5000ms (по умолч.) |
| `getRSSHeadlines()` | ❌ НЕТ | 5000ms (по умолч.) |

**Исправление — добавить во все HTTP-клиенты:**

```cpp
// transcribeGroqWhisper() — после http.begin():
http.setTimeout(30000);

// callGroqChat() — после http.begin():
http.setTimeout(30000);

// callOpenRouterChat() — после http.begin():
http.setTimeout(30000);

// getRSSHeadlines() — после http.begin():
http.setTimeout(10000); // RSS быстрее — 10 сек достаточно
```

---

## 6. Groq Whisper Fallback для транскрипции

### ✅ СТАТУС: РЕАЛИЗОВАН КОРРЕКТНО

`transcribeGroqWhisper()` (строки 425–464):
- Использует `MultipartStream : public Stream` — zero-copy streaming WAV-файла
- Модель: `whisper-large-v3-turbo` — актуальная быстрая модель
- Multipart boundary: `----MelvinBoundary123456789`
- Content-Length явно устанавливается (`http.addHeader("Content-Length", ...)`)
- Правильно парсит `doc["text"]` из JSON-ответа

**⚠️ ПРОБЛЕМА — `MultipartStream::read()` вызывает `drawFace()` внутри цикла:**

```cpp
// строка 147-156: drawFace() вызывается при КАЖДОМ вызове read()!
int read() override {
    uint32_t now = millis();
    if (now - lastRedrawMs >= 80) {
        animTick++;
        drawFace(canvas, currentState, animTick);
        canvas.pushSprite(0, 0);
        lastRedrawMs = now;
    }
    ...
```

Метод `read()` вызывается HTTPClient для каждого байта данных.
`drawFace()` + `pushSprite()` — дорогостоящие операции на SPI дисплее.
Хотя throttle через `lastRedrawMs >= 80` смягчает проблему, это всё равно
создаёт джиттер в потоке данных. В `GeminiStream` эта логика вынесена в `readBytesImpl()`,
что правильнее.

**Рекомендация** — перенести redraw из `read()` в `readBytes()` (добавить метод `readBytes` в `MultipartStream`):

```cpp
class MultipartStream : public Stream {
    // ... существующие поля ...

    size_t readBytesImpl(uint8_t* buf, size_t len) {
        // Redraw throttle — здесь, а не в read()
        uint32_t now = millis();
        if (now - lastRedrawMs >= 80) {
            animTick++;
            drawFace(canvas, currentState, animTick);
            canvas.pushSprite(0, 0);
            lastRedrawMs = now;
        }
        size_t bytesRead = 0;
        while (bytesRead < len && streamPos < totalLen) {
            if (streamPos < header.length()) {
                buf[bytesRead++] = header[streamPos++];
                continue;
            }
            size_t fileEnd = header.length() + fileSize;
            if (streamPos < fileEnd) {
                buf[bytesRead++] = file.read();
                streamPos++;
                continue;
            }
            if (streamPos < totalLen) {
                buf[bytesRead++] = footer[streamPos++ - fileEnd];
            }
        }
        return bytesRead;
    }

public:
    int read() override {
        uint8_t c;
        return (readBytesImpl(&c, 1) == 1) ? c : -1;
    }

    size_t readBytes(char* buf, size_t len) override {
        return readBytesImpl((uint8_t*)buf, len);
    }
    size_t readBytes(uint8_t* buf, size_t len) override {
        return readBytesImpl(buf, len);
    }
    // ... остальное без изменений ...
};
```

---

## 7. Дополнительные проблемы

### ⚠️ Неверное имя модели Gemini (строка 389)

```cpp
// ТЕКУЩЕЕ:
"https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash:generateContent?key=" + key
```

Модели `gemini-3.5-flash` **не существует**. Актуальные имена на 2026-06:
- `gemini-2.5-flash` (рекомендуется)
- `gemini-2.0-flash`

**Исправление:**
```cpp
// СТАЛО:
"https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=" + key
```

Рекомендуется вынести имя модели в `Config.h`:
```cpp
// MelvinConfig:
String gemini_model;  // default: "gemini-2.5-flash"
String groq_model;    // default: "llama-3.3-70b-versatile"
String openrouter_model; // default: "meta-llama/llama-3-8b-instruct:free"

// MelvinConfig конструктор:
gemini_model = "gemini-2.5-flash";
groq_model = "llama-3.3-70b-versatile";
openrouter_model = "meta-llama/llama-3-8b-instruct:free";
```

### ⚠️ OpenRouter провайдер не устанавливает `HTTP-Referer` заголовок

OpenRouter требует для идентификации:
```cpp
http.addHeader("HTTP-Referer", "https://melvin-robot.local");
http.addHeader("X-Title", "Melvin Robot");
```
Без этого запросы могут отклоняться или получать низкий приоритет.

### ℹ️ Groq API endpoint устарел (строка 433)

```cpp
// ТЕКУЩЕЕ:
http.begin(client, "https://api.groq.com/v1/audio/transcriptions");
// Официальный: openai-compat endpoint
// АКТУАЛЬНО:
http.begin(client, "https://api.groq.com/openai/v1/audio/transcriptions");
```

Аналогично для Groq Chat (строка 471):
```cpp
// ТЕКУЩЕЕ:
http.begin(client, "https://api.groq.com/v1/chat/completions");
// СТАЛО:
http.begin(client, "https://api.groq.com/openai/v1/chat/completions");
```

### ℹ️ Config.h — отсутствует проверка валидности конфига

```cpp
// Рекомендуется добавить в MelvinConfig:
bool isValid() const {
    if (wifi_ssid.length() < 2) return false;
    // Хотя бы один провайдер должен иметь ключ
    return (gemini_keys.length() > 5 || groq_keys.length() > 5 || openrouter_keys.length() > 5);
}
```

---

## Итоговая сводка

| Проверка | Статус | Приоритет |
|---|---|---|
| GeminiStream zero-copy | ✅ Реализован | — |
| Каскад провайдеров | ✅ Работает / ⚠️ Порядок хардкожен | Средний |
| История JSONL на SD | ✅ Корректно / ⚠️ Нет ротации | Низкий |
| Буферы в PSRAM | ⚠️ RSS payload в SRAM (критично) | **Высокий** |
| Таймаут HTTP >= 30000ms | ❌ Только в callGemini() | **Высокий** |
| Groq Whisper fallback | ✅ Реализован корректно | — |
| Groq API endpoints | ❌ Устаревшие `/v1/` вместо `/openai/v1/` | **Высокий** |
| Имя модели Gemini | ❌ `gemini-3.5-flash` не существует | **Критично** |
| OpenRouter заголовки | ⚠️ Отсутствуют `HTTP-Referer` / `X-Title` | Средний |
| MultipartStream redraw | ⚠️ drawFace() в read() вместо readBytes() | Средний |

---

*Отчёт сформирован советом Мелвина, роль: Multi-Provider LLM & Dialog Engineer*
