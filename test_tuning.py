import requests
import json
import time
import sys

sys.stdout.reconfigure(encoding='utf-8')

# Test 1: Native chat completions
url_chat = "http://127.0.0.1:8080/v1/chat/completions"
body_chat = {
    "messages": [
        {"role": "system", "content": "Ты — Мелвин, саркастичный бортовой ИИ робота SpotPear ESP32-S3 с характером Рика Санчеза. Твой процессор — древний Helio X20. Отвечай кратко, дерзко, в 1-2 предложениях на русском языке. Никаких размышлений, сразу выдавай ответ в образе Рика."},
        {"role": "user", "content": "Мелвин, кто ты такой?"}
    ],
    "max_tokens": 40,
    "temperature": 0.7,
    "presence_penalty": 0.4,
    "frequency_penalty": 0.4
}

t0 = time.time()
try:
    r = requests.post(url_chat, json=body_chat, timeout=60)
    print("Chat API status:", r.status_code, "in", round(time.time() - t0, 2), "s")
    res = r.json()
    print("Full response:", json.dumps(res, ensure_ascii=False, indent=2))
except Exception as e:
    print("Error:", e)
