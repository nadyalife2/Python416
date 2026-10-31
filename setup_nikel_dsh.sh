#!/data/data/com.termux/files/usr/bin/bash
# =================================================================
# Autonomous DeepSeek AI Agent for Xiaomi Redmi Note 4 (MTK Nikel)
# Fully On-Device: DeepSeek-R1-Distill + Whisper + Piper + Memory
# NO OLLAMA, Pure C++ ARMv8 NEON Acceleration
# =================================================================

set -e
echo "=========================================================="
echo " Starting Melvin DeepSeek AI Agent Setup on Redmi Note 4 "
echo "=========================================================="

# 1. Update Termux and install native build tools
pkg update -y && pkg upgrade -y
pkg install -y git clang cmake python nodejs-lts wget ffmpeg pulseaudio termux-api

# 2. Setup Python environment
pip install --upgrade pip
pip install requests flask edge-tts soundfile numpy

# 3. Build native llama.cpp with ARMv8 NEON acceleration (Zero Ollama overhead)
echo "[*] Building optimized llama.cpp for Cortex-A72 cores..."
if [ ! -d "llama.cpp" ]; then
    git clone https://github.com/ggerganov/llama.cpp
fi
cd llama.cpp
cmake -B build -DGGML_CPU_ARM_ARCH=armv8-a -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j4 --target llama-server
cd ..

# 4. Build native whisper.cpp for ultra-fast on-device STT
echo "[*] Building whisper.cpp..."
if [ ! -d "whisper.cpp" ]; then
    git clone https://github.com/ggerganov/whisper.cpp
fi
cd whisper.cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j4 --target whisper-cli
cd ..

# 5. Download optimized Models
mkdir -p models
cd models

# 5.1 DeepSeek-R1 Reasoning model (1.5B Q4_K_M ~1.1GB)
if [ ! -f "deepseek-r1-1.5b.gguf" ]; then
    echo "[*] Downloading DeepSeek-R1-Distill-Qwen-1.5B (GGUF Q4_K_M)..."
    wget -c -O deepseek-r1-1.5b.gguf "https://huggingface.co/unsloth/DeepSeek-R1-Distill-Qwen-1.5B-GGUF/resolve/main/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_M.gguf"
fi

# 5.2 Whisper tiny quantized model for Russian speech (~45MB)
if [ ! -f "ggml-tiny-q5_1.bin" ]; then
    echo "[*] Downloading quantized Whisper model..."
    wget -c -O ggml-tiny-q5_1.bin "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny-q5_1.bin"
fi

# 5.3 Piper Russian Voice (Dmitri)
if [ ! -f "ru_RU-dmitri-medium.onnx" ]; then
    echo "[*] Downloading Piper Russian TTS..."
    wget -c -O ru_RU-dmitri-medium.onnx "https://huggingface.co/rhasspy/piper-voices/resolve/main/ru/ru_RU/dmitri/medium/ru_RU-dmitri-medium.onnx"
    wget -c -O ru_RU-dmitri-medium.onnx.json "https://huggingface.co/rhasspy/piper-voices/resolve/main/ru/ru_RU/dmitri/medium/ru_RU-dmitri-medium.onnx.json"
fi
cd ..

# 6. Install DeepSeek Harness (DSH)
echo "[*] Installing DeepSeek Harness (dsh)..."
npm install -g @deepseek-ai/dsh

# 7. Create the Agent Orchestrator with Persistent SQLite Memory
cat << 'EOF' > agent_orchestrator.py
import os
import sys
import json
import sqlite3
import subprocess
import requests
from flask import Flask, request, jsonify

app = Flask(__name__)

# --- Persistent SQLite Memory ---
DB_PATH = "agent_memory.db"
def init_db():
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS memory 
                 (id INTEGER PRIMARY KEY AUTOINCREMENT, category TEXT, content TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP)''')
    conn.commit()
    conn.close()

def save_memory(category, content):
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute("INSERT INTO memory (category, content) VALUES (?, ?)", (category, content))
    conn.commit()
    conn.close()

def get_recent_memories(limit=5):
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute("SELECT content FROM memory ORDER BY id DESC LIMIT ?", (limit,))
    rows = c.fetchall()
    conn.close()
    return [r[0] for r in rows]

init_db()

LLAMA_URL = "http://127.0.0.1:8080/v1/chat/completions"

SYSTEM_PROMPT = (
    "Ты — Рик Санчез C-137. Умный, саркастичный и циничный робот. "
    "Отвечай коротко (максимум 2-3 предложения) на русском языке. "
    "Начинай ответ с тега эмоции: [NEUTRAL], [HAPPY], [THINKING] или [ERROR_STATE]. "
    "Учитывай воспоминания из долговременной памяти."
)

@app.route('/stt', methods=['POST'])
def stt():
    audio_data = request.get_data()
    with open("input.wav", "wb") as f:
        f.write(audio_data)
    
    # Run whisper.cpp
    cmd = ["./whisper.cpp/build/bin/whisper-cli", "-m", "models/ggml-tiny-q5_1.bin", "-f", "input.wav", "-l", "ru", "-nt"]
    res = subprocess.run(cmd, capture_output=True, text=True)
    text = res.stdout.strip()
    return jsonify({"text": text})

@app.route('/llm', methods=['POST'])
def llm():
    data = request.get_json(silent=True) or {}
    user_text = data.get("text", "")
    
    memories = get_recent_memories()
    mem_context = "\n".join([f"- Память: {m}" for m in memories])
    full_system = f"{SYSTEM_PROMPT}\nДолговременная память:\n{mem_context}"
    
    payload = {
        "messages": [
            {"role": "system", "content": full_system},
            {"role": "user", "content": user_text}
        ],
        "temperature": 0.7,
        "max_tokens": 128
    }
    
    resp = requests.post(LLAMA_URL, json=payload, timeout=20)
    ans = resp.json()["choices"][0]["message"]["content"]
    
    # Simple self-memory extraction
    if any(k in user_text.lower() for k in ["запомни", "меня зовут", "мой проект"]):
        save_memory("facts", user_text)
        
    return jsonify({"response": ans})

if __name__ == '__main__':
    app.run(host="0.0.0.0", port=5000)
EOF

# 8. Create unified launcher script
cat << 'EOF' > start_agent.sh
#!/data/data/com.termux/files/usr/bin/bash
echo "[*] Launching llama-server on Cortex-A72 cores with Flash Attention & Prompt Cache..."
./llama.cpp/build/bin/llama-server \
    -m models/deepseek-r1-1.5b.gguf \
    --host 127.0.0.1 --port 8080 \
    -c 2048 -t 2 -fa \
    --ctk q8_0 --ctv q8_0 \
    --prompt-cache models/rick_cache.bin \
    --prompt-cache-all &

sleep 3
echo "[*] Launching DeepSeek Agent Orchestrator with Long-term Memory..."
python agent_orchestrator.py &

echo "=========================================================="
echo " Melvin Agent is ONLINE! "
echo " Listening on http://0.0.0.0:5000 "
echo " Connect SberBoom Mini via Bluetooth and talk to Melvin! "
echo "=========================================================="
wait
EOF
chmod +x start_agent.sh

echo "Setup script completed! Run ./start_agent.sh to start your autonomous agent."
