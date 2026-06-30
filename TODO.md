# Мелвин — Список доработок

Актуально для ветки `say-project`. Статусы: ✅ готово / ❌ не сделано / 🔶 частично.

---

## 1. `include/Config.h` — новые поля конфига

**Статус: ❌ не сделано**

Добавить в структуру `MelvinConfig`:

```cpp
String tts_provider2;      // Fallback TTS провайдер (если основной упал)
bool   wake_word_enabled;  // true = активация по слову, false = только кнопка BOOT
int    vad_silence_ms;     // Настраиваемая пауза тишины (сейчас hardcode 1500ms)
String tts_voice_yandex;   // Голос для Yandex отдельно (например "filipp")
String tts_voice_google;   // Голос/язык для Google отдельно (например "ru")
```

Добавить в конструктор `MelvinConfig()`:
```cpp
tts_provider2     = "none";
wake_word_enabled = false;   // По умолчанию — только кнопка
vad_silence_ms    = 1500;
tts_voice_yandex  = "filipp";
tts_voice_google  = "ru";
```

Добавить в `load()` и `save()` соответствующие JSON поля:
```cpp
// load():
config.tts_provider2     = doc["tts_provider2"]     | "none";
config.wake_word_enabled = doc["wake_word_enabled"]  | false;
config.vad_silence_ms    = doc["vad_silence_ms"]     | 1500;
config.tts_voice_yandex  = doc["tts_voice_yandex"]   | "filipp";
config.tts_voice_google  = doc["tts_voice_google"]   | "ru";

// save():
doc["tts_provider2"]     = config.tts_provider2;
doc["wake_word_enabled"] = config.wake_word_enabled;
doc["vad_silence_ms"]    = config.vad_silence_ms;
doc["tts_voice_yandex"]  = config.tts_voice_yandex;
doc["tts_voice_google"]  = config.tts_voice_google;
```

---

## 2. `include/MelvinTTS.h` — fallback провайдер + retry на 429

**Статус: ❌ не сделано**

### 2a. Fallback цепочка в `speak()`

Сейчас при ошибке сразу идёт `speakRandomPhrase()`. Нужно:

```
speak(text, cfg):
  1. Пробуем cfg.tts_provider
  2. При неудаче → пробуем cfg.tts_provider2   ← НОВОЕ
  3. При неудаче → speakRandomPhrase()
```

Изменить метод `speak()`:
```cpp
void speak(String text, const MelvinConfig& cfg) {
    if (text.length() == 0) return;
    text.replace("*", "");
    text.replace("`", "");

    bool ok = trySpeak(text, cfg.tts_provider, cfg);

    // Fallback к второму провайдеру
    if (!ok && cfg.tts_provider2.length() > 0 && cfg.tts_provider2 != "none") {
        Serial.println("[TTS] Primary failed, trying fallback provider...");
        ok = trySpeak(text, cfg.tts_provider2, cfg);
    }

    if (!ok) speakRandomPhrase();
}

bool trySpeak(const String& text, const String& provider, const MelvinConfig& cfg) {
    if (provider == "yandex") {
        return synthesizeYandex(text, cfg);
    } else if (provider == "google_free") {
        // chunk logic (уже есть в текущем speak())
        // перенести сюда
        return true;
    }
    return false;
}
```

### 2b. Retry на HTTP 429 в `synthesizeGoogleFree()`

Добавить после `int code = http.GET();`:
```cpp
if (code == 429) {
    Serial.println("[TTS][GoogleFree] Rate limited (429), retrying after 2s...");
    http.end();
    delay(2000);
    // повторный запрос
    http.begin(secureClient, url);
    http.setUserAgent("Mozilla/5.0 ...");
    http.addHeader("Referer", "https://translate.google.com/");
    http.addHeader("Accept", "audio/mpeg");
    code = http.GET();
    Serial.printf("[TTS][GoogleFree] Retry HTTP %d\n", code);
}
```

### 2c. Использовать отдельные голоса `tts_voice_yandex` / `tts_voice_google`

В `synthesizeYandex()` заменить:
```cpp
// было:
String voice = (cfg.tts_voice.length() > 0) ? cfg.tts_voice : "filipp";
// стало:
String voice = (cfg.tts_voice_yandex.length() > 0) ? cfg.tts_voice_yandex : "filipp";
```

В `synthesizeGoogleFree()` заменить параметр языка:
```cpp
// было:
String lang = (cfg.tts_language.length() > 0) ? cfg.tts_language : "ru";
// стало:
String lang = (cfg.tts_voice_google.length() > 0) ? cfg.tts_voice_google : "ru";
```

---

## 3. `include/WebUI.h` — улучшения интерфейса

**Статус: ❌ не сделано**

### 3a. Кнопка «🔊 Тест голоса»

Добавить в секцию TTS кнопку:
```html
<button class="btn btn-secondary btn-sm" onclick="testTTS()">🔊 Тест голоса</button>
```
```js
function testTTS() {
    fetch('/api/test-tts', {method: 'POST'})
        .then(r => r.ok ? showToast('Воспроизведение...') : showToast('Ошибка теста'));
}
```

Добавить эндпоинт в `main.cpp` → `startWebServer()`:
```cpp
server.on("/api/test-tts", HTTP_POST, [](AsyncWebServerRequest* req) {
    tts.speakAsync("Привет, я Мелвин. TTS работает нормально.", configMgr.config);
    AsyncWebServerResponse* res = req->beginResponse(200, "text/plain", "OK");
    res->addHeader("Access-Control-Allow-Origin", "*");
    req->send(res);
});
```

### 3b. Динамические поля TTS (показывать/скрывать по провайдеру)

Добавить JS в WebUI:
```js
function updateTTSFields() {
    const provider = document.getElementById('tts_provider').value;
    const needsKey    = ['yandex', 'elevenlabs'].includes(provider);
    const needsVoice  = ['yandex'].includes(provider);
    const googleBlock = provider === 'google_free';

    document.getElementById('tts-key-block').style.display   = needsKey    ? 'block' : 'none';
    document.getElementById('tts-voice-block').style.display = needsVoice  ? 'block' : 'none';
    document.getElementById('tts-lang-block').style.display  = googleBlock ? 'block' : 'none';
}

// Вызвать при загрузке и при смене
document.getElementById('tts_provider').addEventListener('change', updateTTSFields);
window.addEventListener('load', updateTTSFields);
```

### 3c. Подсказка для поля wake_word

```html
<!-- Было: -->
<input type="text" id="wake_word" placeholder="Мелвин">

<!-- Стало: -->
<input type="text" id="wake_word" placeholder="Мелвин / Melvin">
<div class="field-hint">⚠️ Пишите точно так, как произносите. Работает на кириллице и латинице.</div>
```

### 3d. Секция «Режим активации» с переключателем

Добавить новую карточку настроек:
```html
<div class="card settings-card">
  <h3>🎤 Активация</h3>
  <div class="field-group">
    <label>Режим запуска</label>
    <div class="radio-group">
      <label><input type="radio" name="wake_mode" value="button" id="mode_button"> Кнопка BOOT (всегда работает)</label>
      <label><input type="radio" name="wake_mode" value="word"   id="mode_word">   По кодовому слову</label>
    </div>
  </div>
  <div id="wake-word-block" style="display:none">
    <label for="wake_word">Кодовое слово</label>
    <input type="text" id="wake_word" placeholder="Мелвин / Melvin">
    <div class="field-hint">⚠️ Пишите точно так, как произносите.</div>
  </div>
</div>
```
```js
document.querySelectorAll('input[name="wake_mode"]').forEach(r =>
    r.addEventListener('change', () => {
        const isWord = document.getElementById('mode_word').checked;
        document.getElementById('wake-word-block').style.display = isWord ? 'block' : 'none';
    })
);
```

Добавить поле `wake_word_enabled` в GET `/api/config` и POST `/api/save`.

---

## 4. `src/main.cpp` — pre-buffer + wake word проверка

**Статус: ❌ не сделано**

### Текущее поведение (проблема):
```cpp
// VAD → сразу старт записи (кодовое слово не проверяется!)
if (vad_process(vad_inst, s_vad_buf, SAMPLE_RATE, 30) == VAD_SPEECH) {
    recorder.startRecording();
    ...
}
```

### Нужная логика (pre-buffer):

```
[IDLE] → VAD обнаружил речь
    → накапливаем pre_buf (1.5 сек аудио)
    → если wake_word_enabled = false → сразу startRecording()
    → если wake_word_enabled = true:
        → отправляем pre_buf на STT (быстрый Groq Whisper)
        → если текст содержит wake_word (case-insensitive) → startRecording() + beep
        → иначе → очистить буфер, вернуться в IDLE
```

Для реализации нужно:
1. Добавить `STATE_WAKE_CHECK` в `MelvinState.h`
2. Завести `static int16_t s_pre_buf[]` на 1.5 сек = 24000 сэмплов = 48KB (PSRAM!)
3. Написать метод `agent.transcribeRaw(buf, len, cfg)` для быстрой транскрипции
4. Добавить проверку в loop():

```cpp
// В STATE_IDLE, после vad_process == VAD_SPEECH:
if (!configMgr.config.wake_word_enabled) {
    // Старое поведение — сразу запись
    recorder.startRecording();
    recorder.prependBuffer(s_vad_buf, 480);
    setState(STATE_RECORDING);
} else {
    // Новое поведение — накопить pre_buf и проверить слово
    setState(STATE_WAKE_CHECK);
    // ... накопление и проверка
}
```

---

## 5. `include/Recorder.h` — VAD окно для wake word

**Статус: 🔶 частично (VAD работает, но окно мало для wake word)**

Текущее окно VAD: **480 сэмплов = 30ms** — достаточно для детекции речи,
но недостаточно для надёжного распознавания wake word.

При реализации п.4 pre-buffer автоматически решает эту проблему —
накапливаем 1.5 сек перед отправкой в STT.

---

## 6. `src/main.cpp` — WiFi fallback в AP-mode

**Статус: 🔶 частично**

Текущая ситуация: реконнект при разрыве есть (каждые 15 сек), но после
длительного разрыва (>5 мин) устройство не переходит в AP-mode автоматически.

Добавить счётчик неудачных реконнектов:
```cpp
static int wifiFailCount = 0;
if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    wifiFailCount++;
    if (wifiFailCount >= 20) {  // 20 × 15сек = 5 мин
        wifiFailCount = 0;
        Serial.println("[WiFi] Too many failures, switching to AP mode");
        startAPMode();
    }
} else {
    wifiFailCount = 0;
}
```

---

## Приоритеты выполнения

| # | Задача | Файл | Сложность | Приоритет |
|---|--------|------|-----------|-----------|
| 1 | Кнопка «Тест голоса» + эндпоинт | `WebUI.h`, `main.cpp` | Низкая | 🔴 Высокий |
| 2 | Динамические поля TTS в UI | `WebUI.h` | Низкая | 🔴 Высокий |
| 3 | Подсказка wake_word + секция активации | `WebUI.h` | Низкая | 🔴 Высокий |
| 4 | Поля `tts_provider2`, `wake_word_enabled` и др. | `Config.h` | Низкая | 🟡 Средний |
| 5 | Fallback TTS провайдер + retry 429 | `MelvinTTS.h` | Средняя | 🟡 Средний |
| 6 | WiFi fallback в AP-mode после 5 мин | `main.cpp` | Низкая | 🟡 Средний |
| 7 | Pre-buffer + wake word проверка (STT) | `main.cpp`, `Agent.h` | Высокая | 🟢 Низкий |
