#ifndef AGENT_H
#define AGENT_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include "Config.h"
#include "MelvinState.h"
#include "Display.h"
#include "RabbitFace.h"
#include <mbedtls/base64.h>

// Forward declarations for display redrawing during network transfer
extern uint32_t lastRedrawMs;
extern RobotState currentState;
extern uint32_t animTick;
extern LGFX_Sprite canvas;

// ============================================================
// Zero-Copy Streaming Class for Gemini POST body
// ============================================================
class GeminiStream : public Stream {
private:
    File file;
    String jsonStart;
    String jsonEnd;
    size_t fileSize = 0;
    size_t streamPos = 0;
    size_t totalLength = 0;
    
    // Temporary buffer for base64 encoding
    uint8_t rawBuf[3];
    char b64Buf[4];
    int b64CacheLen = 0;
    int b64CacheIdx = 0;

    size_t readBytesImpl(uint8_t* buffer, size_t length) {
        // Redraw screen periodically to prevent visual freezes
        uint32_t now = millis();
        if (now - lastRedrawMs >= 80) {
            animTick++;
            drawFace(canvas, currentState, animTick);
            canvas.pushSprite(0, 0);
            lastRedrawMs = now;
        }

        size_t bytesRead = 0;
        while (bytesRead < length && streamPos < totalLength) {
            // 1. JSON Start
            if (streamPos < jsonStart.length()) {
                size_t chunk = jsonStart.length() - streamPos;
                if (chunk > length - bytesRead) chunk = length - bytesRead;
                memcpy(buffer + bytesRead, jsonStart.c_str() + streamPos, chunk);
                streamPos += chunk;
                bytesRead += chunk;
                continue;
            }
            // 2. Base64 encoded audio
            size_t b64Start = jsonStart.length();
            size_t b64End = b64Start + (((fileSize + 2) / 3) * 4);
            if (streamPos < b64End) {
                if (b64CacheIdx < b64CacheLen) {
                    buffer[bytesRead++] = b64Buf[b64CacheIdx++];
                    streamPos++;
                    continue;
                }
                size_t readBytes = file.read(rawBuf, 3);
                if (readBytes > 0) {
                    size_t encLen = 0;
                    mbedtls_base64_encode((unsigned char*)b64Buf, 4, &encLen, rawBuf, readBytes);
                    b64CacheLen = encLen;
                    b64CacheIdx = 0;
                    buffer[bytesRead++] = b64Buf[b64CacheIdx++];
                    streamPos++;
                } else {
                    break;
                }
                continue;
            }
            // 3. JSON End
            if (streamPos < totalLength) {
                size_t offset = streamPos - b64End;
                size_t chunk = jsonEnd.length() - offset;
                if (chunk > length - bytesRead) chunk = length - bytesRead;
                memcpy(buffer + bytesRead, jsonEnd.c_str() + offset, chunk);
                streamPos += chunk;
                bytesRead += chunk;
            }
        }
        return bytesRead;
    }

public:
    GeminiStream(File f, const String& start, const String& end) 
        : file(f), jsonStart(start), jsonEnd(end) {
        fileSize = file.size();
        size_t b64Len = ((fileSize + 2) / 3) * 4;
        totalLength = jsonStart.length() + b64Len + jsonEnd.length();
    }

    int available() override {
        return totalLength - streamPos;
    }

    int read() override {
        uint8_t c;
        if (readBytesImpl(&c, 1) == 1) return c;
        return -1;
    }

    size_t readBytes(char *buffer, size_t length) override {
        return readBytesImpl((uint8_t*)buffer, length);
    }
    size_t readBytes(uint8_t *buffer, size_t length) override {
        return readBytesImpl(buffer, length);
    }

    int peek() override { return -1; }
    size_t write(uint8_t) override { return 0; }
};

// ============================================================
// Zero-Copy Streaming Class for Groq Whisper Multi-part POST
// ============================================================
class MultipartStream : public Stream {
private:
    File file;
    String header;
    String footer;
    size_t fileSize = 0;
    size_t streamPos = 0;
    size_t totalLen = 0;

public:
    MultipartStream(File f, const String& h, const String& ft) 
        : file(f), header(h), footer(ft) {
        fileSize = file.size();
        totalLen = header.length() + fileSize + footer.length();
    }

    int available() override {
        return totalLen - streamPos;
    }

    int read() override {
        // Redraw screen periodically to prevent visual freezes
        uint32_t now = millis();
        if (now - lastRedrawMs >= 80) {
            animTick++;
            drawFace(canvas, currentState, animTick);
            canvas.pushSprite(0, 0);
            lastRedrawMs = now;
        }

        if (streamPos < header.length()) {
            return header[streamPos++];
        }
        size_t fileEnd = header.length() + fileSize;
        if (streamPos < fileEnd) {
            streamPos++;
            return file.read();
        }
        if (streamPos < totalLen) {
            return footer[streamPos++ - fileEnd];
        }
        return -1;
    }

    int peek() override { return -1; }
    size_t write(uint8_t) override { return 0; }
};

class MelvinAgent {
public:
    // --- Infinite Memory (SD-Backed JSONL Format) ---
    void appendToHistory(String role, String text) {
        File file = SD_MMC.open("/history.jsonl", FILE_APPEND);
        if (!file) {
            file = SD_MMC.open("/history.jsonl", FILE_WRITE);
        }
        if (file) {
            JsonDocument doc;
            doc["role"] = role;
            doc["content"] = text;
            serializeJson(doc, file);
            file.println();
            file.close();
        }
    }

    String getRecentHistory(size_t maxBytes = 2048) {
        if (!SD_MMC.exists("/history.jsonl")) return "";
        File file = SD_MMC.open("/history.jsonl", FILE_READ);
        size_t size = file.size();
        if (size == 0) { file.close(); return ""; }

        size_t offset = (size > maxBytes) ? (size - maxBytes) : 0;
        file.seek(offset);
        String lastChunk = file.readString();
        file.close();

        int firstNL = lastChunk.indexOf('\n');
        if (firstNL != -1 && firstNL + 1 < (int)lastChunk.length()) {
            String aligned = lastChunk.substring(firstNL + 1);
            String formatted = "[";
            int pos = 0;
            bool first = true;
            while (pos < (int)aligned.length()) {
                int nextNL = aligned.indexOf('\n', pos);
                if (nextNL == -1) {
                    String line = aligned.substring(pos);
                    line.trim();
                    if (line.length() > 2) {
                        if (!first) formatted += ",";
                        formatted += line;
                    }
                    break;
                }
                String line = aligned.substring(pos, nextNL);
                line.trim();
                if (line.length() > 2) {
                    if (!first) formatted += ",";
                    formatted += line;
                    first = false;
                }
                pos = nextNL + 1;
            }
            formatted += "]";
            return formatted;
        }
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

    // --- Resilient LLM Cascade (Gemini -> Groq -> OpenRouter) ---
    String askAI(const String& audioPath, const MelvinConfig& cfg) {
        String news = getRSSHeadlines(cfg.rss_url);
        String historyContext = getRecentHistory();
        String prompt = cfg.getEffectivePrompt() + "\n\nNEWS:\n" + news + "\n\nRECENT CONTEXT:\n" + historyContext;

        String primaryProvider = cfg.llm_provider;
        String primaryKeys = (primaryProvider == "gemini") ? cfg.gemini_keys : 
                             (primaryProvider == "groq") ? cfg.groq_keys : cfg.openrouter_keys;

        String answer = tryProvider(audioPath, prompt, primaryProvider, primaryKeys, cfg);
        if (!answer.startsWith("Error")) {
            appendToHistory("user", "[Voice Input]");
            appendToHistory("model", answer);
            return answer;
        }

        Serial.printf("[AGENT] Primary provider %s failed. Trying fallbacks...\n", primaryProvider.c_str());

        String fallbackProviders[] = { "gemini", "groq", "openrouter" };
        for (const String& provider : fallbackProviders) {
            if (provider == primaryProvider) continue;

            String keys = (provider == "gemini") ? cfg.gemini_keys : 
                          (provider == "groq") ? cfg.groq_keys : cfg.openrouter_keys;

            if (keys.length() < 5) {
                Serial.printf("[AGENT] Fallback provider %s has no keys configured. Skipping.\n", provider.c_str());
                continue;
            }

            Serial.printf("[AGENT] Attempting fallback provider: %s\n", provider.c_str());
            answer = tryProvider(audioPath, prompt, provider, keys, cfg);
            if (!answer.startsWith("Error")) {
                appendToHistory("user", "[Voice Input]");
                appendToHistory("model", answer);
                return answer;
            }
        }

        return "Error: All keys and fallback providers failed.";
    }

private:
    String tryProvider(const String& audioPath, const String& prompt, const String& provider, const String& keys, const MelvinConfig& cfg) {
        int start = 0;
        int end = keys.indexOf(',');
        while (true) {
            String currentKey = (end == -1) ? keys.substring(start) : keys.substring(start, end);
            currentKey.trim();

            String result = callSingleAPI(audioPath, prompt, provider, currentKey, cfg);
            if (!result.startsWith("Error")) {
                return result;
            }

            if (end == -1) break;
            start = end + 1;
            end = keys.indexOf(',', start);
            Serial.println("[AGENT] Key failed, trying next key in cascade...");
        }
        return "Error: All keys failed for " + provider;
    }

    String callSingleAPI(const String& audioPath, const String& prompt, const String& provider, const String& key, const MelvinConfig& cfg) {
        if (provider == "gemini") {
            return callGemini(audioPath, prompt, key);
        } else if (provider == "groq") {
            String text = transcribeGroqWhisper(audioPath, key);
            if (text.startsWith("Error")) return text;
            Serial.printf("[AGENT] Transcribed text: %s\n", text.c_str());
            String fullPrompt = prompt + "\n\nUser said: " + text;
            return callGroqChat(fullPrompt, key);
        } else if (provider == "openrouter") {
            String groqKeys = cfg.groq_keys;
            String groqKey = "";
            if (groqKeys.length() > 5) {
                int firstComma = groqKeys.indexOf(',');
                groqKey = (firstComma == -1) ? groqKeys : groqKeys.substring(0, firstComma);
                groqKey.trim();
            }
            if (groqKey.length() < 5) {
                return "Error: OpenRouter requires Groq Whisper key for STT fallback, but none configured.";
            }
            String text = transcribeGroqWhisper(audioPath, groqKey);
            if (text.startsWith("Error")) return text;
            Serial.printf("[AGENT] Transcribed text for OpenRouter: %s\n", text.c_str());
            String fullPrompt = prompt + "\n\nUser said: " + text;
            return callOpenRouterChat(fullPrompt, key);
        }
        return "Error: Unsupported provider: " + provider;
    }

    // --- GEMINI CLIENT (Stream-Based to avoid SRAM exhaustion) ---
    String callGemini(const String& audioPath, const String& prompt, String key) {
        File file = SD_MMC.open(audioPath, FILE_READ);
        if (!file) return "Error: Audio file missing";
        size_t fileSize = file.size();
        Serial.printf("[AGENT] WAV size: %d bytes\n", fileSize);
        
        if (fileSize < 44) { file.close(); return "Error: WAV too small"; }
        
        size_t b64Len = ((fileSize + 2) / 3) * 4;
        
        JsonDocument headerDoc;
        headerDoc["contents"][0]["role"] = "user";
        headerDoc["contents"][0]["parts"][0]["text"] = prompt;
        String headerStr;
        serializeJson(headerDoc, headerStr);
        
        int insertPos = headerStr.lastIndexOf("}]}");
        if (insertPos == -1) {
            file.close();
            return "Error: JSON header format invalid";
        }
        String jsonStart = headerStr.substring(0, insertPos) + ",{\"inline_data\":{\"mime_type\":\"audio/wav\",\"data\":\"";
        String jsonEnd = "\"}}]}";
        
        size_t totalLength = jsonStart.length() + b64Len + jsonEnd.length();
        Serial.printf("[AGENT] Streamed POST size: %d bytes, free PSRAM: %d\n", 
                      totalLength, heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.setTimeout(30000); 
        
        http.begin(client, "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key=" + key);
        http.addHeader("Content-Type", "application/json");
        
        GeminiStream gStream(file, jsonStart, jsonEnd);
        
        int code = http.sendRequest("POST", &gStream, totalLength);
        file.close();
        
        Serial.printf("[AGENT] HTTP response: %d\n", code);
        
        if (code == 200) {
            String resp = http.getString();
            Serial.printf("[AGENT] Response preview: %.200s\n", resp.c_str());
            JsonDocument res; 
            DeserializationError err = deserializeJson(res, resp);
            if (err == DeserializationError::Ok) {
                if (res["candidates"] && res["candidates"][0] && 
                    res["candidates"][0]["content"] && 
                    res["candidates"][0]["content"]["parts"] && 
                    res["candidates"][0]["content"]["parts"][0]) {
                    String answer = res["candidates"][0]["content"]["parts"][0]["text"].as<String>();
                    Serial.printf("[AGENT] AI Answer: %s\n", answer.c_str());
                    http.end();
                    return answer;
                }
            }
            http.end();
            return "Error API parsed response invalid";
        }
        String errBody = http.getString();
        Serial.printf("[AGENT] Error body: %.300s\n", errBody.c_str());
        http.end();
        return "Error API " + String(code);
    }

    // --- GROQ WHISPER STT CLIENT (Stream-Based multipart) ---
    String transcribeGroqWhisper(const String& audioPath, const String& key) {
        File file = SD_MMC.open(audioPath, FILE_READ);
        if (!file) return "Error: Open audio failed";
        size_t fileSize = file.size();

        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.begin(client, "https://api.groq.com/v1/audio/transcriptions");
        http.addHeader("Authorization", "Bearer " + key);

        String boundary = "----MelvinBoundary123456789";
        http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

        String header = "--" + boundary + "\r\n" +
                        "Content-Disposition: form-data; name=\"model\"\r\n\r\n" +
                        "whisper-large-v3-turbo\r\n" +
                        "--" + boundary + "\r\n" +
                        "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n" +
                        "Content-Type: audio/wav\r\n\r\n";
        String footer = "\r\n--" + boundary + "--\r\n";

        size_t totalLength = header.length() + fileSize + footer.length();
        http.addHeader("Content-Length", String(totalLength));

        MultipartStream mpStream(file, header, footer);
        int code = http.sendRequest("POST", &mpStream, totalLength);
        file.close();

        if (code == 200) {
            String resp = http.getString();
            JsonDocument doc;
            deserializeJson(doc, resp);
            http.end();
            return doc["text"].as<String>();
        }
        String err = http.getString();
        http.end();
        return "Error: Groq STT failed: " + err;
    }

    // --- GROQ CHAT CLIENT ---
    String callGroqChat(const String& prompt, const String& key) {
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.begin(client, "https://api.groq.com/v1/chat/completions");
        http.addHeader("Authorization", "Bearer " + key);
        http.addHeader("Content-Type", "application/json");

        JsonDocument doc;
        doc["model"] = "llama-3.3-70b-versatile";
        JsonObject msg = doc["messages"].add<JsonObject>();
        msg["role"] = "user";
        msg["content"] = prompt;

        String body;
        serializeJson(doc, body);

        int code = http.POST(body);
        if (code == 200) {
            String resp = http.getString();
            JsonDocument res;
            deserializeJson(res, resp);
            http.end();
            if (res["choices"] && res["choices"][0] && res["choices"][0]["message"]) {
                return res["choices"][0]["message"]["content"].as<String>();
            }
            return "Error API choice missing";
        }
        String err = http.getString();
        http.end();
        return "Error: Groq Chat failed: " + err;
    }

    // --- OPENROUTER CHAT CLIENT ---
    String callOpenRouterChat(const String& prompt, const String& key) {
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.begin(client, "https://openrouter.ai/api/v1/chat/completions");
        http.addHeader("Authorization", "Bearer " + key);
        http.addHeader("Content-Type", "application/json");

        JsonDocument doc;
        doc["model"] = "meta-llama/llama-3-8b-instruct:free";
        JsonObject msg = doc["messages"].add<JsonObject>();
        msg["role"] = "user";
        msg["content"] = prompt;

        String body;
        serializeJson(doc, body);

        int code = http.POST(body);
        if (code == 200) {
            String resp = http.getString();
            JsonDocument res;
            deserializeJson(res, resp);
            http.end();
            if (res["choices"] && res["choices"][0] && res["choices"][0]["message"]) {
                return res["choices"][0]["message"]["content"].as<String>();
            }
            return "Error API choice missing";
        }
        String err = http.getString();
        http.end();
        return "Error: OpenRouter failed: " + err;
    }
};

#endif
