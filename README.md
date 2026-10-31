# Melvin AI & ESP32 Reverse Engineering Workspace

Комплексный проект автономного бортового ИИ-агента **Melvin** (запущенного 100% локально на смартфоне Xiaomi Redmi Note 4 / MediaTek Helio X20) и инструментария реверс-инжиниринга прошивок ESP32.

---

## ⚡ Melvin AI: Автономный бортовой ИИ (On-Device Rick Sanchez)

Автономный голосовой агент, работающий без интернета прямо на мобильном процессоре Helio X20 MT6797:
- **LLM Core**: `Qwen2.5-0.5B-Instruct` (Q4_K_M GGUF) на базе `llama-server` (Alpine Linux PRoot).
- **Характер**: Аутентичный Рик Санчез (каноничный дубляж Сыендука) через Assistant Prefill.
- **Скорость отклика**: **3.5 – 6.5 секунд** на реальном мобильном CPU (в 6.5 раз быстрее базового R1).
- **Долгосрочная память**: SQLite FTS5 RAG-хранилище диалогов и фактов.
- **Аппаратные функции (`phone_tools.py`)**: Прямое чтение аккумулятора (`!bat`), температуры CPU (`!sys`), управление вспышкой (`!torch`).
- **Android App (`melvin_android_app`)**: Нативное приложение WebView с голосовым вводом через SpeechRecognizer и синтезом речи с модуляцией тембра Рика (pitch 0.85, rate 1.15).

Полная статья и руководство по сборке: [`AUTONOMOUS_AI_ON_OLD_SMARTPHONE_GUIDE.md`](AUTONOMOUS_AI_ON_OLD_SMARTPHONE_GUIDE.md).

---

## 🛠 Локальное окружение Ghidra + GhidraMCP + ESP32 Flash Loader

Компоненты анализа прошивок ESP32:

1. **OpenJDK 21 (Temurin HotSpot)**:
   - Путь: `D:\esp32\jdk\jdk-21.0.13+9\`
   - Требуется для работы современной Ghidra 12+.

2. **Ghidra 12.1.4 (NSA SRE)**:
   - Путь: `D:\esp32\ghidra\ghidra_12.1.4_PUBLIC\`
   - Запуск: скрипт `start-ghidra.bat` (автоматически проставляет `JAVA_HOME`).

3. **Плагины Ghidra (в `Ghidra\Extensions\`):**
   - **GhidraMCP (v2.0)**: HTTP/MCP backend для взаимодействия ИИ-агентов с Ghidra.
   - **ESP32 Flash Loader**: загрузчик дампов Flash-памяти и образов прошивок ESP32/ESP8266 с авторазметкой разделов памяти (IRAM, DRAM, Flash Cache, DROM, IROM) и SVD-картами регистров для чипов ESP32, ESP32-S2, ESP32-S3, ESP32-C2/C3/C6, ESP32-H2, ESP32-P4.

4. **Python MCP Bridge (v2.0.0)**:
   - Окружение: `D:\esp32\.venv\`
   - Исполняемый файл: `D:\esp32\.venv\Scripts\ghidra-mcp.exe`
   - Скрипт запуска: `run-ghidra-mcp.bat`
   - Конфиг для MCP-клиентов: `mcp-settings.json`

---

## Как использовать:

### 1. Запуск Ghidra
Дважды кликните или запустите в терминале:
```bat
D:\esp32\start-ghidra.bat
```

При первом запуске:
- В окне Ghidra выберите **File -> Install Extensions...** и убедитесь, что стоят галочки напротив **GhidraMCP** и **ESP32 Flash Loader**.
- Перезапустите Ghidra (при запросе).
- В меню инструмента (CodeBrowser): `File` -> `Configure` -> поставьте галочку у `GhidraMCPPlugin` (в разделе Developer).
- Плагин GhidraMCP запустит локальный HTTP-сервер на `http://127.0.0.1:8080/`.

### 2. Загрузка прошивки ESP32
- Перетащите файл прошивки (`.bin`, полный дамп flash памяти) в проект Ghidra.
- В выпадающем списке формата выберите **ESP32 Flash Image** (предоставляется установленным плагином).
- Плагин автоматически прочитает таблицу разделов и разложит секции по правильным адресам MMU.

### 3. Подключение к AI-агенту через MCP
В настройках вашего MCP-клиента (Antigravity, Claude, Cursor, VS Code) укажите конфигурацию из файла `D:\esp32\mcp-settings.json`:

```json
{
  "mcpServers": {
    "ghidra-esp32": {
      "command": "D:\\esp32\\.venv\\Scripts\\ghidra-mcp.exe",
      "args": [
        "--ghidra-server",
        "http://127.0.0.1:8080/"
      ],
      "env": {
        "JAVA_HOME": "D:\\esp32\\jdk\\jdk-21.0.13+9",
        "GHIDRA_INSTALL_DIR": "D:\\esp32\\ghidra\\ghidra_12.1.4_PUBLIC"
      }
    }
  }
}
```
