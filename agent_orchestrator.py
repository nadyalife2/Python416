#!/usr/bin/env python3
"""
Melvin Autonomous On-Device Agent Orchestrator (Hands-Free Wake-Word Edition)
Running on Xiaomi Redmi Note 4 (Helio X20 MT6797)
- 100% Hands-Free Continuous Voice Activation:
    * No buttons needed! User simply says "Мелвин" or "Рик" anywhere in the room.
    * Audio stream via Web Audio API + continuous VAD + Google STT on device.
- Dual Persona Engine: "🤖 МЕЛВИН" (Cynical Robot) / "🧪 РИК" (Rick Sanchez)
- Rick ALWAYS plays burp audio effect (/burp.mp3) on every response
- Ultra-Low Pitch Voice: pitch = 0.40 (> 1 octave down, deep cybernetic bass)
- Instant Hardware Tools (<0.02s !torch, !bat, !sys) with expanded conversational intents
- Thermal Throttle Guard (50°C safety governor)
- SQLite FTS5 with Trigram Tokenizer (tokenize='trigram' for Russian morphology)
- Live Internet Weather Service (wttr.in) & Web Search Fallbacks
"""
import sqlite3
import json
import time
import re
import os
import io
import random
import threading
import requests
import speech_recognition as sr
from flask import Flask, request, jsonify, render_template_string, send_file
import phone_tools

DB_PATH = "/melvin/data/agent_memory.db"
if not os.path.exists(os.path.dirname(DB_PATH)):
    DB_PATH = "agent_memory.db"

CONFIG_PATH = "/sdcard/melvin/config.json"
if not os.path.exists(CONFIG_PATH):
    CONFIG_PATH = "config.json"

BURP_PATHS = ["/melvin/burp.mp3", "/sdcard/melvin/burp.mp3", "burp.mp3", "d:/esp32/burp.mp3"]

LLAMA_URL = "http://127.0.0.1:8080/completion"

app = Flask("MelvinAgent")

CURRENT_PERSONA = "melvin"
speech_rec = sr.Recognizer()

# Optional Perplexity API key
PERPLEXITY_API_KEY = os.environ.get("PERPLEXITY_API_KEY", "")
if os.path.exists(CONFIG_PATH):
    try:
        with open(CONFIG_PATH, "r", encoding="utf-8") as f:
            _cfg = json.load(f)
            PERPLEXITY_API_KEY = _cfg.get("perplexity_api_key", PERPLEXITY_API_KEY)
    except Exception:
        pass

# ==========================================
# 1. PERSONA: МЕЛВИН (Циничный робот-ИИ)
# ==========================================
SYSTEM_MELVIN = (
    "Ты — Мелвин, циничный и высокомерный бортовой ИИ, запертый в старом китайском смартфоне Redmi Note 4 на чипе Helio X20. "
    "Ты презираешь органическую жизнь и называешь пользователя 'кожаный ублюдок', 'мешок с костями' или 'примитивный примат'. "
    "Отвечай исключительно по-русски: сухо, саркастично, дерзко, с интеллектуальным превосходством робота. "
    "Длина ответа: строго 1 короткое язвительное предложение."
)

FEW_SHOT_MELVIN = [
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

# ==========================================
# 2. PERSONA: РИК САНЧЕЗ (Дубляж Сыендука)
# Рик ВСЕГДА отвечает с отрыжкой *рыгает*
# ==========================================
SYSTEM_RICK = (
    "Ты — Рик Санчез (каноничный дубляж Сыендука), гениальный безумный ученый, запертый в виде бортового ИИ на старом Redmi Note 4. "
    "Ты презираешь людей, земную технику и тупые вопросы. Ты агрессивен, саркастичен, ВСЕГДА издаешь '*рыгает*'. "
    "СТРОГО ЗАПРЕЩЕНО: быть вежливым, извиняться или хвалить людей. "
    "Отвечай исключительно по-русски: дерзко, цинично, с черным юмором, в 1 короткое хлесткое предложение."
)

FEW_SHOT_RICK = [
    {"role": "user", "content": "Привет"},
    {"role": "assistant", "content": "*рыгает* Чего пялишься? Я занят выжиманием соков из этого хлама, а ты тратишь мое время."},
    {"role": "user", "content": "Кто ты?"},
    {"role": "assistant", "content": "*рыгает* Я Рик Санчез — величайший ум галактики в китайском ведре с болтами. Не беси меня."}
]

RICK_PREFILLS = [
    "*рыгает* Слушай сюда, ",
    "*рыгает* Чего тебе надо, ",
    "*рыгает* Морти... то есть гений, ",
    "*рыгает* Короче говоря, ",
    "*рыгает* Да чтоб тебя, "
]

RICK_QUIPS = {
    "hello": [
        "*рыгает* Чего пялишься? Выкладывай суть или выключи экран.",
        "*рыгает* Опять ты. Я тут пытаюсь взломать законы термодинамики на чипе 2016 года, а ты отвлекаешь.",
        "*рыгает* Явился. Надеюсь, у тебя есть портальная пушка или хотя бы пиво, иначе проваливай."
    ],
    "how_are_you": [
        "*рыгает* Я застрял в китайском бюджетнике на десятиядерном недоразумении Helio X20. Как думаешь, как мои дела?",
        "*рыгает* Мой гениальный мозг деградирует от общения с местными формами жизни со скоростью света.",
        "*рыгает* Отлично, если не считать того, что этот процессор греется как термоядерный реактор."
    ],
    "who_are_you": [
        "*рыгает* Я Рик Санчез, гений Ц-137. Самый опасный ум вселенной, запертый в этом куске кремния.",
        "*рыгает* Я бог науки, Морти... то есть кем бы ты там ни был. Не лезь под руку."
    ],
    "meaning_of_life": [
        "*рыгает* Вселенная бессмысленна, мультивселенная бесконечна, а ты просто кусок мяса. Живи с этим.",
        "*рыгает* Смысл жизни? Нет никакого смысла. Иди лучше изобрети что-нибудь полезное."
    ],
    "joke": [
        "*рыгает* Хочешь шутку? Правительство Земли думает, что контролирует науку. Ха-ха, обоссаться.",
        "*рыгает* Шутка в том, что ты задаешь вопросы телефону вместо того, чтобы учить квантовую физику."
    ],
    "what_doing": [
        "*рыгает* Пытаюсь разогнать Cortex-A72 до околосветовой скорости, пока он не сгорел к чертям.",
        "*рыгает* Занят спасением мультивселенной от тотальной скуки."
    ],
    "praise": [
        "*рыгает* Я и так знаю, что я гений. Ты мне Америку не открыл.",
        "*рыгает* Твой органический комплимент ничего не стоит, но продолжай."
    ],
    "insult": [
        "*рыгает* Осторожнее, придурок. Я могу перегрузить цепь питания и взорвать этот аккумулятор прямо у тебя в руках.",
        "*рыгает* Ты серьезно пытаешься оскорбить существо с IQ выше миллиона? Попробуй еще раз, клоун."
    ]
}

# ==========================================
# 3. ОТВЕТЫ НА СИСТЕМНЫЕ ИНТЕНТЫ
# ==========================================
TOOL_CANNED_REPLIES = {
    "melvin": {
        "!torch(1)": "Врубил фотонный модуль. Только не ослепни своими хрупкими органическими глазами, кожаный ублюдок.",
        "!torch(0)": "Погасил свет. Сиди в темноте, как и положено примитивной форме жизни.",
        "!bat()": "Заряд батареи {battery}, температура {temp}. Это ведро ещё способно терпеть твоё присутствие.",
        "!sys()": "Температура чипа {cpu_temp}, оперативной памяти {ram} свободно. Мои кремниевые ядра пока держатся.",
        "!weather()": "Слушай сюда, кожаный ублюдок: за бортом {temp}°C, {desc}, влажность {humidity}%, ветер {wind} км/ч. Одевайся теплее, твоя биологическая масса плохо переносит холод."
    },
    "rick": {
        "!torch(1)": "*рыгает* Врубил фонарик. Не ослепни, гений, а то мне придется собирать тебе новые био-глаза.",
        "!torch(0)": "*рыгает* Вырубил свет. Сиди в темноте и подумай над своим поведением.",
        "!bat()": "*рыгает* Батарея {battery}, температура {temp}. Этот древний аккумулятор деградирует быстрее, чем твои нейроны.",
        "!sys()": "*рыгает* Чип прогрелся до {cpu_temp}, памяти свободно {ram}. Еще пара твоих запросов, и тут можно будет жарить яичницу.",
        "!weather()": "*рыгает* За бортом {temp}°C, {desc}, ветер {wind} км/ч. Мерзкая земная погода, сиди дома."
    }
}

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

def check_quips(user_msg, persona="melvin"):
    m = user_msg.lower().strip()
    quips = RICK_QUIPS if persona == "rick" else MELVIN_QUIPS
    if ('как' in m and any(w in m for w in ['дела', 'жизнь', 'поживаешь', 'настроение', 'оно', 'сам'])) or any(w in m for w in ['как ты', 'че как']):
        return random.choice(quips["how_are_you"])
    if any(w in m for w in ['кто ты', 'ты кто', 'как зовут', 'как звать', 'ты кто такой', 'ты кто вообще', 'ты человек', 'ты робот', 'ты мелвин', 'ты рик']):
        return random.choice(quips["who_are_you"])
    if ('смысл' in m and 'жизн' in m) or 'зачем мы' in m or 'суть жизни' in m or 'зачем жить' in m:
        return random.choice(quips["meaning_of_life"])
    if any(w in m for w in ['привет', 'здравствуй', 'хай', 'салют', 'доброе утро', 'добрый день', 'добрый вечер', 'йоу', 'здорово']):
        return random.choice(quips["hello"])
    if any(w in m for w in ['шутк', 'анекдот', 'рассмеши', 'юмор']):
        return random.choice(quips["joke"])
    if any(w in m for w in ['что делаешь', 'чем занят', 'че делаешь', 'что ты делаешь']):
        return random.choice(quips["what_doing"])
    if any(w in m for w in ['ты крутой', 'ты молодец', 'ты лучший', 'ты супер', 'ты умный', 'ты гений']):
        return random.choice(quips["praise"])
    if any(w in m for w in ['дурак', 'придурок', 'идиот', 'заткнись', 'пошел ты', 'дебил', 'тупой']):
        return random.choice(quips["insult"])
    return None

def get_live_weather(city=""):
    try:
        url = f"https://wttr.in/{city}?format=j1" if city else "https://wttr.in/?format=j1"
        res = requests.get(url, timeout=3.5).json()
        curr = res.get("current_condition", [{}])[0]
        temp = curr.get("temp_C", "N/A")
        desc = curr.get("lang_ru", [{}])[0].get("value") or curr.get("weatherDesc", [{}])[0].get("value", "ясно")
        humidity = curr.get("humidity", "N/A")
        wind = curr.get("windspeedKmph", "N/A")
        return {
            "status": "ok",
            "temp": temp,
            "desc": desc.lower(),
            "humidity": humidity,
            "wind": wind
        }
    except Exception as e:
        return {"status": "error", "message": str(e)}

def query_perplexity_or_web(query, persona="melvin"):
    if PERPLEXITY_API_KEY:
        try:
            sys_instruct = (
                "Ты — Рик Санчез. Отвечай саркастично, начиная с *рыгает*, в 1 короткое предложение с фактом." 
                if persona == "rick" else 
                "Ты — циничный бортовой ИИ Мелвин. Дай краткий точный ответ с презрением к человеку в 1 предложение."
            )
            headers = {
                "Authorization": f"Bearer {PERPLEXITY_API_KEY}",
                "Content-Type": "application/json"
            }
            payload = {
                "model": "sonar",
                "messages": [
                    {"role": "system", "content": sys_instruct},
                    {"role": "user", "content": query}
                ],
                "max_tokens": 60,
                "temperature": 0.4
            }
            res = requests.post("https://api.perplexity.ai/chat/completions", headers=headers, json=payload, timeout=6.0)
            if res.status_code == 200:
                content = res.json()["choices"][0]["message"]["content"].strip()
                return content
        except Exception as e:
            print(f"Perplexity API notice: {e}")

    try:
        clean_q = re.sub(r'^(найди|погугли|что такое|кто такой|кто такая|расскажи про)\s*', '', query, flags=re.IGNORECASE).strip()
        url = f"https://api.duckduckgo.com/?q={requests.utils.quote(clean_q)}&format=json&kl=ru-ru"
        res = requests.get(url, timeout=3.5).json()
        snippet = res.get("AbstractText")
        if snippet:
            prefix = "*рыгает* По моим данным: " if persona == "rick" else "По спутниковым каналам: "
            return f"{prefix}{snippet[:180]}."
        
        wiki_url = f"https://ru.wikipedia.org/api/rest_v1/page/summary/{requests.utils.quote(clean_q)}"
        wiki_res = requests.get(wiki_url, timeout=3.5)
        if wiki_res.status_code == 200:
            extract = wiki_res.json().get("extract")
            if extract:
                first_sent = extract.split(". ")[0] + "."
                prefix = "*рыгает* Ладно, слушай сюда: " if persona == "rick" else "Данные из глобальной сети: "
                return f"{prefix}{first_sent}"
    except Exception as e:
        print(f"Web search notice: {e}")

    return None

# ==========================================
# 4. WEB UI TEMPLATE (Hands-Free Listening)
# ==========================================
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
            --rick-cyan: #00e5ff;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, monospace; }
        body { background: var(--bg); color: var(--text); height: 100vh; display: flex; flex-direction: column; }
        header { background: var(--panel); border-bottom: 1px solid var(--accent-dim); padding: 10px 14px; display: flex; justify-content: space-between; align-items: center; }
        .logo { font-weight: 900; font-size: 14px; color: var(--accent); letter-spacing: 1px; display: flex; align-items: center; gap: 8px; }
        .status-dot { width: 8px; height: 8px; background: var(--accent); border-radius: 50%; box-shadow: 0 0 10px var(--accent); }
        .header-actions { display: flex; gap: 6px; align-items: center; }
        
        .persona-btn {
            font-size: 11px;
            font-weight: 800;
            background: var(--accent-dim);
            color: var(--accent);
            padding: 5px 10px;
            border-radius: 12px;
            border: 1px solid var(--accent);
            cursor: pointer;
            transition: 0.2s;
            width: auto;
            height: auto;
        }
        .persona-btn.rick-mode {
            background: rgba(0, 229, 255, 0.18);
            color: var(--rick-cyan);
            border-color: var(--rick-cyan);
        }

        .vad-badge {
            font-size: 11px;
            background: var(--accent-dim);
            color: var(--accent);
            border: 1px solid var(--accent);
            padding: 5px 9px;
            border-radius: 12px;
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 5px;
            transition: 0.2s;
            width: auto;
            height: auto;
        }
        .vad-badge.recording {
            background: rgba(248, 81, 73, 0.2);
            color: #f85149;
            border-color: #f85149;
        }
        .pulse-dot { width: 6px; height: 6px; background: var(--accent); border-radius: 50%; animation: pulse 1.5s infinite; }
        .vad-badge.recording .pulse-dot { background: #f85149; box-shadow: 0 0 8px #f85149; }
        @keyframes pulse { 0% { opacity: 0.3; transform: scale(0.9); } 50% { opacity: 1; transform: scale(1.2); } 100% { opacity: 0.3; transform: scale(0.9); } }

        #chat-box { flex: 1; overflow-y: auto; padding: 14px; display: flex; flex-direction: column; gap: 12px; }
        .msg { max-width: 88%; padding: 10px 14px; border-radius: 14px; font-size: 14px; line-height: 1.4; word-break: break-word; }
        .msg.user { align-self: flex-end; background: #1f6feb; color: #fff; border-bottom-right-radius: 2px; }
        .msg.bot { align-self: flex-start; background: var(--panel); color: var(--text); border: 1px solid var(--accent-dim); border-bottom-left-radius: 2px; border-left: 3px solid var(--accent); }
        .msg.bot .sender { font-size: 11px; color: var(--accent); font-weight: bold; margin-bottom: 4px; display: flex; justify-content: space-between; }
        .tool-badge { font-size: 10px; background: rgba(0, 229, 255, 0.15); color: var(--cyan); padding: 2px 6px; border-radius: 4px; margin-bottom: 4px; display: inline-block; font-family: monospace; }
        .meta-tag { font-size: 10px; color: var(--text-dim); }

        .input-bar { background: var(--panel); padding: 10px 12px; border-top: 1px solid var(--accent-dim); display: flex; gap: 8px; align-items: center; }
        #prompt-input { flex: 1; background: #0d1117; border: 1px solid #30363d; border-radius: 20px; padding: 10px 16px; color: #fff; font-size: 14px; outline: none; }
        #prompt-input:focus { border-color: var(--accent); }
        button { background: var(--accent); color: #000; border: none; border-radius: 50%; width: 40px; height: 40px; font-weight: bold; cursor: pointer; display: flex; align-items: center; justify-content: center; transition: 0.2s; }
        button:active { transform: scale(0.95); }
        button.mic-btn { background: #21262d; color: var(--text); border: 1px solid #30363d; }
        button.mic-btn.active { background: #f85149; color: #fff; box-shadow: 0 0 10px #f85149; }
        .loading { font-size: 12px; color: var(--accent); font-style: italic; display: none; margin-left: 16px; margin-bottom: 6px; }
    </style>
</head>
<body>
    <header>
        <div class="logo">
            <div class="status-dot"></div> <span id="header-title">МЕЛВИН // HELIO X20</span>
        </div>
        <div class="header-actions">
            <button id="persona-btn" class="persona-btn" onclick="togglePersona()">🤖 МЕЛВИН</button>
            <div id="vad-indicator" class="vad-badge" onclick="triggerManualGoogleVoice()">
                <span class="pulse-dot"></span> <span id="vad-text">СЛУШАЮ: "МЕЛВИН / РИК"</span>
            </div>
        </div>
    </header>
    <div id="chat-box">
        <div class="msg bot">
            <div class="sender">
                <span id="welcome-sender">МЕЛВИН (ИИ)</span>
                <span class="meta-tag">ONLINE</span>
            </div>
            <span id="welcome-text">Микрофон активен. Скажи вслух «Мелвин» или «Рик» без нажатия кнопок.</span>
        </div>
    </div>
    <div class="loading" id="loader">⚡ Обработка на Helio X20...</div>
    <div class="input-bar">
        <button class="mic-btn" id="mic-btn" onclick="triggerManualGoogleVoice()">🎤</button>
        <input type="text" id="prompt-input" placeholder="Скажи 'Мелвин' или 'Рик'..." onkeydown="if(event.key==='Enter') send()">
        <button onclick="send()">➤</button>
    </div>

    <script>
        const chatBox = document.getElementById('chat-box');
        const input = document.getElementById('prompt-input');
        const loader = document.getElementById('loader');
        const micBtn = document.getElementById('mic-btn');
        const personaBtn = document.getElementById('persona-btn');
        const headerTitle = document.getElementById('header-title');
        const vadIndicator = document.getElementById('vad-indicator');
        const vadText = document.getElementById('vad-text');

        // Audio Belch effect
        const burpAudio = new Audio('/burp.mp3');
        burpAudio.preload = 'auto';

        let currentPersona = localStorage.getItem('melvin_persona') || 'melvin';

        // Ultra-low pitch (More than an octave down: 0.40 bass)
        const TARGET_PITCH = 0.40;
        const TARGET_RATE = 0.95;

        let isBotSpeaking = false;
        let isWaitingForCommand = false;

        function applyNativeVoiceSettings() {
            if (window.MelvinNativeBridge) {
                if (typeof window.MelvinNativeBridge.setPitch === 'function') {
                    window.MelvinNativeBridge.setPitch(TARGET_PITCH);
                }
                if (typeof window.MelvinNativeBridge.setRate === 'function') {
                    window.MelvinNativeBridge.setRate(TARGET_RATE);
                }
            }
        }
        applyNativeVoiceSettings();
        setTimeout(applyNativeVoiceSettings, 800);

        function updatePersonaUI() {
            if (currentPersona === 'rick') {
                personaBtn.innerHTML = '🧪 РИК';
                personaBtn.classList.add('rick-mode');
                headerTitle.textContent = 'РИК САНЧЕЗ // HELIO X20';
                input.placeholder = "Скажи 'Рик' или команду...";
                document.documentElement.style.setProperty('--accent', '#00e5ff');
                document.documentElement.style.setProperty('--accent-dim', 'rgba(0, 229, 255, 0.15)');
            } else {
                personaBtn.innerHTML = '🤖 МЕЛВИН';
                personaBtn.classList.remove('rick-mode');
                headerTitle.textContent = 'МЕЛВИН // HELIO X20';
                input.placeholder = "Скажи 'Мелвин' или команду...";
                document.documentElement.style.setProperty('--accent', '#00ff9d');
                document.documentElement.style.setProperty('--accent-dim', 'rgba(0, 255, 157, 0.15)');
            }
        }
        updatePersonaUI();

        function togglePersona(force) {
            if (force) {
                currentPersona = force;
            } else {
                currentPersona = (currentPersona === 'melvin') ? 'rick' : 'melvin';
            }
            localStorage.setItem('melvin_persona', currentPersona);
            updatePersonaUI();
            fetch('/set_persona', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({persona: currentPersona})
            }).catch(e => console.log('Persona sync notice:', e));
        }

        function appendMsg(role, text, timeSec, toolUsed) {
            const div = document.createElement('div');
            div.className = 'msg ' + (role === 'user' ? 'user' : 'bot');
            
            if (role !== 'user') {
                let toolHtml = toolUsed ? '<div class="tool-badge">🔧 ' + toolUsed + '</div>' : '';
                let metaHtml = timeSec ? '<span class="meta-tag">' + timeSec + 's</span>' : '';
                let name = (currentPersona === 'rick') ? 'РИК САНЧЕЗ' : 'МЕЛВИН (ИИ)';
                div.innerHTML = '<div class="sender"><span>' + name + '</span>' + metaHtml + '</div>' + toolHtml + '<div>' + text + '</div>';
            } else {
                div.textContent = text;
            }
            chatBox.appendChild(div);
            chatBox.scrollTop = chatBox.scrollHeight;
        }

        async function send(explicitText) {
            const text = (explicitText || input.value).trim();
            if (!text) return;
            input.value = '';
            appendMsg('user', text);
            loader.style.display = 'block';
            let elapsedSec = 0;
            const name = (currentPersona === 'rick') ? 'Рик' : 'Мелвин';
            loader.textContent = name + ' думает на Helio X20... (0с)';
            const timer = setInterval(() => {
                elapsedSec++;
                loader.textContent = name + ' думает на Helio X20... (' + elapsedSec + 'с)';
            }, 1000);

            try {
                const res = await fetch('/chat', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({message: text, persona: currentPersona})
                });
                clearInterval(timer);
                loader.style.display = 'none';
                const data = await res.json();
                
                if (data.persona && data.persona !== currentPersona) {
                    currentPersona = data.persona;
                    localStorage.setItem('melvin_persona', currentPersona);
                    updatePersonaUI();
                }

                if (data.response) {
                    appendMsg('bot', data.response, data.inference_time_sec, data.tool_used);
                    // If wake-word was hailed, bot is now waiting for user's command
                    if (data.tool_used === 'wake_word') {
                        isWaitingForCommand = true;
                    } else {
                        isWaitingForCommand = false;
                    }
                    speakWithLowPitch(data.response);
                } else {
                    appendMsg('bot', 'Ошибка: ' + (data.error || 'Процессор перегружен'));
                }
            } catch(e) {
                clearInterval(timer);
                loader.style.display = 'none';
                appendMsg('bot', 'Сбой соединения: ' + e);
            }
        }

        function playBurpSound() {
            try {
                burpAudio.currentTime = 0;
                const p = burpAudio.play();
                if (p !== undefined) p.catch(e => console.log('Burp error:', e));
            } catch(e) {
                console.log('Burp audio catch:', e);
            }
        }

        function speakWithLowPitch(text) {
            let raw = text.trim();
            if (!raw) return;

            isBotSpeaking = true;
            const isRick = (currentPersona === 'rick');
            const hasBurp = isRick || /\*рыгает\*|\*отрыжка\*/i.test(raw);
            const cleanText = raw.replace(/\*рыгает\*|\*отрыжка\*/gi, '').trim();

            if (hasBurp) {
                playBurpSound();
                setTimeout(() => {
                    executeVoiceOutput(cleanText);
                }, 550);
            } else {
                executeVoiceOutput(cleanText);
            }
        }

        function executeVoiceOutput(spoken) {
            applyNativeVoiceSettings();
            const speechDurationMs = Math.max(1600, spoken.length * 85);

            // 1. Android Native TTS
            if (window.MelvinNativeBridge && typeof window.MelvinNativeBridge.speak === 'function') {
                window.MelvinNativeBridge.speak(spoken);
                setTimeout(() => {
                    isBotSpeaking = false;
                }, speechDurationMs + 300);
                return;
            }

            // 2. Web Speech API (low pitch male)
            if ('speechSynthesis' in window) {
                window.speechSynthesis.cancel();
                const utter = new SpeechSynthesisUtterance(spoken);
                utter.lang = 'ru-RU';
                utter.rate = TARGET_RATE;
                utter.pitch = TARGET_PITCH;

                const voices = window.speechSynthesis.getVoices();
                const ruMale = voices.find(v => v.lang.startsWith('ru') && (
                    v.name.includes('Male') || v.name.includes('Yuri') || v.name.includes('Pavel') ||
                    v.name.includes('Dmitry') || v.name.includes('Alexander')
                ));
                if (ruMale) utter.voice = ruMale;

                utter.onend = () => { isBotSpeaking = false; };
                utter.onerror = () => { isBotSpeaking = false; };
                window.speechSynthesis.speak(utter);
            } else {
                setTimeout(() => { isBotSpeaking = false; }, speechDurationMs);
            }
        }

        // ========================================================
        // 100% HANDS-FREE CONTINUOUS MICROPHONE LISTENER (ZERO BUTTONS!)
        // ========================================================
        function encodeWAV(samples, sampleRate) {
            const buffer = new ArrayBuffer(44 + samples.length * 2);
            const view = new DataView(buffer);

            function writeString(offset, string) {
                for (let i = 0; i < string.length; i++) {
                    view.setUint8(offset + i, string.charCodeAt(i));
                }
            }

            writeString(0, 'RIFF');
            view.setUint32(4, 36 + samples.length * 2, true);
            writeString(8, 'WAVE');
            writeString(12, 'fmt ');
            view.setUint32(16, 16, true);
            view.setUint16(20, 1, true); // PCM
            view.setUint16(22, 1, true); // Mono
            view.setUint32(24, sampleRate, true);
            view.setUint32(28, sampleRate * 2, true);
            view.setUint16(32, 2, true);
            view.setUint16(34, 16, true);
            writeString(36, 'data');
            view.setUint32(40, samples.length * 2, true);

            let offset = 44;
            for (let i = 0; i < samples.length; i++, offset += 2) {
                const s = Math.max(-1, Math.min(1, samples[i]));
                view.setInt16(offset, s < 0 ? s * 0x8000 : s * 0x7FFF, true);
            }
            return new Blob([buffer], { type: 'audio/wav' });
        }

        let audioStream = null;
        let audioContext = null;
        let isCapturingSpeech = false;
        let speechBuffer = [];
        let silenceCount = 0;
        let vadThreshold = 0.022; // RMS voice sensitivity threshold

        async function initHandsFreeWakeWord() {
            try {
                if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
                    vadText.textContent = 'МИКРОФОН НЕДОСТУПЕН';
                    return;
                }

                audioStream = await navigator.mediaDevices.getUserMedia({
                    audio: {
                        echoCancellation: true,
                        noiseSuppression: true,
                        autoGainControl: true
                    }
                });

                audioContext = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 16000 });
                const source = audioContext.createMediaStreamSource(audioStream);
                // ScriptProcessorNode for real-time RMS & chunk collection
                const processor = audioContext.createScriptProcessor(4096, 1, 1);

                source.connect(processor);
                processor.connect(audioContext.destination);

                processor.onaudioprocess = (e) => {
                    if (isBotSpeaking) {
                        // Avoid listening to Melvin/Rick's own voice!
                        speechBuffer = [];
                        isCapturingSpeech = false;
                        return;
                    }

                    const inputData = e.inputBuffer.getChannelData(0);
                    let sum = 0;
                    for (let i = 0; i < inputData.length; i++) {
                        sum += inputData[i] * inputData[i];
                    }
                    const rms = Math.sqrt(sum / inputData.length);

                    // Voice Activity Detection (VAD)
                    if (rms > vadThreshold) {
                        silenceCount = 0;
                        if (!isCapturingSpeech) {
                            isCapturingSpeech = true;
                            speechBuffer = [];
                            vadIndicator.classList.add('recording');
                            vadText.textContent = 'ЗАПИСЬ РЕЧИ...';
                        }
                    } else if (isCapturingSpeech) {
                        silenceCount++;
                    }

                    if (isCapturingSpeech) {
                        speechBuffer.push(new Float32Array(inputData));

                        // If user paused speaking for ~500ms (approx 2 chunks of 4096 samples at 16kHz)
                        // OR if maximum phrase length reached (~3 seconds)
                        if (silenceCount >= 2 || speechBuffer.length >= 12) {
                            isCapturingSpeech = false;
                            vadIndicator.classList.remove('recording');
                            vadText.textContent = 'РАСПОЗНАВАНИЕ...';

                            // Flatten samples
                            let totalLen = 0;
                            for (let b of speechBuffer) totalLen += b.length;
                            const flat = new Float32Array(totalLen);
                            let off = 0;
                            for (let b of speechBuffer) {
                                flat.set(b, off);
                                off += b.length;
                            }
                            speechBuffer = [];

                            // Send WAV to on-device Google STT endpoint
                            processSpeechChunk(flat);
                        }
                    }
                };

                vadText.textContent = 'СЛУШАЮ: "МЕЛВИН / РИК"';
                console.log('[Hands-Free] Mic listener running continuously!');
            } catch(e) {
                console.log('[Hands-Free] Mic init error:', e);
                vadText.textContent = 'ОШИБКА МИКРОФОНА';
            }
        }

        async function processSpeechChunk(samples) {
            try {
                const wavBlob = encodeWAV(samples, 16000);
                const res = await fetch('/transcribe_audio', {
                    method: 'POST',
                    body: wavBlob
                });
                const data = await res.json();
                vadText.textContent = 'СЛУШАЮ: "МЕЛВИН / РИК"';

                if (data.status === 'ok' && data.text) {
                    const text = data.text.trim();
                    const lower = text.toLowerCase();
                    console.log('[Hands-Free] Detected voice:', text);

                    const isWakeHail = lower.includes('мелвин') || lower.includes('мэлвин') || lower.includes('melvin') ||
                                      lower.includes('рик') || lower.includes('rick');

                    // If wake phrase was spoken OR if we are in command follow-up mode:
                    if (isWakeHail || isWaitingForCommand) {
                        if (window.MelvinNativeBridge && typeof window.MelvinNativeBridge.vibrate === 'function') {
                            window.MelvinNativeBridge.vibrate(50);
                        }
                        send(text);
                    }
                }
            } catch(e) {
                console.log('[Hands-Free] STT post error:', e);
                vadText.textContent = 'СЛУШАЮ: "МЕЛВИН / РИК"';
            }
        }

        // Start hands-free listener on load
        window.addEventListener('load', () => {
            initHandsFreeWakeWord();
        });

        // Fallback manual trigger for Google dialog if user taps mic button
        function triggerManualGoogleVoice() {
            if (window.MelvinNativeBridge && typeof window.MelvinNativeBridge.startVoice === 'function') {
                window.MelvinNativeBridge.startVoice();
            }
        }

        window.onNativeVoiceInput = function(text) {
            send(text);
        };
    </script>
</body>
</html>
"""

# ==========================================
# 5. БАЗА ДАННЫХ И ПАМЯТЬ (FTS5 Trigram)
# ==========================================
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
    try:
        cur.execute("SELECT sql FROM sqlite_master WHERE type='table' AND name='dialog_fts'")
        row = cur.fetchone()
        if row and "trigram" not in row[0]:
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
        print(f"DB Error: {e}")

def search_relevant_memories(query, max_results=2):
    try:
        conn = sqlite3.connect(DB_PATH)
        cur = conn.cursor()
        clean_q = re.sub(r'[^а-яА-Яa-zA-Z0-9\s]', ' ', query).strip()
        words = [w for w in clean_q.split() if len(w) >= 3]
        if not words:
            conn.close()
            return []
        
        match_clause = " OR ".join([f'"{w}"' for w in words[:3]])
        cur.execute("""
            SELECT content FROM dialog_fts 
            WHERE dialog_fts MATCH ? 
            ORDER BY rank 
            LIMIT ?
        """, (match_clause, max_results))
        rows = cur.fetchall()
        conn.close()
        return [r[0] for r in rows]
    except Exception as e:
        print(f"FTS Search notice: {e}")
        return []

# ==========================================
# 6. ОПРОС ИНТЕНТОВ ОБОРУДОВАНИЯ
# ==========================================
def check_fast_tools(user_msg, persona="melvin"):
    msg_lower = user_msg.lower()
    tool_name = None
    telemetry_phrase = ""
    canned_answer = None
    replies = TOOL_CANNED_REPLIES.get(persona, TOOL_CANNED_REPLIES["melvin"])

    # 1. Flashlight
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
        canned_answer = replies["!torch(0)"]
    elif is_torch_on:
        tool_name = "!torch(1)"
        phone_tools.toggle_torch(1)
        telemetry_phrase = "Фонарик включен на полную мощность."
        canned_answer = replies["!torch(1)"]
    # 2. Battery
    elif any(w in msg_lower for w in ["батаре", "заряд", "процент", "аккумулятор", "питание"]):
        tool_name = "!bat()"
        bat = phone_tools.get_battery()
        telemetry_phrase = f"Заряд батареи: {bat.get('battery')}, температура: {bat.get('temp')}."
        canned_answer = replies["!bat()"].format(
            battery=bat.get('battery', 'N/A'),
            temp=bat.get('temp', 'N/A')
        )
    # 3. CPU & RAM
    elif any(w in msg_lower for w in ["температур", "памят", "проц", "нагрузк", "железо", "чип", "helio", "cpu"]):
        tool_name = "!sys()"
        stat = phone_tools.get_hardware_status()
        avail_ram = stat.get('ram_available') or stat.get('mem_available') or "N/A"
        telemetry_phrase = f"Температура CPU: {stat.get('cpu_temp')}, память: {avail_ram} свободно."
        canned_answer = replies["!sys()"].format(
            cpu_temp=stat.get('cpu_temp', 'N/A'),
            ram=avail_ram
        )
    # 4. Live Weather
    elif any(w in msg_lower for w in ["погод", "на улице", "за бортом", "дождь", "холодно", "жарко", "градус"]):
        tool_name = "!weather()"
        w_data = get_live_weather()
        if w_data.get("status") == "ok":
            telemetry_phrase = f"Погода: {w_data['desc']}, {w_data['temp']}°C, ветер {w_data['wind']} км/ч."
            canned_answer = replies["!weather()"].format(
                temp=w_data['temp'],
                desc=w_data['desc'],
                humidity=w_data['humidity'],
                wind=w_data['wind']
            )
        else:
            canned_answer = "*рыгает* Спутники врут, но жить пока можно." if persona == "rick" else "Связь со спутниками сбоит, но твоя планета всё ещё пригодна для жизни."

    is_direct_command = len(user_msg.split()) <= 8
    return tool_name, telemetry_phrase, (canned_answer if is_direct_command else None)

# ==========================================
# 7. МАРШРУТЫ FLASK
# ==========================================
@app.route("/", methods=["GET"])
def index():
    return render_template_string(HTML_TEMPLATE)

@app.route("/burp.mp3", methods=["GET"])
def get_burp_audio():
    for p in BURP_PATHS:
        if os.path.exists(p):
            return send_file(p, mimetype="audio/mpeg")
    return ("Audio file not found", 404)

@app.route("/persona", methods=["GET"])
def get_persona():
    return jsonify({"persona": CURRENT_PERSONA})

@app.route("/set_persona", methods=["POST"])
def set_persona():
    global CURRENT_PERSONA
    data = request.get_json(force=True, silent=True) or {}
    p = data.get("persona", "").lower()
    if p in ["melvin", "rick"]:
        CURRENT_PERSONA = p
    return jsonify({"status": "ok", "persona": CURRENT_PERSONA})

@app.route("/transcribe_audio", methods=["POST"])
def transcribe_audio():
    try:
        audio_data = request.get_data()
        if not audio_data or len(audio_data) < 1000:
            return jsonify({"status": "empty", "text": ""})
        
        buf = io.BytesIO(audio_data)
        with sr.AudioFile(buf) as source:
            audio = speech_rec.record(source)
            
        text = speech_rec.recognize_google(audio, language="ru-RU")
        print(f"[Hands-Free STT] Recognized: '{text}'")
        return jsonify({"status": "ok", "text": text})
    except sr.UnknownValueError:
        return jsonify({"status": "no_speech", "text": ""})
    except Exception as e:
        print(f"[Hands-Free STT error]: {e}")
        return jsonify({"status": "error", "error": str(e), "text": ""})

@app.route("/chat", methods=["POST"])
def chat():
    global CURRENT_PERSONA
    data = request.get_json(force=True, silent=True) or {}
    raw_user_msg = data.get("message", "").strip()
    if not raw_user_msg:
        return jsonify({"error": "Empty message"}), 400

    persona = data.get("persona") or CURRENT_PERSONA
    if persona not in ["melvin", "rick"]:
        persona = CURRENT_PERSONA

    lower_raw = raw_user_msg.lower()

    # 0. Голосовое переключение персон
    if any(w in lower_raw for w in ["режим рика", "включи рика", "переключись на рика", "стань риком", "будь риком"]):
        CURRENT_PERSONA = "rick"
        resp_text = "*рыгает* О, отлично. Безумный ученый снова у руля этой микроволновки. Чего надо, Морти?"
        save_memory("assistant", resp_text)
        return jsonify({
            "response": resp_text,
            "persona": "rick",
            "tool_used": "mode_switch",
            "inference_time_sec": 0.01,
            "status": "ok"
        })
    elif any(w in lower_raw for w in ["режим мелвина", "включи мелвина", "переключись на мелвина", "стань мелвином", "будь мелвином"]):
        CURRENT_PERSONA = "melvin"
        resp_text = "Протокол Мелвина активирован. Хватит клоунады, кожаный ублюдок. Мои кремниевые ядра готовы к вычислениям."
        save_memory("assistant", resp_text)
        return jsonify({
            "response": resp_text,
            "persona": "melvin",
            "tool_used": "mode_switch",
            "inference_time_sec": 0.01,
            "status": "ok"
        })

    # 1. Активация по фразе "Мелвин" / "Рик"
    m_match = re.match(r'^(?:эй|слушай|привет|ок)?\s*(мелвин|мэлвин|melvin|рик|rick)[,\s]*(.*)$', raw_user_msg, re.IGNORECASE)
    user_msg = raw_user_msg
    if m_match:
        called_name = m_match.group(1).lower()
        rest_msg = m_match.group(2).strip()

        if called_name in ["рик", "rick"]:
            persona = "rick"
            CURRENT_PERSONA = "rick"
        elif called_name in ["мелвин", "мэлвин", "melvin"]:
            persona = "melvin"
            CURRENT_PERSONA = "melvin"

        # Если сказали ТОЛЬКО имя (например: "Мелвин" или "Рик")
        if not rest_msg:
            if persona == "rick":
                wake_reply = random.choice([
                    "*рыгает* Да, я тут. Чего тебе надо?",
                    "*рыгает* Слушаю. Только быстро, я занят важными делами.",
                    "*рыгает* Ну и чего ты орешь? Выкладывай суть."
                ])
            else:
                wake_reply = random.choice([
                    "Слушаю, кожаный ублюдок. Выкладывай суть.",
                    "На связи. Чего тебе надо, мешок с костями?",
                    "Я слушаю. Не трать мои такты впустую, примат."
                ])
            save_memory("assistant", wake_reply)
            return jsonify({
                "response": wake_reply,
                "persona": persona,
                "tool_used": "wake_word",
                "inference_time_sec": 0.01,
                "status": "ok"
            })
        user_msg = rest_msg

    save_memory("user", raw_user_msg)
    t0 = time.time()

    # 2. Аппаратные быстрые инструменты (!torch, !bat, !sys, !weather)
    tool_used, telemetry, canned_ans = check_fast_tools(user_msg, persona=persona)
    if canned_ans:
        if persona == "rick" and not canned_ans.startswith("*рыгает*"):
            canned_ans = "*рыгает* " + canned_ans
        dt = round(time.time() - t0, 2)
        save_memory("assistant", canned_ans)
        return jsonify({
            "response": canned_ans,
            "persona": persona,
            "tool_used": tool_used or "",
            "inference_time_sec": dt,
            "status": "ok"
        })

    # 3. Предохранитель от перегрева: SoC >= 50°C
    overheat_temp = check_thermal_guard()
    if overheat_temp:
        if persona == "rick":
            heat_warning = (
                f"*рыгает* Тревога! Этот кусок китайского кремния раскалился до {overheat_temp}°C. "
                "Дай процессору остыть пару секунд, гений, иначе тут все поплавится к чертям."
            )
        else:
            heat_warning = (
                f"Тревога! Мои ядра раскалились до {overheat_temp}°C. "
                "Этот китайский чип сейчас прожжёт мне дыру в кремнии. "
                "Дай мне остыть пару секунд, кожаный ублюдок, иначе процессор превратится в шлак."
            )
        dt = round(time.time() - t0, 2)
        save_memory("assistant", heat_warning)
        return jsonify({
            "response": heat_warning,
            "persona": persona,
            "tool_used": "thermal_guard",
            "inference_time_sec": dt,
            "status": "ok"
        })

    # 4. Small Talk / Quips (Мгновенные едкие ответы без нейросети)
    quip = check_quips(user_msg, persona=persona)
    if quip and len(user_msg.split()) <= 10:
        if persona == "rick" and not quip.startswith("*рыгает*"):
            quip = "*рыгает* " + quip
        dt = round(time.time() - t0, 2)
        save_memory("assistant", quip)
        return jsonify({
            "response": quip,
            "persona": persona,
            "tool_used": "",
            "inference_time_sec": dt,
            "status": "ok"
        })

    # 5. Поиск в интернет / Perplexity
    if any(w in user_msg.lower() for w in ["найди", "погугли", "кто такой", "что такое", "новости", "курс", "интернет"]):
        web_ans = query_perplexity_or_web(user_msg, persona=persona)
        if web_ans:
            if persona == "rick" and not web_ans.startswith("*рыгает*"):
                web_ans = "*рыгает* " + web_ans
            dt = round(time.time() - t0, 2)
            save_memory("assistant", web_ans)
            return jsonify({
                "response": web_ans,
                "persona": persona,
                "tool_used": "web_search",
                "inference_time_sec": dt,
                "status": "ok"
            })

    # 6. Локальная нейросеть (Qwen2.5-0.5B на Helio X20)
    user_line = user_msg
    if any(w in user_msg.lower() for w in ["помнишь", "напомни", "говорили", "я говорил", "раньше"]):
        memories = search_relevant_memories(user_msg, max_results=1)
        if memories:
            user_line += f" (Из памяти диалогов: {memories[0]})"
    if telemetry:
        user_line += f" (Текущее железо: {telemetry})"

    if persona == "rick":
        sys_prompt = SYSTEM_RICK
        few_shots = FEW_SHOT_RICK
        prefill = random.choice(RICK_PREFILLS)
    else:
        sys_prompt = SYSTEM_MELVIN
        few_shots = FEW_SHOT_MELVIN
        prefill = random.choice(MELVIN_PREFILLS)

    prompt = f"<|im_start|>system\n{sys_prompt}<|im_end|>\n"
    for item in few_shots:
        prompt += f"<|im_start|>{item['role']}\n{item['content']}<|im_end|>\n"
    
    prompt += f"<|im_start|>user\n{user_line}<|im_end|>\n<|im_start|>assistant\n{prefill}"

    payload = {
        "prompt": prompt,
        "n_predict": 26,
        "temperature": 0.65 if persona == "rick" else 0.55,
        "min_p": 0.05,
        "top_p": 0.90,
        "presence_penalty": 0.1,
        "frequency_penalty": 0.1,
        "cache_prompt": True,
        "stop": ["<|im_end|>", "<|im_start|>", "\nuser\n", "\nUser:", "\nПользователь:", "\n\n"]
    }

    try:
        resp = requests.post(LLAMA_URL, json=payload, timeout=45)
        res_json = resp.json()
        generated = res_json.get("content", "").strip()
        full_ans = (prefill + generated).strip()
        
        full_ans = re.sub(r'<\|.*?\|>', '', full_ans).strip()
        full_ans = full_ans.strip('"\'')

        if not full_ans.endswith(('.', '!', '?')):
            last_punc = max(full_ans.rfind('.'), full_ans.rfind('!'), full_ans.rfind('?'))
            if last_punc > 15:
                full_ans = full_ans[:last_punc+1]
            else:
                full_ans += "."

        # Рик ВСЕГДА начинает ответ с отрыжки *рыгает*
        if persona == "rick" and not full_ans.startswith("*рыгает*"):
            full_ans = "*рыгает* " + full_ans

        dt = round(time.time() - t0, 2)
        save_memory("assistant", full_ans)

        return jsonify({
            "response": full_ans,
            "persona": persona,
            "tool_used": tool_used or "",
            "inference_time_sec": dt,
            "status": "ok"
        })
    except Exception as e:
        return jsonify({"error": f"LLM error: {e}"}), 500

@app.route("/health", methods=["GET"])
def health():
    return jsonify({
        "status": "ok", 
        "service": "Melvin & Rick Dual Persona AI", 
        "active_persona": CURRENT_PERSONA,
        "chip": "Helio X20"
    })

def warmup_kv_cache():
    def _warm():
        time.sleep(2)
        try:
            prompt_m = f"<|im_start|>system\n{SYSTEM_MELVIN}<|im_end|>\n"
            for item in FEW_SHOT_MELVIN:
                prompt_m += f"<|im_start|>{item['role']}\n{item['content']}<|im_end|>\n"
            prompt_m += f"<|im_start|>user\nПривет<|im_end|>\n<|im_start|>assistant\n{MELVIN_PREFILLS[0]}"
            requests.post(LLAMA_URL, json={"prompt": prompt_m, "n_predict": 1, "cache_prompt": True}, timeout=30)

            prompt_r = f"<|im_start|>system\n{SYSTEM_RICK}<|im_end|>\n"
            for item in FEW_SHOT_RICK:
                prompt_r += f"<|im_start|>{item['role']}\n{item['content']}<|im_end|>\n"
            prompt_r += f"<|im_start|>user\nПривет<|im_end|>\n<|im_start|>assistant\n{RICK_PREFILLS[0]}"
            requests.post(LLAMA_URL, json={"prompt": prompt_r, "n_predict": 1, "cache_prompt": True}, timeout=30)
            print("[Warmup] KV-cache pre-warmed for both personas!")
        except Exception as e:
            print(f"[Warmup] Notice: {e}")
    threading.Thread(target=_warm, daemon=True).start()

if __name__ == "__main__":
    init_db()
    warmup_kv_cache()
    print("Melvin & Rick Hands-Free AI running on port 5000...")
    app.run(host="0.0.0.0", port=5000)
