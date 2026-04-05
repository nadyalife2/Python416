import os
import json
import base64
import asyncio
import logging
from datetime import datetime
from dotenv import load_dotenv
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from openai import AsyncOpenAI
import edge_tts
import aiohttp

# Load environment variables
load_dotenv()

# API Keys (Fallback to current project values if not in .env)
HF_API_KEY = os.getenv("HF_API_KEY", "PLACE_YOUR_HF_KEY_HERE")
OR_API_KEY = os.getenv("OR_API_KEY", "PLACE_YOUR_OR_KEY_HERE")

# API URLs
HF_STT_URL = "https://router.huggingface.co/models/openai/whisper-large-v3-turbo"
OR_URL = "https://openrouter.ai/api/v1/chat/completions"
OR_MODEL = "meta-llama/llama-3.1-8b-instruct:free"

SYSTEM_PROMPT = (
    "Ты — Рик Санчес C-137. Будь грубым, саркастичным, используй научный жаргон. "
    "Отвечай по-русски, максимум 2-3 предложения. "
    "ОБЯЗАТЕЛЬНО возвращай ответ в формате JSON: "
    "{\"text\": \"твой ответ\", \"emotion\": \"одна из: NEUTRAL, HAPPY, THINKING, ERROR_STATE\"}"
)

# Logger setup
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("MelvinServer")

app = FastAPI()

class MelvinOrchestrator:
    def __init__(self):
        self.or_client = AsyncOpenAI(api_key=OR_API_KEY, base_url="https://openrouter.ai/api/v1")

    async def transcribe(self, audio_data: bytes):
        """Perform STT using Hugging Face Whisper"""
        headers = {"Authorization": f"Bearer {HF_API_KEY}", "Content-Type": "audio/wav"}
        async with aiohttp.ClientSession() as session:
            async with session.post(HF_STT_URL, headers=headers, data=audio_data) as resp:
                if resp.status == 200:
                    result = await resp.json()
                    return result.get("text", "").strip()
                else:
                    logger.error(f"STT Error: {resp.status} - {await resp.text()}")
                    return ""

    async def get_response(self, user_text: str):
        """Get LLM response from OpenRouter with Rick Sanchez personality"""
        try:
            response = await self.or_client.chat.completions.create(
                model=OR_MODEL,
                messages=[
                    {"role": "system", "content": SYSTEM_PROMPT},
                    {"role": "user", "content": user_text}
                ],
                response_format={"type": "json_object"}
            )
            content = response.choices[0].message.content
            return json.loads(content)
        except Exception as e:
            logger.error(f"LLM Error: {e}")
            return {"text": "Черт возьми, Морти, сервер упал!", "emotion": "ERROR_STATE"}

    async def synthesize(self, text: str):
        """Perform TTS using Edge-TTS (very fast)"""
        communicate = edge_tts.Communicate(text, "ru-RU-DmitryNeural")
        # Save to memory instead of file for speed
        audio_data = b""
        async for chunk in communicate.stream():
            if chunk["type"] == "audio":
                audio_data += chunk["data"]
        return audio_data

orchestrator = MelvinOrchestrator()

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    logger.info(f"Client connected: {websocket.client}")
    
    try:
        while True:
            # Receive data (JSON for control or Binary for audio)
            message = await websocket.receive()
            
            if "bytes" in message:
                audio_chunks = message["bytes"]
                logger.info(f"Received audio: {len(audio_chunks)} bytes")
                
                # 1. Start STT
                user_text = await orchestrator.transcribe(audio_chunks)
                if not user_text:
                    await websocket.send_json({"type": "error", "message": "No speech detected"})
                    continue
                
                logger.info(f"User: {user_text}")
                await websocket.send_json({"type": "transcription", "text": user_text})
                
                # 2. Start LLM
                rick_resp = await orchestrator.get_response(user_text)
                logger.info(f"Rick: {rick_resp}")
                await websocket.send_json({"type": "response", "text": rick_resp["text"], "emotion": rick_resp["emotion"]})
                
                # 3. Start TTS
                tts_audio = await orchestrator.synthesize(rick_resp["text"])
                logger.info(f"TTS generated: {len(tts_audio)} bytes")
                
                # 4. Send Audio back
                # Split audio into chunks for streaming feel
                chunk_size = 4096
                for i in range(0, len(tts_audio), chunk_size):
                    await websocket.send_bytes(tts_audio[i:i+chunk_size])
                
                # Signal end of audio
                await websocket.send_json({"type": "audio_end"})
                    
            elif "text" in message:
                data = json.loads(message["text"])
                logger.info(f"Received JSON: {data}")
                if data.get("type") == "hello":
                    await websocket.send_json({"type": "welcome", "message": "Wubba Lubba Dub Dub!"})

    except WebSocketDisconnect:
        logger.info("Client disconnected")
    except Exception as e:
        logger.error(f"WS Error: {e}")

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
