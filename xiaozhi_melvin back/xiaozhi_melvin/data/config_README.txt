=== МЭЛВИН v3.0 — ИНСТРУКЦИЯ config.txt ===

Положи этот файл (config.txt) в КОРЕНЬ SD-карты.
Формат: каждое значение на ОТДЕЛЬНОЙ строке, без пробелов.

--- СТРОКИ ---
Строка 1: SSID вашей Wi-Fi сети
Строка 2: Пароль Wi-Fi
Строка 3: Hugging Face API Key — получить на https://huggingface.co/settings/tokens
           Нужен токен типа "hf_..."
           Используется для: распознавание речи (Whisper large-v3-turbo)
Строка 4: OpenRouter API Key — получить на https://openrouter.ai/keys
           Нужен токен типа "sk-or-..."
           Используется для: ответы ИИ (Llama 3.1 8B Instruct, бесплатно)
Строка 5+: RSS-ленты новостей (по одной на строку, полный URL)
           Примеры:
             https://feeds.bbcrussian.com/russian/russia
             https://lenta.ru/rss/news
             https://rss.nytimes.com/services/xml/rss/nyt/World.xml

--- УПРАВЛЕНИЕ КНОПКОЙ BOOT ---
Удержать (>0.4 сек) : Запись голоса → Whisper STT → Рик Санчес отвечает → TTS
1 клик               : Случайная фраза от Рика Санчеса
2 клика              : Читает RSS-ленты и пересказывает новости

--- TTS (синтез речи) ---
Google Translate TTS — бесплатно, ключ НЕ нужен.
Встроен в библиотеку ESP32-audioI2S.

--- ПРИМЕР config.txt ---
MyHomeWifi
password123
hf_xxxxxxxxxxxxxxxxxxxxxxxxxxxx
sk-or-v1-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
https://lenta.ru/rss/news
https://feeds.bbcrussian.com/russian/russia
