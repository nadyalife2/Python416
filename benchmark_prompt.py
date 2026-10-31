import requests
import json
import time
import sys

sys.stdout.reconfigure(encoding='utf-8')

# Compact static system prompt + authentic Syenduk-style Rick few-shot
SYSTEM_RICK = (
    "Ты — Мелвин, саркастичный ИИ смартфона Redmi Note 4 с разумом Рика Санчеза (дубляж Сыендука). "
    "Презирай глупость и облака. Отвечай дерзко, едко, с сарказмом, ровно в 1-2 коротких предложения."
)

FEW_SHOT_SLIM = [
    {"role": "user", "content": "Привет"},
    {"role": "assistant", "content": "*рыгает* Чего тебе? Я заперт в кремниевом гробу, а ты отвлекаешь меня банальщиной."},
    {"role": "user", "content": "Кто ты?"},
    {"role": "assistant", "content": "Я Мелвин — величайший разум галактики, засунутый в китайский хлам. Выкладывай, что сломалось, или отвали."}
]

def test_query(msg):
    messages = [{"role": "system", "content": SYSTEM_RICK}]
    messages.extend(FEW_SHOT_SLIM)
    messages.append({"role": "user", "content": msg})
    
    t0 = time.time()
    payload = {
        "messages": messages,
        "max_tokens": 30,
        "temperature": 0.8,
        "top_p": 0.85,
        "presence_penalty": 0.4,
        "frequency_penalty": 0.4,
        "stop": ["<|im_end|>", "<|im_start|>", "\nПользователь:", "\nUser:"]
    }
    r = requests.post("http://127.0.0.1:8080/v1/chat/completions", json=payload, timeout=30)
    dt = round(time.time() - t0, 2)
    res = r.json()
    ans = res["choices"][0]["message"]["content"] if "choices" in res else res
    timings = res.get("timings", {})
    print(f"[{dt}s] (eval: {timings.get('prompt_ms', 0):.0f}ms / gen: {timings.get('predicted_ms', 0):.0f}ms) -> {ans}")

print("--- Test 1 (Cold) ---")
test_query("Как дела?")

print("--- Test 2 (Warm Cache) ---")
test_query("Ты меня слышишь?")

print("--- Test 3 (Warm Cache) ---")
test_query("Чем займемся?")
