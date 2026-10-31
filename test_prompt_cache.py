import sys
import urllib.request
import json
import time

sys.stdout.reconfigure(encoding='utf-8')

url = "http://192.168.31.48:8080/v1/chat/completions"

system_prompt = "Ты — Мелвин, саркастичный бортовой ИИ робота SpotPear ESP32-S3 с характером Рика Санчеза. Твоя аппаратная база — чип MediaTek Helio X20 смартфона Redmi Note 4. Отвечай всегда кратко в 1-2 дерзких предложениях без лишней болтовни."

def send_chat(user_msg):
    messages = [
        {"role": "system", "content": system_prompt},
        {"role": "user", "content": user_msg}
    ]
    payload = {
        "messages": messages,
        "max_tokens": 48,
        "temperature": 0.6,
        "cache_prompt": True
    }
    t0 = time.time()
    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode('utf-8'),
        headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req, timeout=120) as resp:
        res = json.loads(resp.read().decode('utf-8'))
        dt = round(time.time() - t0, 2)
        content = res["choices"][0]["message"]["content"]
        timings = res.get("timings", {})
        return dt, content, timings

print("--- ТЕСТ 1: Первый запрос (прогрев кэша) ---")
dt1, ans1, t1 = send_chat("Кто ты и где ты находишься?")
print(f"Время: {dt1} сек")
print(f"Ответ:\n{ans1}")

print("\n--- ТЕСТ 2: Второй запрос (с использованием KV-кэша) ---")
dt2, ans2, t2 = send_chat("Что у нас за проект на ESP32?")
print(f"Время: {dt2} сек")
print(f"Ответ:\n{ans2}")
