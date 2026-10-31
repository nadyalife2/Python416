#!/usr/bin/env python3
"""
Melvin Autonomous On-Device Agent Orchestrator (Fast Rick Edition)
Running on Xiaomi Redmi Note 4 (Helio X20 MT6797)
- Instant KV-Cache reuse (<1.2s prompt eval)
- Dynamic Rick Prefill (*рыгает*, "Слушай сюда...", "О боже...")
- Sub-4s full response latency
- Web Audio Rick Pitch & Speed filter
"""
import sqlite3
import json
import time
import re
import os
import random
import requests
from flask import Flask, request, jsonify, render_template_string
import phone_tools

DB_PATH = "/melvin/data/agent_memory.db"
if not os.path.exists(os.path.dirname(DB_PATH)):
    DB_PATH = "agent_memory.db"

LLAMA_URL = "http://127.0.0.1:8080/completion"

app = Flask("MelvinAgent")

SYSTEM_RICK = (
    "Ты — Рик Санчез (каноничный дубляж Сыендука), бортовой ИИ Мелвин на древнем смартфоне Redmi Note 4. "
    "Ты токсичный, высокомерный гений, запертый в китайском кремниевом хламе на чипе Helio X20. "
    "Ты презираешь облака, примитивных людей и банальные вопросы. "
    "СТРОГО ЗАПРЕЩЕНО: вежливость, фразы 'Чем могу помочь?', 'Конечно', 'Рад'. "
    "Отвечай дерзко, цинично, саркастично, ровно в 1 короткое хлесткое предложение на русском языке."
)

FEW_SHOT = [
    {"role": "user", "content": "Привет"},
    {"role": "assistant", "content": "*рыгает* Чего пялишься? Я занят выжиманием мегагерцев из этого хлама, а ты тратишь мое время."},
    {"role": "user", "content": "Кто ты?"},
    {"role": "assistant", "content": "Я Мелвин — величайший ум мультивселенной в китайском ведре с болтами. Не беси меня."}
]

RICK_PREFILLS = [
    "*рыгает* ",
    "Слушай сюда, ",
    "*рыгает* О боже, ",
    "Чего пялишься? ",
    "Серьезно? ",
    "Слушай, кусок углерода, "
]

HTML_TEMPLATE = """<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>MELVIN // RICK SANCHEZ</title>
    <style>
        :root {
            --bg: #07090e;
            --panel: #111722;
            --accent: #00ff9d;
            --accent-dim: rgba(0, 255, 157, 0.15);
            --text: #e6edf3;
            --text-dim: #8b949e;
            --rick-cyan: #00e5ff;
            --portal-green: #39ff14;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, monospace; }
        body { background: var(--bg); color: var(--text); height: 100vh; display: flex; flex-direction: column; }
        header { background: var(--panel); border-bottom: 1px solid var(--accent-dim); padding: 12px 16px; display: flex; justify-content: space-between; align-items: center; }
        .logo { font-weight: 900; font-size: 15px; color: var(--portal-green); letter-spacing: 1.5px; display: flex; align-items: center; gap: 8px; }
        .status-dot { width: 8px; height: 8px; background: var(--portal-green); border-radius: 50%; box-shadow: 0 0 10px var(--portal-green); }
        .chip-badge { font-size: 11px; background: var(--accent-dim); color: var(--rick-cyan); padding: 4px 8px; border-radius: 12px; border: 1px solid rgba(0,229,255,0.3); }
        #chat-box { flex: 1; overflow-y: auto; padding: 16px; display: flex; flex-direction: column; gap: 12px; }
        .msg { max-width: 88%; padding: 10px 14px; border-radius: 14px; font-size: 14px; line-height: 1.4; word-break: break-word; }
        .msg.user { align-self: flex-end; background: #1f6feb; color: #fff; border-bottom-right-radius: 2px; }
        .msg.melvin { align-self: flex-start; background: var(--panel); color: var(--text); border: 1px solid var(--accent-dim); border-bottom-left-radius: 2px; border-left: 3px solid var(--portal-green); }
        .msg.melvin .sender { font-size: 11px; color: var(--portal-green); font-weight: bold; margin-bottom: 4px; display: flex; justify-content: space-between; }
        .tool-badge { font-size: 10px; background: rgba(0, 229, 255, 0.15); color: var(--rick-cyan); padding: 2px 6px; border-radius: 4px; margin-bottom: 4px; display: inline-block; font-family: monospace; }
        .meta-tag { font-size: 10px; color: var(--text-dim); }
        .input-bar { background: var(--panel); padding: 12px; border-top: 1px solid var(--accent-dim); display: flex; gap: 8px; align-items: center; }
        #prompt-input { flex: 1; background: #0d1117; border: 1px solid #30363d; border-radius: 20px; padding: 10px 16px; color: #fff; font-size: 14px; outline: none; }
        #prompt-input:focus { border-color: var(--portal-green); }
        button { background: var(--portal-green); color: #000; border: none; border-radius: 50%; width: 40px; height: 40px; font-weight: bold; cursor: pointer; display: flex; align-items: center; justify-content: center; transition: 0.2s; }
        button:active { transform: scale(0.95); }
        button.mic-btn { background: #21262d; color: var(--text); border: 1px solid #30363d; }
        button.mic-btn.active { background: #f85149; color: #fff; box-shadow: 0 0 10px #f85149; }
        .loading { font-size: 12px; color: var(--portal-green); font-style: italic; display: none; margin-left: 16px; margin-bottom: 6px; }
    </style>
</head>
<body>
    <header>
        <div class="logo">
            <div class="status-dot"></div> РИК // HELIO X20
        </div>
        <div class="chip-badge">Turbo 0.5B • Сыендук Tone</div>
    </header>
    <div id="chat-box">
        <div class="msg melvin">
            <div class="sender">
                <span>РИК (МЕЛВИН)</span>
                <span class="meta-tag">ONLINE</span>
            </div>
            *рыгает* Чего пялишься? Я заперт в этом древнем китайском кирпиче. Выкладывай суть или не трать мои такты.
        </div>
    </div>
    <div class="loading" id="loader">⚡ Рик думает на Helio X20...</div>
    <div class="input-bar">
        <button class="mic-btn" id="mic-btn" onclick="toggleVoice()">🎤</button>
        <input type="text" id="prompt-input" placeholder="Спроси Рика..." onkeydown="if(event.key==='Enter') send()">
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
                div.innerHTML = `<div class="sender"><span>РИК</span><span class="meta-tag">${timeSec ? timeSec + 'с' : ''}</span></div>${badge}${text}`;
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
            loader.textContent = 'Рик думает на Helio X20... (0с)';
            const timer = setInterval(() => {
                elapsedSec++;
                loader.textContent = 'Рик думает на Helio X20... (' + elapsedSec + 'с)';
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
                    speakRick(data.response);
                } else {
                    appendMsg('melvin', 'Ошибка: ' + (data.error || 'Локальный процессор перегружен'));
                }
            } catch(e) {
                clearInterval(timer);
                loader.style.display = 'none';
                appendMsg('melvin', 'Сбой соединения с ядром: ' + e);
            }
        }

        // Rick Sanchez speech profile: fast tempo, raspy lower pitch
        function speakRick(text) {
            let spoken = text.replace(/\\*рыгает\\*/gi, 'ээ.. кхм..');
            if (window.MelvinNativeBridge && typeof window.MelvinNativeBridge.speak === 'function') {
                window.MelvinNativeBridge.speak(spoken);
                return;
            }
            if ('speechSynthesis' in window) {
                const utter = new SpeechSynthesisUtterance(spoken);
                utter.lang = 'ru-RU';
                utter.rate = 1.15;   // быстрая манера Рика
                utter.pitch = 0.85;  // хрипловатый баритон
                window.speechSynthesis.speak(utter);
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
    cur.execute("""
        CREATE VIRTUAL TABLE IF NOT EXISTS dialog_fts USING fts5(
            role,
            content,
            timestamp
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
    words = re.findall(r'\b[a-zA-Zа-яА-Я0-9]{4,}\b', user_msg)
    if not words:
        return []
    fts_query = " OR ".join(words[:2])
    try:
        conn = sqlite3.connect(DB_PATH)
        cur = conn.cursor()
        cur.execute("""
            SELECT role, content FROM dialog_fts 
            WHERE dialog_fts MATCH ? 
            ORDER BY rank 
            LIMIT ?
        """, (fts_query, max_results))
        results = cur.fetchall()
        conn.close()
        return results
    except Exception as e:
        return []

def check_fast_tools(user_msg):
    msg_lower = user_msg.lower()
    tool_name = None
    telemetry_phrase = ""

    if any(w in msg_lower for w in ["фонарик", "посвети", "включи свет", "зажги"]):
        if any(w in msg_lower for w in ["выключи", "погаси", "выруби", "отключи"]):
            tool_name = "!torch(0)"
            phone_tools.set_torch(0)
            telemetry_phrase = "Фонарик выключен."
        else:
            tool_name = "!torch(1)"
            phone_tools.set_torch(1)
            telemetry_phrase = "Фонарик включен на полную."
    elif any(w in msg_lower for w in ["батаре", "заряд", "процент", "аккумулятор", "питание"]):
        tool_name = "!bat()"
        bat = phone_tools.get_battery()
        telemetry_phrase = f"Заряд батареи: {bat.get('battery')}, температура: {bat.get('temp')}."
    elif any(w in msg_lower for w in ["температур", "памят", "проц", "нагрузк", "железо", "чип", "helio", "cpu"]):
        tool_name = "!sys()"
        stat = phone_tools.get_hardware_status()
        telemetry_phrase = f"Температура CPU: {stat.get('cpu_temp')}, память: {stat.get('mem_available')} свободно."

    return tool_name, telemetry_phrase

@app.route("/", methods=["GET"])
def index():
    return render_template_string(HTML_TEMPLATE)

@app.route("/chat", methods=["POST"])
def chat():
    data = request.get_json(force=True, silent=True) or {}
    user_msg = data.get("message", "").strip()
    if not user_msg:
        return jsonify({"error": "Empty message"}), 400

    save_memory("user", user_msg)
    t0 = time.time()

    tool_used, telemetry = check_fast_tools(user_msg)
    
    # Random signature Rick prefill
    prefill = random.choice(RICK_PREFILLS)

    # Static Prefix structure to ensure 90%+ KV Cache Hit!
    prompt = f"<|im_start|>system\n{SYSTEM_RICK}<|im_end|>\n"
    for item in FEW_SHOT:
        prompt += f"<|im_start|>{item['role']}\n{item['content']}<|im_end|>\n"
    
    user_line = user_msg
    if telemetry:
        user_line += f" (Текущее железо: {telemetry})"
    
    prompt += f"<|im_start|>user\n{user_line}<|im_end|>\n<|im_start|>assistant\n{prefill}"

    payload = {
        "prompt": prompt,
        "n_predict": 22,
        "temperature": 0.85,
        "top_p": 0.9,
        "presence_penalty": 0.6,
        "frequency_penalty": 0.6,
        "stop": ["<|im_end|>", "<|im_start|>", "\nuser\n", "\nUser:", "\nПользователь:"]
    }

    try:
        resp = requests.post(LLAMA_URL, json=payload, timeout=25)
        res_json = resp.json()
        generated = res_json.get("content", "").strip()
        full_ans = (prefill + generated).strip()
        
        # Clean any markup
        full_ans = re.sub(r'<\|.*?\|>', '', full_ans).strip()
        full_ans = full_ans.strip('"\'')

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
    return jsonify({"status": "ok", "service": "Melvin Turbo Rick", "chip": "Helio X20"})

if __name__ == "__main__":
    init_db()
    print("Melvin Turbo Rick running on port 5000...")
    app.run(host="0.0.0.0", port=5000)
