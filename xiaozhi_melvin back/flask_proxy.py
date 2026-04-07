from flask import Flask, request, Response
import requests

app = Flask(__name__)

# =================================================================
# MELVIN PROXY v1.1 - Groq + OpenRouter + HuggingFace
# Deploy on PythonAnywhere, set WSGI to point to this file.
# config.txt line 3: https://melvinxiaoshi.pythonanywhere.com
# =================================================================

GROQ_URL = 'https://api.groq.com/openai/v1/chat/completions'
OR_URL   = 'https://openrouter.ai/api/v1/chat/completions'
STT_URL  = 'https://router.huggingface.co/hf-inference/models/openai/whisper-large-v3-turbo'
TTS_URL  = 'https://router.huggingface.co/hf-inference/models/facebook/mms-tts-rus'


def _forward(target_url, timeout=30):
    """Forward current request to target_url, strip Host header."""
    headers = {k: v for k, v in request.headers if k.lower() != 'host'}
    resp = requests.request(
        method=request.method,
        url=target_url,
        headers=headers,
        data=request.get_data(),
        timeout=timeout
    )
    return Response(resp.content, status=resp.status_code,
                    content_type=resp.headers.get('Content-Type', 'application/octet-stream'))


@app.route('/ping')
def ping():
    return 'pong'


# --- LLM via Groq ---
@app.route('/llm', methods=['POST'])
def llm():
    return _forward(GROQ_URL, timeout=20)


# --- LLM via OpenRouter (same OpenAI-compatible format) ---
@app.route('/llm-or', methods=['POST'])
def llm_or():
    return _forward(OR_URL, timeout=25)


# --- STT: Whisper via HuggingFace ---
@app.route('/stt', methods=['POST'])
def stt():
    return _forward(STT_URL, timeout=30)


# --- TTS: MMS-TTS-RUS via HuggingFace ---
@app.route('/tts', methods=['POST'])
def tts():
    return _forward(TTS_URL, timeout=25)


if __name__ == '__main__':
    app.run(debug=True)
