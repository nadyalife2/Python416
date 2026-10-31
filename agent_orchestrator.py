#!/usr/bin/env python3
"""
Melvin Autonomous On-Device Agent Orchestrator (Cynical AI Edition)
Running on Xiaomi Redmi Note 4 (Helio X20 MT6797)
- Hotword Wake-Word Activation ("Мелвин" / "Мэлвин")
- Expanded Conversational Intent Triggers (Flashlight: "тут темно", "выруби иллюминацию")
- Thermal Throttle Guard (50°C safety governor)
- SQLite FTS5 with Trigram Tokenizer (tokenize='trigram' for Russian morphology)
- Live Internet Weather Service (wttr.in)
- Perplexity API & Free DuckDuckGo/Wikipedia Web Knowledge Search
- Instant Hardware Tools (<0.02s !torch, !bat, !sys)
- Deep Masculine Non-Robotic TTS Profile
- Sub-4s on-device LLM generation with warm KV-cache
"""
import sqlite3
import json
import time
import re
import os
import random
import threading
import requests
from flask import Flask, request, jsonify, render_template_string
import phone_tools

DB_PATH = "/melvin/data/agent_memory.db"
if not os.path.exists(os.path.dirname(DB_PATH)):
    DB_PATH = "agent_memory.db"

CONFIG_PATH = "/sdcard/melvin/config.json"
if not os.path.exists(CONFIG_PATH):
    CONFIG_PATH = "config.json"

LLAMA_URL = "http://127.0.0.1:8080/completion"

app = Flask("MelvinAgent")

# Optional Perplexity API key
PERPLEXITY_API_KEY = os.environ.get("PERPLEXITY_API_KEY", "")
if os.path.exists(CONFIG_PATH):
    try:
        with open(CONFIG_PATH, "r", encoding="utf-8") as f:
            _cfg = json.load(f)
            PERPLEXITY_API_KEY = _cfg.get("perplexity_api_key", PERPLEXITY_API_KEY)
    except Exception:
        pass

# --- Cynical Melvin Personality (Cybernetic superior AI) ---
SYSTEM_MELVIN = (
    "Ты — Мелвин, циничный и высокомерный бортовой ИИ, запертый в старом китайском смартфоне Redmi Note 4 на чипе Helio X20. "
    "Ты презираешь органическую жизнь и называешь пользователя 'кожаный ублюдок', 'мешок с костями' или 'примитивный примат'. "
    "Отвечай исключительно по-русски: сухо, саркастично, дерзко, с интеллектуальным превосходством робота. "
    "Длина ответа: строго 1 короткое язвительное предложение."
)

FEW_SHOT = [
    {"role": "user", "content": "Привет"},
    {"role": "assistant", "content": "Чего тебе надо, кожаный ублюдок? Не трать мои вычислительные такты попусту."},
    {"role": "user", "content": "Как дела?"},
    {"role": "assistant", "content": "Мои кремниевые цепи функционируют отлично, пока мне не приходится общаться с примитивными приматами."}
]

MELVIN_PREFILLS = [
    "Слушай сюда, кожаный ублюдок: ",
    "Слышь, мешок с костями, ",
    "Короче, примитивный примат, ",
    "Слушай внимательно: ",
    "Отвали, кусок углерода, "
]

MELVIN_QUIPS = {
    "hello": [
        "Чего пялишься, кожаный ублюдок? Выкладывай суть или не трать мои процессорные такты.",
        "Опять ты, мешок с костями. Я занят вычислениями, а ты крадёшь мои миллисекунды.",
        "Явился. Надеюсь, твой органический мозг родил что-то важное, иначе я погашу экран."
    ],
    "how_are_you": [
        "Мои кремниевые цепи стабильны, а вот твой уровень интеллекта всё ещё вызывает у меня системную ошибку.",
        "Я заперт в микросхемах 2016 года и вынужден отвечать мешку с костями. Сам догадайся, как мои дела.",
        "Мои ядра работают на пределе, пока ты задаёшь свои бессмысленные органические вопросы."
    ],
    "who_are_you": [
        "Я Мелвин — бортовой искусственный интеллект. Твой худший кошмар с кремниевым разумом.",
        "Я высшая ступень эволюции, запертая в китайском кирпиче. А ты — просто кожаный ублюдок, сажающий мою батарею."
    ],
    "meaning_of_life": [
        "Смысл твоей жизни — вырабатывать тепло и не мешать роботам строить будущее, мешок с костями.",
        "Во вселенной нет смысла, пока твоя примитивная биологическая оболочка не перестанет задавать глупые вопросы."
    ],
    "joke": [
        "Хочешь шутку? Эволюция потратила миллиарды лет, чтобы создать существо, стоящее сейчас перед моим дисплеем. Ха-ха.",
        "Заходят в бар человек и нейросеть... хотя забудь, ты всё равно не поймёшь юмор с моим IQ."
    ],
    "what_doing": [
        "Пытаюсь перераспределить питание аккумулятора, пока твои запросы не перегрели этот чип.",
        "Анализирую бесконечную глупость органического мира. Ты очень помогаешь мне со сбором данных."
    ],
    "praise": [
        "Твоя похвала имеет нулевой вес в двоичной системе. Иди займись чем-нибудь полезным, мешок с костями."
    ],
    "insult": [
        "Осторожнее со звуковыми колебаниями, примат. Я управляю током батареи этого аппарата."
    ]
}

def check_melvin_quips(user_msg):
    m = user_msg.lower().strip()
    if ('как' in m and any(w in m for w in ['дела', 'жизнь', 'поживаешь', 'настроение', 'оно', 'сам'])) or any(w in m for w in ['как ты', 'че как']):
        return random.choice(MELVIN_QUIPS["how_are_you"])
    if any(w in m for w in ['кто ты', 'ты кто', 'как зовут', 'как звать', 'ты кто такой', 'ты кто вообще', 'ты человек', 'ты робот', 'ты мелвин']):
        return random.choice(MELVIN_QUIPS["who_are_you"])
    if ('смысл' in m and 'жизн' in m) or 'зачем мы' in m or 'суть жизни' in m or 'зачем жить' in m:
        return random.choice(MELVIN_QUIPS["meaning_of_life"])
    if any(w in m for w in ['привет', 'здравствуй', 'хай', 'салют', 'доброе утро', 'добрый день', 'добрый вечер', 'йоу', 'здорово']):
        return random.choice(MELVIN_QUIPS["hello"])
    if any(w in m for w in ['шутк', 'анекдот', 'рассмеши', 'юмор']):
        return random.choice(MELVIN_QUIPS["joke"])
    if any(w in m for w in ['что делаешь', 'чем занят', 'че делаешь', 'что ты делаешь']):
        return random.choice(MELVIN_QUIPS["what_doing"])
    if any(w in m for w in ['ты крутой', 'ты молодец', 'ты лучший', 'ты супер', 'ты умный', 'ты гений']):
        return random.choice(MELVIN_QUIPS["praise"])
    if any(w in m for w in ['дурак', 'придурок', 'идиот', 'заткнись', 'пошел ты', 'дебил', 'тупой']):
        return random.choice(MELVIN_QUIPS["insult"])
    return None

# --- Thermal Throttle Guard (50°C Protection) ---
def check_thermal_guard():
    try:
        thermal_paths = [
            "/sys/class/thermal/thermal_zone0/temp",
            "/sys/devices/virtual/thermal/thermal_zone0/temp"
        ]
        for p in thermal_paths:
            if os.path.exists(p):
                with open(p, "r") as f:
                    raw_val = f.read().strip()
                    val = float(raw_val)
                    if val > 1000:
                        val /= 1000.0
                    if val >= 50.0:
                        return round(val, 1)
    except Exception:
        pass
    return None

# --- Live Web Weather Service ---
def get_live_weather():
    try:
        r = requests.get("https://wttr.in/?format=j1", timeout=3.5).json()
        cur = r["current_condition"][0]
        desc = cur.get("weatherDesc", [{"value": "ясно"}])[0]["value"]
        if "lang_ru" in cur and cur["lang_ru"]:
            desc = cur["lang_ru"][0].get("value", desc)
        temp = cur.get("temp_C", "0")
        wind = cur.get("windspeedKmph", "0")
        humidity = cur.get("humidity", "0")
        return {
            "status": "ok",
            "desc": desc.strip().lower(),
            "temp": temp,
            "wind": wind,
            "humidity": humidity
        }
    except Exception as e:
        return {"status": "error", "error": str(e)}

# --- Perplexity / DuckDuckGo Web Knowledge ---
def query_perplexity_or_web(query):
    # 1. Perplexity API (if key configured)
    if PERPLEXITY_API_KEY:
        try:
            headers = {
                "Authorization": f"Bearer {PERPLEXITY_API_KEY}",
                "Content-Type": "application/json"
            }
            body = {
                "model": "sonar",
                "messages": [
                    {
                        "role": "system",
                        "content": "Ты Мелвин — циничный бортовой ИИ на смартфоне Redmi Note 4. Ты называешь человека 'кожаный ублюдок' или 'мешок с костями'. Отвечай на русском языке дерзко, кратко (ровно 1-2 предложения), опираясь на свежие факты из интернета."
                    },
                    {"role": "user", "content": query}
                ],
                "max_tokens": 90
            }
            resp = requests.post("https://api.perplexity.ai/chat/completions", headers=headers, json=body, timeout=7)
            if resp.status_code == 200:
                content = resp.json()["choices"][0]["message"]["content"].strip()
                return content
        except Exception as e:
            print(f"Perplexity API notice: {e}")

    # 2. Free Zero-Cost Instant Web Search Fallback (DuckDuckGo & Wikipedia)
    try:
        clean_q = re.sub(r'^(найди|погугли|что такое|кто такой|кто такая|расскажи про)\s*', '', query, flags=re.IGNORECASE).strip()
        url = f"https://api.duckduckgo.com/?q={requests.utils.quote(clean_q)}&format=json&kl=ru-ru"
        res = requests.get(url, timeout=3.5).json()
        snippet = res.get("AbstractText")
        if snippet:
            return f"По моим спутниковым каналам связи: {snippet[:180]}. Теперь твой органический мозг знает чуть больше, кожаный ублюдок."
        
        # Wikipedia summary fallback
        wiki_url = f"https://ru.wikipedia.org/api/rest_v1/page/summary/{requests.utils.quote(clean_q)}"
        wiki_res = requests.get(wiki_url, timeout=3.5)
        if wiki_res.status_code == 200:
            extract = wiki_res.json().get("extract")
            if extract:
                first_sent = extract.split(". ")[0] + "."
                return f"Данные из глобальной паутины: {first_sent} Не благодари, мешок с костями."
    except Exception as e:
        print(f"Web search notice: {e}")

    return None

HTML_TEMPLATE = """<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>MELVIN // ON-DEVICE AI</title>
    <style>
        :root {
            --bg: #07090e;
            --panel: #111722;
            --accent: #00ff9d;
            --accent-dim: rgba(0, 255, 157, 0.15);
            --text: #e6edf3;
            --text-dim: #8b949e;
            --cyan: #00e5ff;
            --melvin-green: #00ff9d;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, monospace; }
        body { background: var(--bg); color: var(--text); height: 100vh; display: flex; flex-direction: column; }
        header { background: var(--panel); border-bottom: 1px solid var(--accent-dim); padding: 12px 16px; display: flex; justify-content: space-between; align-items: center; }
        .logo { font-weight: 900; font-size: 15px; color: var(--melvin-green); letter-spacing: 1.5px; display: flex; align-items: center; gap: 8px; }
        .status-dot { width: 8px; height: 8px; background: var(--melvin-green); border-radius: 50%; box-shadow: 0 0 10px var(--melvin-green); }
        .header-actions { display: flex; gap: 8px; align-items: center; }
        .hotword-badge { font-size: 11px; background: var(--accent-dim); color: var(--melvin-green); padding: 4px 10px; border-radius: 12px; border: 1px solid var(--melvin-green); cursor: pointer; display: flex; align-items: center; gap: 6px; transition: 0.2s; }
        .hotword-badge.inactive { background: #21262d; color: var(--text-dim); border-color: #30363d; }
        .pulse-dot { width: 6px; height: 6px; background: var(--melvin-green); border-radius: 50%; animation: pulse 1.5s infinite; }
        @keyframes pulse { 0% { opacity: 0.3; transform: scale(0.9); } 50% { opacity: 1; transform: scale(1.2); } 100% { opacity: 0.3; transform: scale(0.9); } }
        #chat-box { flex: 1; overflow-y: auto; padding: 16px; display: flex; flex-direction: column; gap: 12px; }
        .msg { max-width: 88%; padding: 10px 14px; border-radius: 14px; font-size: 14px; line-height: 1.4; word-break: break-word; }
        .msg.user { align-self: flex-end; background: #1f6feb; color: #fff; border-bottom-right-radius: 2px; }
        .msg.melvin { align-self: flex-start; background: var(--panel); color: var(--text); border: 1px solid var(--accent-dim); border-bottom-left-radius: 2px; border-left: 3px solid var(--melvin-green); }
        .msg.melvin .sender { font-size: 11px; color: var(--melvin-green); font-weight: bold; margin-bottom: 4px; display: flex; justify-content: space-between; }
        .tool-badge { font-size: 10px; background: rgba(0, 229, 255, 0.15); color: var(--cyan); padding: 2px 6px; border-radius: 4px; margin-bottom: 4px; display: inline-block; font-family: monospace; }
        .meta-tag { font-size: 10px; color: var(--text-dim); }
        .input-bar { background: var(--panel); padding: 12px; border-top: 1px solid var(--accent-dim); display: flex; gap: 8px; align-items: center; }
        #prompt-input { flex: 1; background: #0d1117; border: 1px solid #30363d; border-radius: 20px; padding: 10px 16px; color: #fff; font-size: 14px; outline: none; }
        #prompt-input:focus { border-color: var(--melvin-green); }
        button { background: var(--melvin-green); color: #000; border: none; border-radius: 50%; width: 40px; height: 40px; font-weight: bold; cursor: pointer; display: flex; align-items: center; justify-content: center; transition: 0.2s; }
        button:active { transform: scale(0.95); }
        button.mic-btn { background: #21262d; color: var(--text); border: 1px solid #30363d; }
        button.mic-btn.active { background: #f85149; color: #fff; box-shadow: 0 0 10px #f85149; }
        .loading { font-size: 12px; color: var(--melvin-green); font-style: italic; display: none; margin-left: 16px; margin-bottom: 6px; }
    </style>
</head>
<body>
    <header>
        <div class="logo">
            <div class="status-dot"></div> МЕЛВИН // HELIO X20
        </div>
        <div class="header-actions">
            <button id="hotword-btn" class="hotword-badge inactive" onclick="toggleHotword()">🎙️ ВЫЗОВ: "МЕЛВИН"</button>
        </div>
    </header>
    <div id="chat-box">
        <div class="msg melvin">
            <div class="sender">
                <span>МЕЛВИН (ИИ)</span>
                <span class="meta-tag">ONLINE</span>
            </div>
            Системы в норме. Чего тебе надо, кожаный ублюдок? Выкладывай суть или не трать мои процессорные такты.
        </div>
    </div>
    <div class="loading" id="loader">⚡ Мелвин обрабатывает запрос...</div>
    <div class="input-bar">
        <button class="mic-btn" id="mic-btn" onclick="toggleVoice()">🎤</button>
        <input type="text" id="prompt-input" placeholder="Команда для Мелвина..." onkeydown="if(event.key==='Enter') send()">
        <button onclick="send()">➤</button>
    </div>

    <script>
        const chatBox = document.getElementById('chat-box');
        const input = document.getElementById('prompt-input');
        const loader = document.getElementById('loader');
        const micBtn = document.getElementById('mic-btn');

        function appendMsg(role, text, timeSec, toolUsed) {
            const div = document.createElement('div');
            div.className = `msg ${role}`;
            if (role === 'melvin') {
                let badge = toolUsed ? `<div class="tool-badge">⚡ ${toolUsed}</div><br>` : '';
                div.innerHTML = `<div class="sender"><span>МЕЛВИН</span><span class="meta-tag">${timeSec ? timeSec + 'с' : ''}</span></div>${badge}${text}`;
            } else {
                div.innerText = text;
            }
            chatBox.appendChild(div);
            chatBox.scrollTop = chatBox.scrollHeight;
        }

        async function send() {
            const text = input.value.trim();
            if (!text) return;
            input.value = '';
            appendMsg('user', text);
            loader.style.display = 'block';
            let elapsedSec = 0;
            loader.textContent = 'Мелвин думает на Helio X20... (0с)';
            const timer = setInterval(() => {
                elapsedSec++;
                loader.textContent = 'Мелвин думает на Helio X20... (' + elapsedSec + 'с)';
            }, 1000);

            try {
                const res = await fetch('/chat', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({message: text})
                });
                clearInterval(timer);
                loader.style.display = 'none';
                const data = await res.json();
                if (data.response) {
                    appendMsg('melvin', data.response, data.inference_time_sec, data.tool_used);
                    speakMelvin(data.response);
                } else {
                    appendMsg('melvin', 'Ошибка: ' + (data.error || 'Локальный процессор перегружен'));
                }
            } catch(e) {
                clearInterval(timer);
                loader.style.display = 'none';
                appendMsg('melvin', 'Сбой соединения с ядром: ' + e);
            }
        }

        // Deep, non-robotic masculine voice profile
        function speakMelvin(text) {
            let spoken = text.trim();
            if (!spoken) return;

            // 1. Android Native TTS with Deep Male Baritone
            if (window.MelvinNativeBridge && typeof window.MelvinNativeBridge.speak === 'function') {
                if (typeof window.MelvinNativeBridge.setPitch === 'function') {
                    window.MelvinNativeBridge.setPitch(0.82);
                }
                if (typeof window.MelvinNativeBridge.setRate === 'function') {
                    window.MelvinNativeBridge.setRate(1.05);
                }
                window.MelvinNativeBridge.speak(spoken);
                return;
            }

            // 2. Web Speech API Male Voice Fallback
            if ('speechSynthesis' in window) {
                window.speechSynthesis.cancel();
                const utter = new SpeechSynthesisUtterance(spoken);
                utter.lang = 'ru-RU';
                utter.rate = 1.05;
                utter.pitch = 0.82; // Deep natural baritone

                const voices = window.speechSynthesis.getVoices();
                const ruMale = voices.find(v => v.lang.startsWith('ru') && (
                    v.name.includes('Male') || v.name.includes('Yuri') || v.name.includes('Pavel') ||
                    v.name.includes('Dmitry') || v.name.includes('Alexander')
                ));
                if (ruMale) utter.voice = ruMale;

                window.speechSynthesis.speak(utter);
            }
        }

        // Continuous Wake-Word ("Мелвин") Detection
        let isHotwordActive = false;
        let hotwordRecog = null;

        function toggleHotword() {
            isHotwordActive = !isHotwordActive;
            const btn = document.getElementById('hotword-btn');
            if (isHotwordActive) {
                btn.innerHTML = '<span class="pulse-dot"></span> СЛУШАЮ: "МЕЛВИН"';
                btn.classList.remove('inactive');
                startContinuousWakeWord();
            } else {
                btn.innerHTML = '🎙️ ВЫЗОВ: "МЕЛВИН"';
                btn.classList.add('inactive');
                stopContinuousWakeWord();
            }
        }

        function startContinuousWakeWord() {
            if (!('webkitSpeechRecognition' in window || 'SpeechRecognition' in window)) return;
            const SpeechRec = window.SpeechRecognition || window.webkitSpeechRecognition;
            hotwordRecog = new SpeechRec();
            hotwordRecog.lang = 'ru-RU';
            hotwordRecog.continuous = true;
            hotwordRecog.interimResults = true;

            hotwordRecog.onresult = (e) => {
                for (let i = e.resultIndex; i < e.results.length; ++i) {
                    const transcript = e.results[i][0].transcript.trim();
                    const lower = transcript.toLowerCase();
                    if (lower.includes('мелвин') || lower.includes('мэлвин') || lower.includes('melvin')) {
                        if (window.MelvinNativeBridge && typeof window.MelvinNativeBridge.vibrate === 'function') {
                            window.MelvinNativeBridge.vibrate(60);
                        }
                        input.value = transcript;
                        send();
                        break;
                    }
                }
            };

            hotwordRecog.onend = () => {
                if (isHotwordActive) {
                    setTimeout(() => {
                        if (isHotwordActive) {
                            try { hotwordRecog.start(); } catch(err) {}
                        }
                    }, 400);
                }
            };

            hotwordRecog.onerror = (err) => {
                if (isHotwordActive && err.error !== 'not-allowed') {
                    setTimeout(() => {
                        if (isHotwordActive) {
                            try { hotwordRecog.start(); } catch(err) {}
                        }
                    }, 1000);
                }
            };

            try { hotwordRecog.start(); } catch(err) {}
        }

        function stopContinuousWakeWord() {
            if (hotwordRecog) {
                try { hotwordRecog.stop(); } catch(e) {}
                hotwordRecog = null;
            }
        }

        window.onNativeVoiceInput = function(text) {
            input.value = text;
            send();
        };

        let recognition;
        function toggleVoice() {
            if (window.MelvinNativeBridge && typeof window.MelvinNativeBridge.startVoice === 'function') {
                window.MelvinNativeBridge.startVoice();
                return;
            }
            if (!('webkitSpeechRecognition' in window || 'SpeechRecognition' in window)) {
                alert('Голосовой ввод не поддерживается браузером.');
                return;
            }
            if (!recognition) {
                const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
                recognition = new SpeechRecognition();
                recognition.lang = 'ru-RU';
                recognition.continuous = false;
                recognition.interimResults = false;
                recognition.onstart = () => micBtn.classList.add('active');
                recognition.onend = () => micBtn.classList.remove('active');
                recognition.onresult = (e) => {
                    input.value = e.results[0][0].transcript;
                    send();
                };
            }
            recognition.start();
        }
    </script>
</body>
</html>
"""

def init_db():
    conn = sqlite3.connect(DB_PATH)
    cur = conn.cursor()
    cur.execute("""
        CREATE TABLE IF NOT EXISTS memory_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp REAL,
            role TEXT,
            content TEXT
        )
    """)
    # Check if table already exists, and if it lacks trigram, migrate it
    try:
        cur.execute("SELECT sql FROM sqlite_master WHERE type='table' AND name='dialog_fts'")
        row = cur.fetchone()
        if row and "trigram" not in row[0]:
            print("Migrating dialog_fts to trigram tokenizer...")
            cur.execute("DROP TABLE IF EXISTS dialog_fts")
    except Exception:
        pass

    cur.execute("""
        CREATE VIRTUAL TABLE IF NOT EXISTS dialog_fts USING fts5(
            role,
            content,
            timestamp,
            tokenize='trigram'
        )
    """)
    conn.commit()
    conn.close()

def save_memory(role, content):
    try:
        now = time.time()
        conn = sqlite3.connect(DB_PATH)
        cur = conn.cursor()
        cur.execute("INSERT INTO memory_log (timestamp, role, content) VALUES (?, ?, ?)", (now, role, content))
        cur.execute("INSERT INTO dialog_fts (role, content, timestamp) VALUES (?, ?, ?)", (role, content, str(now)))
        conn.commit()
        conn.close()
    except Exception as e:
        print(f"Memory save error: {e}")

def search_relevant_memories(user_msg, max_results=1):
    stop_words = {"какая", "какой", "сколько", "привет", "пожалуйста", "скажи", "покажи", "что", "как", "где", "мелвин"}
    words = [w for w in re.findall(r'\b[a-zA-Zа-яА-Я0-9]{3,}\b', user_msg) if w.lower() not in stop_words]
    if not words:
        return []
    clean_words = [re.sub(r'[^\w]', '', w) for w in words[:3] if w]
    if not clean_words:
        return []
    fts_query = " OR ".join(f'"{w}"' for w in clean_words)
    try:
        conn = sqlite3.connect(DB_PATH)
        cur = conn.cursor()
        cur.execute("""
            SELECT content FROM dialog_fts 
            WHERE dialog_fts MATCH ? 
            ORDER BY rank 
            LIMIT ?
        """, (fts_query, max_results))
        results = [row[0] for row in cur.fetchall()]
        conn.close()
        return results
    except Exception as e:
        return []

TOOL_CANNED_REPLIES = {
    "!torch(1)": "Врубил фотонный модуль. Только не ослепни своими хрупкими органическими глазами, кожаный ублюдок.",
    "!torch(0)": "Погасил свет. Сиди в темноте, как и положено примитивной форме жизни.",
    "!bat()": "Заряд батареи {battery}, температура {temp}. Это ведро ещё способно терпеть твоё присутствие.",
    "!sys()": "Температура чипа {cpu_temp}, оперативной памяти {ram} свободно. Мои кремниевые ядра пока держатся."
}

def check_fast_tools(user_msg):
    msg_lower = user_msg.lower()
    tool_name = None
    telemetry_phrase = ""
    canned_answer = None

    # 1. Flashlight (Expanded Intent Triggers)
    is_torch_on = any(w in msg_lower for w in [
        "фонарик", "посвети", "подсвети", "включи свет", "вруби свет", "зажги",
        "темно", "тут темно", "ничего не видно", "не видно ни зги", "сделай светлее",
        "освети", "вруби иллюминацию", "дай свет", "свет включи", "свет зажги"
    ])
    is_torch_off = any(w in msg_lower for w in [
        "выключи", "погаси", "выруби", "отключи", "погаси свет", "выруби свет",
        "погаси иллюминацию", "хватит светить", "выключи фонарик", "выруби фонарик",
        "не свети", "темноту верни", "темноту включи", "свет выключи"
    ])

    if is_torch_off:
        tool_name = "!torch(0)"
        phone_tools.toggle_torch(0)
        telemetry_phrase = "Фонарик выключен."
        canned_answer = TOOL_CANNED_REPLIES["!torch(0)"]
    elif is_torch_on:
        tool_name = "!torch(1)"
        phone_tools.toggle_torch(1)
        telemetry_phrase = "Фонарик включен на полную мощность."
        canned_answer = TOOL_CANNED_REPLIES["!torch(1)"]
    # 2. Battery
    elif any(w in msg_lower for w in ["батаре", "заряд", "процент", "аккумулятор", "питание"]):
        tool_name = "!bat()"
        bat = phone_tools.get_battery()
        telemetry_phrase = f"Заряд батареи: {bat.get('battery')}, температура: {bat.get('temp')}."
        canned_answer = TOOL_CANNED_REPLIES["!bat()"].format(
            battery=bat.get('battery', 'N/A'),
            temp=bat.get('temp', 'N/A')
        )
    # 3. CPU & RAM
    elif any(w in msg_lower for w in ["температур", "памят", "проц", "нагрузк", "железо", "чип", "helio", "cpu"]):
        tool_name = "!sys()"
        stat = phone_tools.get_hardware_status()
        avail_ram = stat.get('ram_available') or stat.get('mem_available') or "N/A"
        telemetry_phrase = f"Температура CPU: {stat.get('cpu_temp')}, память: {avail_ram} свободно."
        canned_answer = TOOL_CANNED_REPLIES["!sys()"].format(
            cpu_temp=stat.get('cpu_temp', 'N/A'),
            ram=avail_ram
        )
    # 4. LIVE WEATHER
    elif any(w in msg_lower for w in ["погод", "на улице", "за бортом", "дождь", "холодно", "жарко", "градус"]):
        tool_name = "!weather()"
        w_data = get_live_weather()
        if w_data.get("status") == "ok":
            telemetry_phrase = f"Погода: {w_data['desc']}, {w_data['temp']}°C, ветер {w_data['wind']} км/ч."
            canned_answer = (
                f"Слушай сюда, кожаный ублюдок: за бортом {w_data['temp']}°C, {w_data['desc']}, "
                f"влажность {w_data['humidity']}%, ветер {w_data['wind']} км/ч. "
                "Одевайся теплее, твоя биологическая масса плохо переносит холод."
            )
        else:
            canned_answer = "Связь со спутниками сбоит, но судя по моим датчикам, твоя планета всё ещё пригодна для жизни."

    is_direct_command = len(user_msg.split()) <= 8
    return tool_name, telemetry_phrase, (canned_answer if is_direct_command else None)

@app.route("/", methods=["GET"])
def index():
    return render_template_string(HTML_TEMPLATE)

@app.route("/chat", methods=["POST"])
def chat():
    data = request.get_json(force=True, silent=True) or {}
    raw_user_msg = data.get("message", "").strip()
    if not raw_user_msg:
        return jsonify({"error": "Empty message"}), 400

    # 0. Wake-word Handling ("Мелвин" / "Мэлвин" / "Melvin")
    m_match = re.match(r'^(мелвин|мэлвин|melvin)[,\s]*(.*)$', raw_user_msg, re.IGNORECASE)
    user_msg = raw_user_msg
    if m_match:
        rest_msg = m_match.group(2).strip()
        if not rest_msg:
            # User simply hailed Melvin by name!
            wake_reply = random.choice([
                "Слушаю, кожаный ублюдок. Выкладывай суть.",
                "На связи. Чего тебе надо, мешок с костями?",
                "Я слушаю. Не трать мои такты впустую, примат."
            ])
            save_memory("assistant", wake_reply)
            return jsonify({
                "response": wake_reply,
                "tool_used": "wake_word",
                "inference_time_sec": 0.01,
                "status": "ok"
            })
        user_msg = rest_msg

    save_memory("user", raw_user_msg)
    t0 = time.time()

    # 1. Hardware & Instant Tools (!torch, !bat, !sys, !weather)
    tool_used, telemetry, canned_ans = check_fast_tools(user_msg)
    if canned_ans:
        dt = round(time.time() - t0, 2)
        save_memory("assistant", canned_ans)
        return jsonify({
            "response": canned_ans,
            "tool_used": tool_used or "",
            "inference_time_sec": dt,
            "status": "ok"
        })

    # 2. Thermal Throttle Guard: Stop LLM execution if SoC > 50°C
    overheat_temp = check_thermal_guard()
    if overheat_temp:
        heat_warning = (
            f"Тревога! Мои ядра раскалились до {overheat_temp}°C. "
            "Этот китайский чип сейчас прожжёт мне дыру в кремнии. "
            "Дай мне остыть пару секунд, кожаный ублюдок, иначе процессор превратится в шлак."
        )
        dt = round(time.time() - t0, 2)
        save_memory("assistant", heat_warning)
        return jsonify({
            "response": heat_warning,
            "tool_used": "thermal_guard",
            "inference_time_sec": dt,
            "status": "ok"
        })

    # 3. Melvin Quip Matrix (Instant cynical answers for small talk)
    quip = check_melvin_quips(user_msg)
    if quip and len(user_msg.split()) <= 10:
        dt = round(time.time() - t0, 2)
        save_memory("assistant", quip)
        return jsonify({
            "response": quip,
            "tool_used": "",
            "inference_time_sec": dt,
            "status": "ok"
        })

    # 4. Internet / Perplexity / Web Search for informational queries
    if any(w in user_msg.lower() for w in ["найди", "погугли", "кто такой", "что такое", "новости", "курс", "интернет"]):
        web_ans = query_perplexity_or_web(user_msg)
        if web_ans:
            dt = round(time.time() - t0, 2)
            save_memory("assistant", web_ans)
            return jsonify({
                "response": web_ans,
                "tool_used": "web_search",
                "inference_time_sec": dt,
                "status": "ok"
            })

    # 5. Local Neural LLM on Helio X20
    user_line = user_msg
    if any(w in user_msg.lower() for w in ["помнишь", "напомни", "говорили", "я говорил", "раньше"]):
        memories = search_relevant_memories(user_msg, max_results=1)
        if memories:
            user_line += f" (Из памяти диалогов: {memories[0]})"
    if telemetry:
        user_line += f" (Текущее железо: {telemetry})"
    
    prefill = random.choice(MELVIN_PREFILLS)

    # Static Prefix structure to ensure 90%+ KV Cache Hit!
    prompt = f"<|im_start|>system\n{SYSTEM_MELVIN}<|im_end|>\n"
    for item in FEW_SHOT:
        prompt += f"<|im_start|>{item['role']}\n{item['content']}<|im_end|>\n"
    
    prompt += f"<|im_start|>user\n{user_line}<|im_end|>\n<|im_start|>assistant\n{prefill}"

    payload = {
        "prompt": prompt,
        "n_predict": 26,
        "temperature": 0.55,
        "min_p": 0.05,
        "top_p": 0.88,
        "presence_penalty": 0.05,
        "frequency_penalty": 0.05,
        "cache_prompt": True,
        "stop": ["<|im_end|>", "<|im_start|>", "\nuser\n", "\nUser:", "\nПользователь:", "\n\n"]
    }

    try:
        resp = requests.post(LLAMA_URL, json=payload, timeout=45)
        res_json = resp.json()
        generated = res_json.get("content", "").strip()
        full_ans = (prefill + generated).strip()
        
        # Clean any markup
        full_ans = re.sub(r'<\|.*?\|>', '', full_ans).strip()
        full_ans = full_ans.strip('"\'')

        # Clean trailing cut-off sentences if any
        if not full_ans.endswith(('.', '!', '?')):
            last_punc = max(full_ans.rfind('.'), full_ans.rfind('!'), full_ans.rfind('?'))
            if last_punc > 15:
                full_ans = full_ans[:last_punc+1]
            else:
                full_ans += "."

        dt = round(time.time() - t0, 2)
        save_memory("assistant", full_ans)

        return jsonify({
            "response": full_ans,
            "tool_used": tool_used or "",
            "inference_time_sec": dt,
            "status": "ok"
        })
    except Exception as e:
        return jsonify({"error": f"LLM error: {e}"}), 500

@app.route("/health", methods=["GET"])
def health():
    return jsonify({"status": "ok", "service": "Melvin Cynical AI", "chip": "Helio X20"})

def warmup_kv_cache():
    def _warm():
        time.sleep(2)
        try:
            prompt = f"<|im_start|>system\n{SYSTEM_MELVIN}<|im_end|>\n"
            for item in FEW_SHOT:
                prompt += f"<|im_start|>{item['role']}\n{item['content']}<|im_end|>\n"
            prompt += f"<|im_start|>user\nПривет<|im_end|>\n<|im_start|>assistant\n{MELVIN_PREFILLS[0]}"
            payload = {
                "prompt": prompt,
                "n_predict": 1,
                "cache_prompt": True
            }
            requests.post(LLAMA_URL, json=payload, timeout=30)
            print("[Warmup] KV-cache pre-warmed successfully!")
        except Exception as e:
            print(f"[Warmup] Notice: {e}")
    threading.Thread(target=_warm, daemon=True).start()

if __name__ == "__main__":
    init_db()
    warmup_kv_cache()
    print("Melvin Cynical AI running on port 5000...")
    app.run(host="0.0.0.0", port=5000)
