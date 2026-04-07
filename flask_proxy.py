# ================================================================
# MELVIN PROXY SERVER
# Deploy to PythonAnywhere (free tier)
# 
# API keys are stored on ESP32's SD card (config.txt)
# This proxy just forwards requests — it does NOT store any keys.
#
# Endpoints:
#   POST /stt  -> Whisper STT (HuggingFace)
#   POST /tts  -> MMS TTS (HuggingFace)
#   POST /llm  -> Groq LLM
#   GET  /ping -> Health check
# ================================================================

from flask import Flask, request, Response
import requests

app = Flask(__name__)

STT_URL = "https://router.huggingface.co/hf-inference/models/openai/whisper-large-v3-turbo"
TTS_URL = "https://router.huggingface.co/hf-inference/models/facebook/mms-tts-rus"
LLM_URL = "https://api.groq.com/openai/v1/chat/completions"

TIMEOUT_STT = 30
TIMEOUT_TTS = 25
TIMEOUT_LLM = 20


def _forward(target_url, timeout):
    """Forward request to target, passing Authorization and Content-Type from ESP32."""
    headers = {}
    for key in ("Authorization", "Content-Type", "User-Agent"):
        val = request.headers.get(key)
        if val:
            headers[key] = val

    try:
        resp = requests.request(
            method=request.method,
            url=target_url,
            headers=headers,
            data=request.get_data(),
            timeout=timeout,
        )
        return Response(
            resp.content,
            status=resp.status_code,
            content_type=resp.headers.get("Content-Type", "application/octet-stream"),
        )
    except requests.exceptions.Timeout:
        return Response('{"error":"proxy timeout"}', status=504, content_type="application/json")
    except Exception as e:
        return Response('{"error":"' + str(e) + '"}', status=502, content_type="application/json")


@app.route("/stt", methods=["POST"])
def stt():
    return _forward(STT_URL, TIMEOUT_STT)


@app.route("/tts", methods=["POST"])
def tts():
    return _forward(TTS_URL, TIMEOUT_TTS)


@app.route("/llm", methods=["POST"])
def llm():
    return _forward(LLM_URL, TIMEOUT_LLM)


@app.route("/ping", methods=["GET"])
def ping():
    return Response('{"status":"ok","server":"melvin-proxy"}', content_type="application/json")


# PythonAnywhere использует WSGI — объект app импортируется напрямую.
# Локальный запуск: python flask_proxy.py
if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8080, debug=False)
