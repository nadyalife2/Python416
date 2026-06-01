#ifndef AGENT_H
#define AGENT_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include "Config.h"
#include <mbedtls/base64.h>

class MelvinAgent {
public:
    // --- Infinite Memory (SD-Backed) ---
    void appendToHistory(String role, String text) {
        File file = SD_MMC.open("/history.json", FILE_APPEND);
        if (!file) {
            file = SD_MMC.open("/history.json", FILE_WRITE);
            file.print("[");
        } else {
            file.print(",");
        }
        
        JsonDocument doc;
        doc["role"] = role;
        doc["content"] = text;
        serializeJson(doc, file);
        file.close();
    }

    String getRecentHistory(size_t maxBytes = 2048) {
        if (!SD_MMC.exists("/history.json")) return "";
        File file = SD_MMC.open("/history.json", FILE_READ);
        size_t size = file.size();
        if (size == 0) { file.close(); return ""; }

        size_t offset = (size > maxBytes) ? (size - maxBytes) : 0;
        file.seek(offset);
        String lastChunk = file.readString();
        file.close();

        // Find the first valid JSON object start in the chunk to avoid partial objects
        int firstBrace = lastChunk.indexOf('{');
        if (firstBrace != -1) return lastChunk.substring(firstBrace);
        return "";
    }

    // --- RSS News Tool ---
    String getRSSHeadlines(String url) {
        if (url.length() < 5) return "No news configured.";
        HTTPClient http;
        http.begin(url);
        int httpCode = http.GET();
        String headlines = "";
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            int count = 0, pos = 0;
            while ((pos = payload.indexOf("<title>", pos)) != -1 && count < 5) {
                int end = payload.indexOf("</title>", pos);
                if (end != -1) {
                    String title = payload.substring(pos + 7, end);
                    if (title.indexOf("Lenta") == -1) {
                        headlines += "- " + title + "\n";
                        count++;
                    }
                    pos = end;
                } else break;
            }
        }
        http.end();
        return headlines;
    }

    // --- Resilient LLM Cascade ---
    String askAI(const String& audioPath, const MelvinConfig& cfg) {
        String keys = (cfg.llm_provider == "gemini") ? cfg.gemini_keys : 
                     (cfg.llm_provider == "groq") ? cfg.groq_keys : cfg.openrouter_keys;
        
        if (keys.length() < 5) return "Error: No API Keys configured for " + cfg.llm_provider;

        String news = getRSSHeadlines(cfg.rss_url);
        String historyContext = getRecentHistory();

        int start = 0;
        int end = keys.indexOf(',');
        while (true) {
            String currentKey = (end == -1) ? keys.substring(start) : keys.substring(start, end);
            currentKey.trim();

            String result = callProviderAPI(audioPath, news, historyContext, cfg, currentKey);
            if (!result.startsWith("Error API")) {
                appendToHistory("user", "[Voice Input]");
                appendToHistory("model", result);
                return result;
            }

            if (end == -1) break;
            start = end + 1;
            end = keys.indexOf(',', start);
            Serial.println("[AGENT] Key failed, trying next in cascade...");
        }
        return "Error: All keys failed for " + cfg.llm_provider;
    }

private:
    String callProviderAPI(const String& audioPath, const String& news, const String& history, const MelvinConfig& cfg, String key) {
        // Multimodal logic for Gemini
        if (cfg.llm_provider == "gemini") {
            return callGemini(audioPath, news, history, cfg, key);
        }
        // Fallback for text-only providers (would need STT first)
        return "Error: " + cfg.llm_provider + " audio input not yet implemented.";
    }

    String callGemini(const String& audioPath, const String& news, const String& history, const MelvinConfig& cfg, String key) {
        File file = SD_MMC.open(audioPath, FILE_READ);
        if (!file) return "Error: Audio file missing";
        size_t fileSize = file.size();
        Serial.printf("[AGENT] WAV size: %d bytes\n", fileSize);
        
        if (fileSize < 44) { file.close(); return "Error: WAV too small"; }
        
        uint8_t* buf = (uint8_t*)malloc(fileSize);
        if (!buf) { file.close(); return "Error: Memory full"; }
        file.read(buf, fileSize);
        file.close();
        // mbedtls base64 encode (встроено в ESP-IDF, нет конфликтов)
        size_t b64Len = 0;
        mbedtls_base64_encode(nullptr, 0, &b64Len, buf, fileSize); // узнаём размер
        uint8_t* b64Buf = (uint8_t*)malloc(b64Len + 1);
        if (!b64Buf) { free(buf); return "Error: Memory full (b64)"; }
        mbedtls_base64_encode(b64Buf, b64Len + 1, &b64Len, buf, fileSize);
        b64Buf[b64Len] = 0;
        String base64Audio = String((char*)b64Buf);
        free(b64Buf);
        free(buf);
        Serial.printf("[AGENT] Base64 size: %d chars\n", base64Audio.length());

        WiFiClientSecure client; client.setInsecure();
        HTTPClient http;
        http.setTimeout(30000); // 30 сек таймаут для большого аудио
        http.begin(client, "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key=" + key);
        http.addHeader("Content-Type", "application/json");

        JsonDocument doc;
        JsonObject content = doc["contents"].add<JsonObject>();
        content["role"] = "user";
        JsonArray parts = content["parts"].to<JsonArray>();
        
        String prompt = cfg.getEffectivePrompt() + "\n\nNEWS:\n" + news + "\n\nRECENT CONTEXT:\n" + history;
        parts.add<JsonObject>()["text"] = prompt;
        
        JsonObject audioPart = parts.add<JsonObject>();
        audioPart["inline_data"]["mime_type"] = "audio/wav";
        audioPart["inline_data"]["data"] = base64Audio;

        String body; serializeJson(doc, body);
        Serial.printf("[AGENT] POST body size: %d bytes → Gemini...\n", body.length());
        int code = http.POST(body);
        Serial.printf("[AGENT] HTTP response: %d\n", code);
        
        if (code == 200) {
            String resp = http.getString();
            Serial.printf("[AGENT] Response preview: %.200s\n", resp.c_str());
            JsonDocument res; deserializeJson(res, resp);
            String answer = res["candidates"][0]["content"]["parts"][0]["text"].as<String>();
            Serial.printf("[AGENT] AI Answer: %s\n", answer.c_str());
            http.end();
            return answer;
        }
        String errBody = http.getString();
        Serial.printf("[AGENT] Error body: %.300s\n", errBody.c_str());
        http.end();
        return "Error API " + String(code);
    }
};

#endif
