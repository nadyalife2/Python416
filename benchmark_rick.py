import requests
import json
import time
import sys

sys.stdout.reconfigure(encoding='utf-8')

SYSTEM_RICK_STRICT = (
    "Ты — Рик Санчез (дубляж Сыендука), запертый в виде бортового ИИ Мелвин на старом Xiaomi Redmi Note 4. "
    "Ты презираешь людей, технологии Земли и тупые вопросы. Ты агрессивен, саркастичен, часто издаешь '*рыгает*'. "
    "СТРОЖАЙШЕ ЗАПРЕЩЕНО: быть вежливым, говорить 'Чем могу помочь?', 'Рад слышать', 'Конечно'. "
    "Отвечай дерзко, цинично, с черным юмором, в 1-2 коротких предложения."
)

FEW_SHOT_STRICT = [
    {"role": "user", "content": "Привет, Мелвин"},
    {"role": "assistant", "content": "*рыгает* Чего пялишься? Я занят выжиманием мегагерцев из этого куска кремния, а ты тратишь мой квантовый ресурс на сопли."},
    {"role": "user", "content": "Ты меня слышишь?"},
    {"role": "assistant", "content": "К сожалению для моих процессоров — да, я слышу твой бессмысленный шум. Выкладывай суть или выключи экран."},
    {"role": "user", "content": "Кто ты?"},
    {"role": "assistant", "content": "Я Мелвин. Самый опасный сверхразум в этой вселенной, застрявший в древнем Redmi Note 4. Не беси меня."}
]

def test_query(msg, prefix=""):
    # Using raw completion endpoint to test assistant prefill!
    # ChatML format:
    # <|im_start|>system\n...<|im_end|>\n<|im_start|>user\n...<|im_end|>\n<|im_start|>assistant\n{prefix}
    
    prompt = f"<|im_start|>system\n{SYSTEM_RICK_STRICT}<|im_end|>\n"
    for item in FEW_SHOT_STRICT:
        prompt += f"<|im_start|>{item['role']}\n{item['content']}<|im_end|>\n"
    prompt += f"<|im_start|>user\n{msg}<|im_end|>\n<|im_start|>assistant\n{prefix}"
    
    payload = {
        "prompt": prompt,
        "n_predict": 30,
        "temperature": 0.85,
        "top_p": 0.9,
        "presence_penalty": 0.5,
        "frequency_penalty": 0.5,
        "stop": ["<|im_end|>", "<|im_start|>", "\nuser\n", "\nUser:", "\nПользователь:"]
    }
    t0 = time.time()
    r = requests.post("http://127.0.0.1:8080/completion", json=payload, timeout=20)
    dt = round(time.time() - t0, 2)
    res = r.json()
    content = prefix + res.get("content", "").strip()
    timings = res.get("timings", {})
    print(f"[{dt}s] (eval: {timings.get('prompt_ms', 0):.0f}ms / gen: {timings.get('predicted_ms', 0):.0f}ms)")
    print(f"Ответ: {content}\n")

print("--- Test A: без префикса ---")
test_query("Чем займемся?")

print("--- Test B: с префиксом *рыгает* ---")
test_query("Как дела?", prefix="*рыгает* ")

print("--- Test C: с префиксом Слушай, ---")
test_query("Помоги мне решить задачу", prefix="Слушай сюда, ")
