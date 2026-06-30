# Архитектурный отчёт: Система Мелвин v7.0
**Дата:** 2026-06-05  
**Архитектор:** Совет разработчиков прошивки Мелвин  
**Файлы:** `main.cpp` (627 строк), `MelvinState.h`, `Config.h`, `WebUI.h`

---

## 1. FSM — Конечный Автомат Состояний

### 1.1 Определённые состояния (`MelvinState.h`)

| Состояние       | Описание                          |
|-----------------|-----------------------------------|
| `STATE_BOOT`    | Начальная загрузка                |
| `STATE_CONFIG_AP` | AP-режим, нет WiFi              |
| `STATE_CONNECTING` | Подключение к WiFi             |
| `STATE_IDLE`    | Ожидание, VAD слушает             |
| `STATE_RECORDING` | Запись голоса                  |
| `STATE_THINKING` | Запрос к AI                      |
| `STATE_SPEAKING` | Воспроизведение ответа           |
| `STATE_ERROR`   | Ошибка (определено, но не используется) |

### 1.2 Реальные переходы FSM (из кода)

```
STATE_BOOT
    ↓ (setup())
STATE_CONNECTING  ← connectWifi() вызывает setState()
    ↓ WL_CONNECTED
STATE_SPEAKING    ← "Я готов к работе!"
    ↓
STATE_IDLE
    ↓ VAD / кнопка
STATE_RECORDING
    ↓ тишина 1.5с / кнопка / таймаут 10с
STATE_THINKING
    ↓
STATE_SPEAKING
    ↓
STATE_IDLE

Альтернатива:
STATE_CONNECTING → STATE_CONFIG_AP (нет WiFi)
```

### 1.3 Проблемы FSM

**КРИТИЧНО [FSM-01]: `STATE_ERROR` определён, но нигде не используется**
- Ошибки SD-карты, ошибки кодека, провалы `askAI()` — всё обрабатывается молча
- Пользователь не видит лицо "ошибки", анимация не меняется
- **Решение:** Добавить переход в `STATE_ERROR` при PSRAM alloc fail, codec init fail, SD fail

**ПРЕДУПРЕЖДЕНИЕ [FSM-02]: Переход `STATE_BOOT → STATE_CONNECTING` не отображается на дисплее**
- В `setup()` нет `setState(STATE_BOOT)` — начальное состояние установлено через инициализатор переменной `currentState = STATE_BOOT`
- `drawFace(canvas, STATE_BOOT, 0)` вызывается напрямую, минуя `setState()`
- `setState()` проверяет `if (currentState == s) return;`, значит первый вызов `setState(STATE_CONNECTING)` сработает корректно
- **Статус:** Допустимо, но неочевидно — рекомендуется документировать

**ПРЕДУПРЕЖДЕНИЕ [FSM-03]: В состоянии `STATE_THINKING` и `STATE_SPEAKING` дисплей не перерисовывается в loop()**
- Оба состояния выполняются синхронно (`askAI()`, `tts.speak()`)
- Единственный редрав во время воспроизведения — внутри `playWavFromSD()`, но только там
- Во время `askAI()` (STATE_THINKING) дисплей полностью "замораживается"
- **Решение:** Внутри `Agent::askAI()` добавить периодический вызов `drawFace()` через указатель на callback или FreeRTOS таск

---

## 2. Порядок инициализации в `setup()`

### 2.1 Фактический порядок (строки 445–496)

```
1. Serial.begin(115200)                    [L446]
2. pinMode(BOOT_BTN_PIN, INPUT_PULLUP)     [L447]
3. pinMode(PA_CTRL_PIN, OUTPUT)            [L448]
4. digitalWrite(PA_CTRL_PIN, LOW)          [L449]  ✅ PA_CTRL=LOW ПЕРВЫМ
5. lcd.init() + canvas + drawFace()        [L452-455] ✅ Display
6. SD_MMC.setPins(17,18,21) + begin()      [L458-475] ✅ SD Card 1-bit
7. i2s_duplex_init()                       [L478]     ✅ I2S → MCLK активен
8. delay(200)                              [L479]     ✅ Стабилизация MCLK
9. Wire.begin(SDA=15, SCL=14)              [L482]     ✅ I2C
10. initES8311()                           [L483]     ✅ Кодек ПОСЛЕ MCLK
11. vad_create(VAD_MODE_3)                 [L486]     ✅ VAD
12. recorder.begin()                       [L489]     ✅ Recorder (PSRAM)
13. WiFi.setAutoReconnect(true)            [L494]     ✅ Auto-reconnect
14. connectWifi()                          [L495]     ✅ WiFi
```

### 2.2 Оценка порядка инициализации

| Пункт | Требование | Факт | Статус |
|-------|-----------|------|--------|
| PA_CTRL=LOW первым | До любого аудио | L449 (4-й шаг) | ✅ |
| Display init | До WiFi | L452 (5-й шаг) | ✅ |
| SD Card 1-bit | Пины 17,18,21 | L458 `setPins(17,18,21)` | ✅ |
| i2s_duplex_init() → delay(200) | До ES8311 | L478→L479 | ✅ |
| initES8311() после MCLK | MCLK активен | L483 (после I2S) | ✅ |
| vad_create(VAD_MODE_3) | Любое место | L486 | ✅ |
| WiFi.setAutoReconnect(true) | До connectWifi | L494 | ✅ |
| connectWifi() | После всего | L495 | ✅ |

**Вывод: Порядок инициализации корректен. Все критические требования соблюдены.**

### 2.3 Замечания по `setup()`

**ПРЕДУПРЕЖДЕНИЕ [INIT-01]: `configMgr.load()` вызывается только если SD готова**
```cpp
if (SD_MMC.begin("/sdcard", true)) {
    sdReady = true;
    configMgr.load();
}
```
- Если SD не найдена, `config` останется с дефолтами — это нормально
- НО `recorder.begin()` также зависит от наличия SD для сохранения WAV!
- `sdReady = false` + попытка `recorder.stopAndSave()` вернёт пустой путь
- **Статус:** Защита есть (проверка `path.length() > 0`), но лог предупреждения отсутствует

**ПРЕДУПРЕЖДЕНИЕ [INIT-02]: `recorder.begin()` при провале не блокирует работу**
```cpp
if (!recorder.begin()) {
    Serial.println("[SETUP] FATAL: Recorder PSRAM alloc failed!");
    // ← нет halt, нет setState(STATE_ERROR)
}
```
- При отсутствии PSRAM система продолжит работу, но запись будет невозможна
- **Решение:** `setState(STATE_ERROR); while(1) { delay(1000); }` или циклическая индикация на дисплее

---

## 3. `setState()` — Переключение I2S

### 3.1 Реализация (строки 328–347)

```cpp
void setState(RobotState s) {
    if (currentState == s) return;
    
    // Централизованное переключение I2S
    if (s == STATE_IDLE || s == STATE_RECORDING) {
        switchToRX();
    } else {
        switchToTX();
    }
    
    if (s == STATE_RECORDING) {
        vad_processed_frames = 0;
    }
    
    currentState = s;
    drawFace(canvas, currentState, animTick);
    canvas.pushSprite(0, 0);
    lastRedrawMs = millis();
}
```

### 3.2 Анализ `switchToRX()` / `switchToTX()`

```cpp
void switchToRX() {
    // Flush буфера RX (до 50 итераций)
    size_t br = 0;
    int16_t dummy[128];
    int limit = 50;
    while (limit-- > 0 && i2s_channel_read(..., 0) == ESP_OK && br > 0) { }
}

void switchToTX() {
    // No-op — полный дуплекс, MCLK всегда активен
}
```

**Архитектурное решение: Full-Duplex режим** — оба канала I2S (TX и RX) активны одновременно. Переключение сводится к сбросу RX-буфера перед прослушиванием.

### 3.3 Проблемы `setState()`

**КРИТИЧНО [FSM-04]: `setState()` вызывает `switchToTX()` для `STATE_THINKING`**
```cpp
} else {
    switchToTX(); // вызывается для STATE_THINKING, STATE_SPEAKING, STATE_CONNECTING...
}
```
- `switchToTX()` — no-op, проблемы нет на практике
- НО семантически неверно: `STATE_THINKING` не использует I2S вообще
- Лишний вызов безвреден, но создаёт путаницу при отладке

**ПРЕДУПРЕЖДЕНИЕ [FSM-05]: `switchToRX()` с таймаутом `0` (non-blocking)**
```cpp
i2s_channel_read(rx_handle, dummy, sizeof(dummy), &br, 0)
```
- Таймаут `0` означает чтение только готовых данных из DMA
- При полном дуплексе это правильно — не блокирует, flush происходит быстро
- **Статус:** Корректно

**ПРЕДУПРЕЖДЕНИЕ [FSM-06]: Дисплей в `setState()` обновляется до записи `currentState`**
```cpp
// НЕВЕРНЫЙ ПОРЯДОК:
currentState = s;           // ← сначала обновляем состояние
drawFace(canvas, currentState, animTick);  // ← потом рисуем (OK, использует currentState)
```
- Фактически порядок правильный: сначала `currentState = s`, потом `drawFace`
- **Статус:** Корректно

---

## 4. WiFi Reconnect

### 4.1 Механизм (строки 494, 530–541)

```cpp
// setup():
WiFi.setAutoReconnect(true);  // ✅ Фоновый авто-реконнект ESP32

// loop() — проверка каждые 15 секунд в STATE_IDLE:
if (currentState == STATE_IDLE && (now - lastWifiCheckMs > 15000)) {
    if (WiFi.status() != WL_CONNECTED) {
        wifiConnected = false;
    } else if (!wifiConnected) {
        wifiConnected = true;
    }
}
```

### 4.2 Оценка

| Механизм | Статус |
|----------|--------|
| `WiFi.setAutoReconnect(true)` | ✅ Установлен ДО `connectWifi()` |
| Мониторинг только в `STATE_IDLE` | ⚠️ Не работает во время RECORDING/THINKING |
| Флаг `wifiConnected` синхронизируется | ✅ |
| Повторная попытка ручного коннекта | ❌ Нет — только пассивный мониторинг |

**ПРЕДУПРЕЖДЕНИЕ [WIFI-01]: Мониторинг WiFi не работает в STATE_RECORDING/THINKING/SPEAKING**
- Если WiFi отвалился во время записи, `wifiConnected` останется `true`
- Следующий вызов `agent.askAI()` провалится молча
- **Решение:** Проверять `WiFi.status()` непосредственно перед `askAI()`:
```cpp
if (path.length() > 0 && WiFi.status() == WL_CONNECTED) {
    // вместо wifiConnected
}
```

**ПРЕДУПРЕЖДЕНИЕ [WIFI-02]: При `startAPMode()` `WiFi.setAutoReconnect()` не сбрасывается**
- В AP-режиме auto-reconnect пытается подключиться к прежней AP
- `WiFi.mode(WIFI_AP)` должен отменять это, но явно стоит добавить `WiFi.setAutoReconnect(false)` в `startAPMode()`

---

## 5. `shouldReboot` — Безопасная перезагрузка

### 5.1 Реализация

```cpp
// WebServer callback (другой поток):
shouldReboot = true;  // [L384]

// loop() (основной поток):
if (shouldReboot) {
    Serial.println("[SYSTEM] Safe rebooting in 500ms...");
    delay(500);
    ESP.restart();  // [L519]
}
```

### 5.2 Оценка

| Аспект | Статус |
|--------|--------|
| Флаг устанавливается из callback AsyncWebServer | ✅ |
| Проверка и reboot в основном потоке loop() | ✅ |
| delay(500) перед restart() | ✅ Даёт время отправить HTTP-ответ |
| `volatile` для `shouldReboot` | ❌ ОТСУТСТВУЕТ — потенциальный race condition |

**КРИТИЧНО [REBOOT-01]: `shouldReboot` не объявлен как `volatile`**
```cpp
// Текущий код (L47):
bool shouldReboot = false;

// Должно быть:
volatile bool shouldReboot = false;
```
- `AsyncWebServer` использует FreeRTOS таски на другом ядре (Core 0)
- Компилятор может кешировать `shouldReboot` в регистре
- Без `volatile` основной поток может никогда не увидеть изменение
- **Риск:** Мелвин не перезагружается после сохранения конфига через WebUI

---

## 6. Display Redraw во всех состояниях

### 6.1 Карта перерисовок

| Место | Состояния | Механизм |
|-------|-----------|----------|
| `loop()` L523–528 | IDLE, RECORDING, CONFIG_AP | Каждые 80мс (`REDRAW_MS`) |
| `playWavFromSD()` L294–300 | SPEAKING | Каждые 80мс внутри цикла |
| `setState()` L344–346 | Все | Немедленно при переходе |
| `connectWifi()` L410–413 | CONNECTING | В цикле ожидания WiFi |

### 6.2 Проблемы перерисовки

**КРИТИЧНО [DISP-01]: STATE_THINKING — дисплей заморожен**
- `askAI()` — синхронный HTTP-запрос, может длиться 3–15 секунд
- В это время `loop()` не выполняется, редравов нет
- Анимация "думающего лица" не воспроизводится
- **Решение:** Вынести `askAI()` в FreeRTOS task или добавить HTTP progress callback

**КРИТИЧНО [DISP-02]: STATE_SPEAKING — частичный редрав**
- Редрав есть только внутри `playWavFromSD()` — только пока играет WAV с SD
- Если `tts.speak()` использует HTTP-стриминг без SD-файла, редрав отсутствует
- **Решение:** Аналогично — callback редрав или проверить реализацию `MelvinTTS`

**ПРЕДУПРЕЖДЕНИЕ [DISP-03]: CONFIG_AP не имеет анимационного таймера**
```cpp
// loop():
if (currentState == STATE_CONFIG_AP) dnsServer.processNextRequest();
// ↓
// Далее: кнопка, shouldReboot, REDRAW...
```
- Редрав через `loop()` L523 работает и для CONFIG_AP — **OK**
- НО: нет VAD-чтения, нет interaction logic — всё ожидание идёт через WebUI

---

## 7. Архитектурные проблемы — Сводная таблица

| ID | Критичность | Описание | Строки |
|----|-------------|----------|--------|
| REBOOT-01 | 🔴 КРИТИЧНО | `shouldReboot` не `volatile` | L47 |
| FSM-01 | 🔴 КРИТИЧНО | `STATE_ERROR` не используется | — |
| DISP-01 | 🔴 КРИТИЧНО | Дисплей заморожен в STATE_THINKING | L595 |
| FSM-04 | 🟡 СРЕДНЕ | `switchToTX()` вызывается для STATE_THINKING | L335-337 |
| WIFI-01 | 🟡 СРЕДНЕ | WiFi мониторинг только в STATE_IDLE | L532 |
| WIFI-02 | 🟡 СРЕДНЕ | `setAutoReconnect` не сбрасывается в AP-режиме | L390 |
| INIT-02 | 🟡 СРЕДНЕ | PSRAM fail не переводит в STATE_ERROR | L489 |
| DISP-02 | 🟡 СРЕДНЕ | Редрав в SPEAKING зависит от реализации TTS | L597 |
| FSM-02 | 🟢 НИЗКО | STATE_BOOT устанавливается не через setState() | L42 |
| FSM-03 | 🟢 НИЗКО | loop() не имеет редрава для THINKING/SPEAKING | L523 |
| INIT-01 | 🟢 НИЗКО | SD fail не логирует невозможность записи | L457 |
| FSM-05 | ℹ️ ИНФО | switchToRX() timeout=0 — корректно, документировать | L433 |

---

## 8. Конкретные Исправления

### Исправление 1: `volatile shouldReboot` [REBOOT-01]

```cpp
// main.cpp, строка 47
// БЫЛО:
bool shouldReboot = false;

// СТАЛО:
volatile bool shouldReboot = false;
```

### Исправление 2: Использовать `WiFi.status()` вместо `wifiConnected` [WIFI-01]

```cpp
// main.cpp, строки 593, 615
// БЫЛО:
if (path.length() > 0 && wifiConnected) {

// СТАЛО:
if (path.length() > 0 && WiFi.status() == WL_CONNECTED) {
```

### Исправление 3: `startAPMode()` — сброс auto-reconnect [WIFI-02]

```cpp
void startAPMode() {
    WiFi.setAutoReconnect(false);  // ← добавить
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Melvin-Setup", "melvin123");
    // ...
}
```

### Исправление 4: STATE_ERROR при провале PSRAM [INIT-02, FSM-01]

```cpp
// main.cpp, строки 489-491
if (!recorder.begin()) {
    Serial.println("[SETUP] FATAL: Recorder PSRAM alloc failed!");
    setState(STATE_ERROR);
    // Мигание лица ошибки в loop() или бесконечный halt
    while (true) {
        drawFace(canvas, STATE_ERROR, millis() / 500);
        canvas.pushSprite(0, 0);
        delay(500);
    }
}
```

### Исправление 5: Уточнение setState() для STATE_THINKING [FSM-04]

```cpp
void setState(RobotState s) {
    if (currentState == s) return;
    
    if (s == STATE_IDLE || s == STATE_RECORDING) {
        switchToRX();
    } else if (s == STATE_SPEAKING) {
        switchToTX();
    }
    // STATE_THINKING, STATE_CONNECTING, STATE_CONFIG_AP — I2S не переключаем
    
    // ...остальной код...
}
```

### Исправление 6: Редрав-callback для STATE_THINKING [DISP-01]

```cpp
// Добавить тип callback в main.cpp:
typedef void (*DisplayCallback)(RobotState state, uint32_t tick);

// Передавать в Agent::askAI():
String answer = agent.askAI(path, configMgr.config, [](RobotState, uint32_t) {
    uint32_t now = millis();
    if (now - lastRedrawMs >= REDRAW_MS) {
        animTick++;
        drawFace(canvas, STATE_THINKING, animTick);
        canvas.pushSprite(0, 0);
        lastRedrawMs = now;
    }
});
```

---

## 9. Config.h — Анализ

### 9.1 Структура

- Корректная JSON-сериализация через ArduinoJson
- Дефолты в конструкторе `MelvinConfig()` — правильный подход
- `getEffectivePrompt()` — хорошая инкапсуляция логики промптов

### 9.2 Проблемы

**ПРЕДУПРЕЖДЕНИЕ [CFG-01]: WebUI не отображает `tts_voice`, `rss_url`, `llm_provider`**
- В `WebUI.h` нет полей для `tts_voice`, `rss_url`, `llm_provider`, `groq_keys`, `openrouter_keys`
- Поля сохраняются в конфиг через `/api/save`, только если присланы в JSON
- Если WebUI не отправляет поле — оно перезаписывается дефолтным значением через оператор `|`
- **Решение:** Добавить поля в WebUI или изменить логику `/api/save` чтобы не перезаписывать незначащие поля

**ПРЕДУПРЕЖДЕНИЕ [CFG-02]: `wifi_pass` никогда не возвращается в `/api/config`**
```cpp
// server.on("/api/config"):
// doc["wifi_pass"] — отсутствует в ответе ✅ (правильно, из соображений безопасности)
// НО: WebUI не может проверить, установлен ли пароль
```
- **Статус:** Приемлемо с точки зрения безопасности

---

## 10. WebUI.h — Анализ

### 10.1 Обнаруженные проблемы

**ПРЕДУПРЕЖДЕНИЕ [UI-01]: Заголовок версии несоответствует**
```html
<h2>Melvin v7.0 Setup</h2>
```
- Указана версия "v7.0" — необходимо синхронизировать с реальной версией прошивки

**ПРЕДУПРЕЖДЕНИЕ [UI-02]: Кнопка SAVE не блокируется после нажатия**
- Пользователь может нажать SAVE несколько раз, отправив дублирующие запросы
- **Решение:**
```javascript
function save() {
    document.querySelector('button').disabled = true;  // добавить
    // ...
}
```

**ИНФО [UI-03]: `load()` не сохраняет `tts_voice` из API**
```javascript
// load():
document.getElementById('personality').value = data.personality || 'rick';
// data.tts_voice — игнорируется, нет поля в UI
```

---

## 11. Итоговая Оценка Архитектуры

```
┌─────────────────────────────────────────────────────────┐
│                  ОЦЕНКА СИСТЕМЫ                         │
├─────────────────────────┬───────────────────────────────┤
│ Компонент               │ Оценка                        │
├─────────────────────────┼───────────────────────────────┤
│ FSM структура           │ ✅ Хорошая, 7 состояний       │
│ Порядок инициализации   │ ✅ Корректный, все правила    │
│ PA_CTRL управление      │ ✅ LOW на boot, HIGH в playWav│
│ I2S Full-Duplex         │ ✅ Элегантное решение         │
│ WAV Header Parser       │ ✅ Надёжный, без magic offset │
│ VAD интеграция          │ ✅ VAD_MODE_3, 30мс окна      │
│ PSRAM для буферов       │ ✅ heap_caps_malloc SPIRAM     │
│ shouldReboot safety     │ ⚠️  volatile отсутствует      │
│ WiFi reconnect          │ ⚠️  Мониторинг неполный       │
│ Display в THINKING      │ ❌ Заморожен                  │
│ STATE_ERROR             │ ❌ Определён, не используется │
│ WebUI полнота           │ ⚠️  Не все поля config        │
└─────────────────────────┴───────────────────────────────┘

ИТОГ: Архитектура работоспособна и профессионально выстроена.
Критические исправления: 3 (volatile, STATE_ERROR, DISP freeze)
Рекомендуемые улучшения: 6
```

---

*Отчёт сгенерирован: 2026-06-05 | Версия анализа: 1.0*
