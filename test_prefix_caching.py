import sys
import urllib.request
import json
import time

sys.stdout.reconfigure(encoding='utf-8')

url = "http://192.168.31.48:8080/v1/chat/completions"

# 100% СТАТИЧНЫЙ СИСТЕМНЫЙ ПРОМПТ (всегда один и тот же для 100% попадания в KV-кэш!)
STATIC_SYSTEM = "Ты — Мелвин, саркастичный бортовой ИИ робота SpotPear ESP32-S3 с характером Рика Санчеза. Твой процессор — Helio X20. Отвечай кратко в 1 предложение на русском языке."

def ask(query_text, max_tokens=24):
    messages = [
        {"role": "system", "content": STATIC_SYSTEM},
        {"role": "user", "content": query_text}
    ]
    payload = {
        "messages": messages,
        "max_tokens": max_tokens,
        "temperature": 0.5,
        "cache_prompt": True
    }
    t0 = time.time()
    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode('utf-8'),
        headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req, timeout=60) as resp:
        res = json.loads(resp.read().decode('utf-8'))
        dt = round(time.time() - t0, 2)
        content = res["choices"][0]["message"].get("content") or res["choices"][0]["message"].get("reasoning_content") or ""
        timings = res.get("timings", {})
        return dt, content, timings

print("Запрос 1 (прогрев статического префикса)...")
dt1, ans1, t1 = ask("Привет! Назови свой чип.")
print(f"[{dt1}s] Ответ: {ans1.strip()}")
print(f"Timings 1: prompt_n={t1.get('prompt_n')}, cache_n={t1.get('cache_n')}\n")

print("Запрос 2 (горячий кэш KV, только дельта)...")
dt2, ans2, t2 = ask("Помнишь проект на ESP32?")
print(f"[{dt2}s] Ответ: {ans2.strip()}")
print(f"Timings 2: prompt_n={t2.get('prompt_n')}, cache_n={t2.get('cache_n')}\n")
