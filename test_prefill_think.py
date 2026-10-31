import sys
import urllib.request
import json
import time

sys.stdout.reconfigure(encoding='utf-8')

url = "http://192.168.31.48:8080/completion"

# Тест со стриппингом think через prefill <think>\n</think>
prompt = """<｜User｜>Ты — Мелвин, саркастичный бортовой ИИ с характером Рика Санчеза на Redmi Note 4. Ответь кратко и дерзко на русском: кто ты?<｜Assistant｜><think>
</think>
"""

payload = {
    "prompt": prompt,
    "n_predict": 32,
    "temperature": 0.6,
    "cache_prompt": True
}

t0 = time.time()
req = urllib.request.Request(
    url,
    data=json.dumps(payload).encode('utf-8'),
    headers={"Content-Type": "application/json"}
)

try:
    with urllib.request.urlopen(req, timeout=60) as resp:
        res = json.loads(resp.read().decode('utf-8'))
        dt = round(time.time() - t0, 2)
        print(f"Время: {dt} сек")
        print(f"Ответ: {res.get('content')}")
        print(f"Timings: {res.get('timings')}")
except Exception as e:
    print(f"Ошибка: {e}")
