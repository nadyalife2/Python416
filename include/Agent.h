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
    
    uint8_t* rawBuf = nullptr;
    char* b64Buf = nullptr;
    size_t b64CacheLen = 0;
    size_t b64CacheIdx = 0;

    size_t readBytesImpl(uint8_t* buffer, size_t length) {
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
                if (!rawBuf || !b64Buf) {
                    break;
                }
                size_t readBytes = file.read(rawBuf, 768);
                if (readBytes > 0) {
                    size_t encLen = 0;
                    mbedtls_base64_encode((unsigned char*)b64Buf, 1028, &encLen, rawBuf, readBytes);
                    b64CacheLen = encLen;
                    b64CacheIdx = 0;
                    buffer[bytesRead++] = b64Buf[b64CacheIdx++];
                    streamPos++;
                } else {
                    Serial.printf("[DEBUG] GeminiStream file.read returned 0! streamPos=%d, b64End=%d\n", streamPos, b64End);
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
        
        rawBuf = (uint8_t*)heap_caps_malloc(768, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (!rawBuf) rawBuf = (uint8_t*)malloc(768);
        
        if (psramFound()) {
            b64Buf = (char*)heap_caps_malloc(1028, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!b64Buf) b64Buf = (char*)malloc(1028);
    }

    ~GeminiStream() override {
        if (rawBuf) free(rawBuf);
        if (b64Buf) free(b64Buf);
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
        return (int)(totalLen - streamPos);
    }

    // Efficient bulk read — HTTPClient uses this for sendRequest()
    size_t readBytes(uint8_t* buf, size_t len) override {
        size_t got = 0;
        while (got < len && streamPos < totalLen) {
            size_t hLen = header.length();
            size_t fileEnd = hLen + fileSize;

            if (streamPos < hLen) {
                // Region 1: multipart header
                size_t chunk = min(len - got, hLen - streamPos);
                memcpy(buf + got, header.c_str() + streamPos, chunk);
                streamPos += chunk;
                got += chunk;
            } else if (streamPos < fileEnd) {
                // Region 2: WAV file data
                size_t chunk = min(len - got, fileEnd - streamPos);
                if (chunk > 1024) chunk = 1024;
                alignas(4) uint8_t temp[1024];
                size_t r = file.read(temp, chunk);
                if (r == 0) {
                    Serial.printf("[DEBUG] MultipartStream file.read returned 0! streamPos=%d, fileEnd=%d, chunk=%d\n", streamPos, fileEnd, chunk);
                    break;
                }
                memcpy(buf + got, temp, r);
                streamPos += r;
                got += r;
            } else {
                // Region 3: multipart footer
                size_t offset = streamPos - fileEnd;
                size_t chunk = min(len - got, footer.length() - offset);
                memcpy(buf + got, footer.c_str() + offset, chunk);
                streamPos += chunk;
                got += chunk;
            }
        }
        return got;
    }

    size_t readBytes(char* buf, size_t len) override {
        return readBytes((uint8_t*)buf, len);
    }

    int read() override {
        uint8_t c;
        return (readBytes(&c, 1) == 1) ? c : -1;
    }

    int peek() override { return -1; }
    size_t write(uint8_t) override { return 0; }
};

// ============================================================
// Zero-Copy Streaming Class for Yandex STT Raw LPCM POST body
// ============================================================
class LpcmStream : public Stream {
private:
    File file;
    size_t fileOffset = 44;
    size_t streamPos = 0;
    size_t totalLen = 0;

public:
    LpcmStream(File f) : file(f) {
        size_t fileSize = file.size();
        if (fileSize > fileOffset) {
            totalLen = fileSize - fileOffset;
        } else {
            totalLen = 0;
        }
        file.seek(fileOffset);
    }

    int available() override {
        return (int)(totalLen - streamPos);
    }

    size_t readBytes(uint8_t* buf, size_t len) override {
        size_t got = 0;
        while (got < len && streamPos < totalLen) {
            size_t chunk = min(len - got, totalLen - streamPos);
            if (chunk > 1024) chunk = 1024;
            alignas(4) uint8_t temp[1024];
            size_t r = file.read(temp, chunk);
            if (r == 0) break;
            memcpy(buf + got, temp, r);
            streamPos += r;
            got += r;
        }
        return got;
    }

    size_t readBytes(char* buf, size_t len) override {
        return readBytes((uint8_t*)buf, len);
    }

    int read() override {
        uint8_t c;
        return (readBytes(&c, 1) == 1) ? c : -1;
    }

    int peek() override { return -1; }
    size_t write(uint8_t) override { return 0; }
};

// ============================================================
// Zero-Copy Streaming Class to capture HTTP responses in PSRAM
// ============================================================
class PSRAMStream : public Stream {
private:
    char* _buffer;
    size_t _capacity;
    size_t _writePos = 0;
public:
    PSRAMStream(char* buffer, size_t capacity) : _buffer(buffer), _capacity(capacity) {
        if (_capacity > 0 && _buffer) {
            _buffer[0] = '\0';
        }
    }
    size_t write(uint8_t c) override {
        if (_writePos < _capacity - 1) {
            _buffer[_writePos++] = c;
            _buffer[_writePos] = '\0';
            return 1;
        }
        return 0;
    }
    size_t write(const uint8_t *buffer, size_t size) override {
        size_t space = (_writePos < _capacity - 1) ? (_capacity - 1 - _writePos) : 0;
        size_t toWrite = (size < space) ? size : space;
        if (toWrite > 0) {
            memcpy(_buffer + _writePos, buffer, toWrite);
            _writePos += toWrite;
            _buffer[_writePos] = '\0';
            return toWrite;
        }
        return 0;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    size_t getWritePos() const { return _writePos; }
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
        WiFiClient client;
        WiFiClientSecure secureClient;
        if (url.startsWith("https://")) {
            secureClient.setInsecure();
            http.begin(secureClient, url);
        } else {
            http.begin(client, url);
        }
        int httpCode = http.GET();
        String headlines = "";
        if (httpCode == HTTP_CODE_OK) {
            WiFiClient& stream = http.getStream();
            const size_t bufSize = 2048;
            uint8_t* buf = nullptr;
            if (psramFound()) {
                buf = (uint8_t*)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            }
            if (!buf) {
                buf = (uint8_t*)malloc(bufSize);
            }
            if (buf) {
                String payload = "";
                uint32_t startMs = millis();
                while (millis() - startMs < 10000) {
                    int avail = stream.available();
                    if (avail > 0) {
                        size_t toRead = min((size_t)avail, bufSize - 1);
                        size_t r = stream.readBytes(buf, toRead);
                        buf[r] = 0;
                        payload += (char*)buf;
                        startMs = millis();
                        if (payload.length() > 20000) break;
                    } else if (!stream.connected()) {
                        break;
                    } else {
                        delay(10);
                    }
                }
                free(buf);

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
                             (primaryProvider == "groq") ? cfg.groq_keys : 
                             (primaryProvider == "yandex") ? cfg.yandex_keys : cfg.openrouter_keys;

        String answer = tryProvider(audioPath, prompt, primaryProvider, primaryKeys, cfg);
        if (!answer.startsWith("Error")) {
            appendToHistory("user", "[Voice Input]");
            appendToHistory("model", answer);
            return answer;
        }

        Serial.printf("[AGENT] Primary provider %s failed. Trying fallbacks...\n", primaryProvider.c_str());

        String fallbackProviders[] = { "gemini", "groq", "openrouter", "yandex" };
        for (const String& provider : fallbackProviders) {
            if (provider == primaryProvider) continue;

            String keys = (provider == "gemini") ? cfg.gemini_keys : 
                          (provider == "groq") ? cfg.groq_keys : 
                          (provider == "yandex") ? cfg.yandex_keys : cfg.openrouter_keys;

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
    size_t readHttpToBuffer(HTTPClient& http, char* buffer, size_t capacity) {
        PSRAMStream psStream(buffer, capacity);
        int written = http.writeToStream(&psStream);
        if (written < 0) {
            Serial.printf("[AGENT] writeToStream error: %d\n", written);
        }
        return psStream.getWritePos();
    }

    String sendCustomPost(const String& url, Stream* payloadStream, size_t payloadLength, const String& contentType, const String& authHeader) {
        bool isHttps = url.startsWith("https://");
        if (!isHttps && !url.startsWith("http://")) {
            return "Error: Custom POST only supports http:// or https://";
        }
        int hostStart = isHttps ? 8 : 7;
        int hostEnd = url.indexOf('/', hostStart);
        String host = (hostEnd == -1) ? url.substring(hostStart) : url.substring(hostStart, hostEnd);
        String path = (hostEnd == -1) ? "/" : url.substring(hostEnd);

        int port = isHttps ? 443 : 80;
        int colonIdx = host.indexOf(':');
        if (colonIdx != -1) {
            port = host.substring(colonIdx + 1).toInt();
            host = host.substring(0, colonIdx);
        }

        WiFiClient* clientPtr = nullptr;
        WiFiClientSecure* secureClientPtr = nullptr;

        if (isHttps) {
            secureClientPtr = new WiFiClientSecure();
            secureClientPtr->setInsecure();
            clientPtr = secureClientPtr;
        } else {
            clientPtr = new WiFiClient();
        }

        clientPtr->setTimeout(30000);
        clientPtr->setConnectionTimeout(10000);

        Serial.printf("[CUSTOM_HTTP] Connecting to %s:%d...\n", host.c_str(), port);
        if (!clientPtr->connect(host.c_str(), port)) {
            delete clientPtr;
            return "Error: Connection to proxy failed";
        }

        // Send all headers in a single print to avoid packet fragmentation
        String req = "POST " + path + " HTTP/1.1\r\n" +
                     "Host: " + host + "\r\n" +
                     "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n";
        if (authHeader.length() > 0) {
            req += "Authorization: " + authHeader + "\r\n";
        }
        req += "Content-Type: " + contentType + "\r\n" +
               "Content-Length: " + String(payloadLength) + "\r\n" +
               "Connection: close\r\n\r\n";
        
        clientPtr->print(req);

        Serial.printf("[CUSTOM_HTTP] Sending payload (%d bytes)...\n", payloadLength);
        
        // Use 256-byte chunks to fit easily in the TCP socket send buffer
        uint8_t temp[256];
        size_t remaining = payloadLength;
        uint32_t lastPrintMs = millis();
        uint32_t writeStartMs = millis();
        
        while (remaining > 0) {
            // Check if server sent a response early
            if (clientPtr->available() > 0) {
                Serial.println("[CUSTOM_HTTP] Server sent early response, stopping write!");
                break;
            }

            if (!clientPtr->connected()) {
                if (clientPtr->available() > 0) {
                    Serial.println("[CUSTOM_HTTP] Client disconnected but response is available!");
                    break;
                }
                Serial.println("[CUSTOM_HTTP] Error: client disconnected during write!");
                clientPtr->stop();
                delete clientPtr;
                return "Error: client disconnected during write";
            }

            size_t toRead = min(remaining, sizeof(temp));
            size_t r = payloadStream->readBytes(temp, toRead);
            if (r == 0) {
                Serial.println("[CUSTOM_HTTP] Error: Stream ended prematurely!");
                clientPtr->stop();
                delete clientPtr;
                return "Error: Payload stream ended prematurely";
            }
            
            size_t written = 0;
            while (written < r) {
                if (clientPtr->available() > 0) {
                    Serial.println("[CUSTOM_HTTP] Server sent early response during chunk write, stopping!");
                    break;
                }
                if (!clientPtr->connected()) {
                    if (clientPtr->available() > 0) {
                        break;
                    }
                    Serial.println("[CUSTOM_HTTP] Error: client disconnected during write loop!");
                    clientPtr->stop();
                    delete clientPtr;
                    return "Error: client disconnected during write loop";
                }
                int w = clientPtr->write(temp + written, r - written);
                if (w < 0) {
                    delay(50); // wait a short bit for any response packets
                    if (clientPtr->available() > 0) {
                        Serial.println("[CUSTOM_HTTP] client.write failed but response is available!");
                        break;
                    }
                    Serial.printf("[CUSTOM_HTTP] Error: client.write returned %d (errno=%d)\n", w, errno);
                    clientPtr->stop();
                    delete clientPtr;
                    return "Error: Write to socket failed";
                }
                if (w == 0) {
                    // EAGAIN or full buffer. Wait a bit and retry.
                    if (millis() - writeStartMs > 15000) { // 15 seconds timeout
                        delay(50);
                        if (clientPtr->available() > 0) {
                            break;
                        }
                        Serial.printf("[CUSTOM_HTTP] Error: Write timeout! (errno=%d)\n", errno);
                        clientPtr->stop();
                        delete clientPtr;
                        return "Error: Write to socket timeout";
                    }
                    delay(5);
                    continue;
                }
                written += w;
                writeStartMs = millis(); // Reset timeout since we successfully wrote some bytes
            }
            
            if (clientPtr->available() > 0) {
                break;
            }
            
            remaining -= r;
            if (millis() - lastPrintMs > 2000) {
                lastPrintMs = millis();
                Serial.printf("[CUSTOM_HTTP] Sent: %d%%\n", (int)(100 - (remaining * 100 / payloadLength)));
            }
        }
        
        Serial.println("[CUSTOM_HTTP] Payload sent. Reading response...");
        
        int statusCode = 0;
        String line;
        while (clientPtr->connected() || clientPtr->available()) {
            line = clientPtr->readStringUntil('\n');
            line.trim();
            if (line.length() == 0) {
                break;
            }
            if (line.startsWith("HTTP/1.1 ") || line.startsWith("HTTP/1.0 ")) {
                statusCode = line.substring(9, 12).toInt();
            }
        }
        
        Serial.printf("[CUSTOM_HTTP] HTTP Status: %d\n", statusCode);
        
        String responseBody = "";
        char readBuf[256];
        while (clientPtr->connected() || clientPtr->available()) {
            int avail = clientPtr->available();
            if (avail > 0) {
                int toRead = min(avail, (int)sizeof(readBuf) - 1);
                int r = clientPtr->read((uint8_t*)readBuf, toRead);
                if (r > 0) {
                    readBuf[r] = '\0';
                    responseBody += readBuf;
                }
                if (responseBody.length() > 65536) {
                    break;
                }
            } else {
                delay(5);
            }
        }
        clientPtr->stop();
        delete clientPtr;
        
        if (statusCode == 200) {
            return responseBody;
        } else {
            Serial.printf("[CUSTOM_HTTP] Error body: %s\n", responseBody.c_str());
            return "Error: HTTP " + String(statusCode) + " - " + responseBody;
        }
    }

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
            return callGemini(audioPath, prompt, key, cfg);
        } else if (provider == "yandex") {
            String text = transcribeYandexSTT(audioPath, key, cfg);
            if (text.startsWith("Error")) return text;
            Serial.printf("[AGENT] Transcribed text (Yandex): %s\n", text.c_str());
            String fullPrompt = prompt + "\n\nUser said: " + text;
            return callYandexGPT(fullPrompt, key, cfg);
        } else if (provider == "groq") {
            String text = transcribeGroqWhisper(audioPath, key, cfg);
            if (text.startsWith("Error")) return text;
            Serial.printf("[AGENT] Transcribed text: %s\n", text.c_str());
            String fullPrompt = prompt + "\n\nUser said: " + text;
            return callGroqChat(fullPrompt, key, cfg);
        } else if (provider == "openrouter") {
            Serial.println("[AGENT] Trying OpenRouter STT transcription...");
            String text = transcribeOpenRouter(audioPath, key, cfg);
            if (text.startsWith("Error")) {
                Serial.printf("[AGENT] OpenRouter STT failed: %s. Trying Groq STT fallback...\n", text.c_str());
                String groqKeys = cfg.groq_keys;
                String groqKey = "";
                if (groqKeys.length() > 5) {
                    int firstComma = groqKeys.indexOf(',');
                    groqKey = (firstComma == -1) ? groqKeys : groqKeys.substring(0, firstComma);
                    groqKey.trim();
                }
                if (groqKey.length() > 5) {
                    text = transcribeGroqWhisper(audioPath, groqKey, cfg);
                } else {
                    return "Error: OpenRouter STT failed and no Groq Whisper key is configured.";
                }
            }
            if (text.startsWith("Error")) return text;
            Serial.printf("[AGENT] Transcribed text for OpenRouter: %s\n", text.c_str());
            String fullPrompt = prompt + "\n\nUser said: " + text;
            return callOpenRouterChat(fullPrompt, key, cfg);
        }
        return "Error: Unsupported provider: " + provider;
    }

    // --- YANDEX STT CLIENT (Stream-Based raw LPCM) ---
    String transcribeYandexSTT(const String& audioPath, const String& key, const MelvinConfig& cfg) {
        File file = SD_MMC.open(audioPath, FILE_READ);
        if (!file) return "Error: Open audio failed";
        size_t fileSize = file.size();
        if (fileSize <= 44) {
            file.close();
            return "Error: WAV file too small";
        }
        size_t dataSize = fileSize - 44;

        int colonIdx = key.indexOf(':');
        if (colonIdx == -1) {
            file.close();
            return "Error: Yandex key format must be FolderID:ApiKey";
        }
        String folderId = key.substring(0, colonIdx);
        String apiKey = key.substring(colonIdx + 1);
        folderId.trim();
        apiKey.trim();

        String result;
        {
            WiFiClient client;
            client.setTimeout(30000);
            client.setConnectionTimeout(30000);
            WiFiClientSecure secureClient;
            secureClient.setTimeout(30000);
            secureClient.setConnectionTimeout(30000);
            HTTPClient http;
            http.setTimeout(30000);
            http.setConnectTimeout(30000);

            String url = "https://stt.api.cloud.yandex.net/speech/v1/stt:recognize?folderId=" + folderId + "&format=lpcm&sampleRateHertz=16000";
            if (cfg.api_proxy.length() > 0) {
                url = cfg.api_proxy + "/yandex-stt/speech/v1/stt:recognize?folderId=" + folderId + "&format=lpcm&sampleRateHertz=16000";
                if (url.startsWith("https://")) {
                    secureClient.setInsecure();
                    http.begin(secureClient, url);
                } else {
                    http.begin(client, url);
                }
            } else {
                secureClient.setInsecure();
                http.begin(secureClient, url);
            }

            http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
            http.addHeader("Authorization", "Api-Key " + apiKey);
            http.addHeader("Content-Type", "application/octet-stream");

            LpcmStream lpStream(file);
            int code = http.sendRequest("POST", &lpStream, dataSize);
            file.close();

            Serial.printf("[AGENT] Yandex STT HTTP response: %d\n", code);

            if (code == 200) {
                String resp = http.getString();
                JsonDocument doc;
                deserializeJson(doc, resp);
                http.end();
                result = doc["result"].as<String>();
            } else {
                String err = http.getString();
                http.end();
                result = "Error: Yandex STT failed (" + String(code) + "): " + err;
            }
        }
        delay(150);
        return result;
    }

    // --- YANDEX GPT LLM CLIENT ---
    String callYandexGPT(const String& prompt, const String& key, const MelvinConfig& cfg) {
        int colonIdx = key.indexOf(':');
        if (colonIdx == -1) {
            return "Error: Yandex key format must be FolderID:ApiKey";
        }
        String folderId = key.substring(0, colonIdx);
        String apiKey = key.substring(colonIdx + 1);
        folderId.trim();
        apiKey.trim();

        String result;
        {
            WiFiClient client;
            client.setTimeout(30000);
            client.setConnectionTimeout(30000);
            WiFiClientSecure secureClient;
            secureClient.setTimeout(30000);
            secureClient.setConnectionTimeout(30000);
            HTTPClient http;
            http.setTimeout(30000);
            http.setConnectTimeout(30000);

            String url = "https://llm.api.cloud.yandex.net/foundationModels/v1/completion";
            if (cfg.api_proxy.length() > 0) {
                url = cfg.api_proxy + "/yandex-gpt/foundationModels/v1/completion";
                if (url.startsWith("https://")) {
                    secureClient.setInsecure();
                    http.begin(secureClient, url);
                } else {
                    http.begin(client, url);
                }
            } else {
                secureClient.setInsecure();
                http.begin(secureClient, url);
            }

            http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
            http.addHeader("Authorization", "Api-Key " + apiKey);
            http.addHeader("Content-Type", "application/json");

            JsonDocument doc;
            doc["modelUri"] = "gpt://" + folderId + "/yandexgpt/latest";
            JsonObject compOpts = doc["completionOptions"].to<JsonObject>();
            compOpts["stream"] = false;
            compOpts["temperature"] = 0.5;
            compOpts["maxTokens"] = 1000;

            JsonArray messages = doc["messages"].to<JsonArray>();
            JsonObject msg = messages.add<JsonObject>();
            msg["role"] = "user";
            msg["text"] = prompt;

            String body;
            serializeJson(doc, body);

            int code = http.POST(body);
            Serial.printf("[AGENT] Yandex GPT HTTP response: %d\n", code);

            if (code == 200) {
                size_t maxRespLen = 64 * 1024;
                char* respBuf = nullptr;
                if (psramFound()) {
                    respBuf = (char*)heap_caps_malloc(maxRespLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                }
                if (!respBuf) {
                    Serial.println("[AGENT] PSRAM allocation failed or PSRAM not found, falling back to SRAM (32KB)");
                    maxRespLen = 32 * 1024;
                    respBuf = (char*)malloc(maxRespLen);
                }
                if (respBuf) {
                    size_t got = readHttpToBuffer(http, respBuf, maxRespLen);
                    JsonDocument res;
                    DeserializationError err = deserializeJson(res, respBuf, got);
                    if (err == DeserializationError::Ok) {
                        if (res["result"] && res["result"]["alternatives"] && 
                            res["result"]["alternatives"][0] && 
                            res["result"]["alternatives"][0]["message"]) {
                            result = res["result"]["alternatives"][0]["message"]["text"].as<String>();
                        } else {
                            result = "Error: Yandex GPT response missing alternatives";
                        }
                    } else {
                        result = "Error: Yandex GPT JSON parse failed";
                    }
                    heap_caps_free(respBuf);
                } else {
                    result = "Error: Out of memory for response buffer";
                }
            } else {
                String err = http.getString();
                Serial.printf("[AGENT] Yandex GPT error body: %.300s\n", err.c_str());
                result = "Error: Yandex GPT failed (" + String(code) + "): " + err;
            }
            http.end();
        }
        delay(150);
        return result;
    }

    // --- GEMINI CLIENT (Stream-Based to avoid SRAM exhaustion) ---
    String callGemini(const String& audioPath, const String& prompt, String key, const MelvinConfig& cfg) {
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
        
        int insertPos = headerStr.lastIndexOf("}]}]}");
        if (insertPos == -1) {
            file.close();
            return "Error: JSON header format invalid";
        }
        String jsonStart = headerStr.substring(0, insertPos + 1) + ",{\"inline_data\":{\"mime_type\":\"audio/wav\",\"data\":\"";
        String jsonEnd = "\"}}]}]}";
        
        size_t totalLength = jsonStart.length() + b64Len + jsonEnd.length();
        Serial.printf("[AGENT] Streamed POST size: %d bytes, free PSRAM: %d\n", 
                      totalLength, heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

        String answer;
        String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=" + key;
        if (cfg.api_proxy.length() > 0) {
            url = cfg.api_proxy + "/gemini/v1beta/models/gemini-2.5-flash:generateContent?key=" + key;
        }

        if (cfg.api_proxy.length() > 0) {
            GeminiStream gStream(file, jsonStart, jsonEnd);
            String resp = sendCustomPost(url, &gStream, totalLength, "application/json", "");
            file.close();
            if (resp.startsWith("Error")) {
                answer = resp;
            } else {
                JsonDocument res;
                DeserializationError err = deserializeJson(res, resp);
                if (err == DeserializationError::Ok) {
                    if (res["candidates"] && res["candidates"][0] &&
                        res["candidates"][0]["content"] &&
                        res["candidates"][0]["content"]["parts"] &&
                        res["candidates"][0]["content"]["parts"][0]) {
                        answer = res["candidates"][0]["content"]["parts"][0]["text"].as<String>();
                        Serial.printf("[AGENT] AI Answer: %s\n", answer.c_str());
                    } else {
                        answer = "Error API parsed response invalid";
                    }
                } else {
                    answer = "Error API JSON parse failed";
                }
            }
        } else {
            WiFiClientSecure secureClient;
            secureClient.setTimeout(30000);
            secureClient.setConnectionTimeout(30000);
            HTTPClient http;
            http.setTimeout(30000); 
            http.setConnectTimeout(30000);
            
            secureClient.setInsecure();
            http.begin(secureClient, url);
            http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
            http.addHeader("Content-Type", "application/json");
            
            GeminiStream gStream(file, jsonStart, jsonEnd);
            int code = http.sendRequest("POST", &gStream, totalLength);
            file.close();
            
            Serial.printf("[AGENT] HTTP response: %d\n", code);
            
            if (code == 200) {
                size_t maxRespLen = 64 * 1024;
                char* respBuf = nullptr;
                if (psramFound()) {
                    respBuf = (char*)heap_caps_malloc(maxRespLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                }
                if (!respBuf) {
                    Serial.println("[AGENT] PSRAM allocation failed or PSRAM not found, falling back to SRAM (32KB)");
                    maxRespLen = 32 * 1024;
                    respBuf = (char*)malloc(maxRespLen);
                }
                if (respBuf) {
                    size_t got = readHttpToBuffer(http, respBuf, maxRespLen);
                    Serial.printf("[AGENT] Response preview: %.200s\n", respBuf);
                    JsonDocument res;
                    DeserializationError err = deserializeJson(res, respBuf, got);
                    if (err == DeserializationError::Ok) {
                        if (res["candidates"] && res["candidates"][0] &&
                            res["candidates"][0]["content"] &&
                            res["candidates"][0]["content"]["parts"] &&
                            res["candidates"][0]["content"]["parts"][0]) {
                            answer = res["candidates"][0]["content"]["parts"][0]["text"].as<String>();
                            Serial.printf("[AGENT] AI Answer: %s\n", answer.c_str());
                        } else {
                            answer = "Error API parsed response invalid";
                        }
                    } else {
                        answer = "Error API JSON parse failed";
                    }
                    heap_caps_free(respBuf);
                } else {
                    answer = "Error: Out of memory for response buffer";
                }
            } else {
                String errBody = http.getString();
                Serial.printf("[AGENT] Error body: %.300s\n", errBody.c_str());
                answer = "Error API " + String(code);
            }
            http.end();
            delay(150); // Give lwIP stack time to fully close the SSL socket
        }
        return answer;
    }

    // --- GROQ WHISPER STT CLIENT (Stream-Based multipart) ---
    String transcribeGroqWhisper(const String& audioPath, const String& key, const MelvinConfig& cfg) {
        File file = SD_MMC.open(audioPath, FILE_READ);
        if (!file) return "Error: Open audio failed";
        size_t fileSize = file.size();

        // Use a scoped block so WiFiClient destructor runs before next request
        String result;
        String url = "https://api.groq.com/openai/v1/audio/transcriptions";
        if (cfg.api_proxy.length() > 0) {
            url = cfg.api_proxy + "/groq/openai/v1/audio/transcriptions";
        }

        String boundary = "----MelvinBoundary123456789";
        String contentType = "multipart/form-data; boundary=" + boundary;
        String header = "--" + boundary + "\r\n" +
                        "Content-Disposition: form-data; name=\"model\"\r\n\r\n" +
                        "whisper-large-v3-turbo\r\n" +
                        "--" + boundary + "\r\n" +
                        "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n" +
                        "Content-Type: audio/wav\r\n\r\n";
        String footer = "\r\n--" + boundary + "--\r\n";
        size_t totalLength = header.length() + fileSize + footer.length();
        MultipartStream mpStream(file, header, footer);

        if (cfg.api_proxy.length() > 0) {
            String resp = sendCustomPost(url, &mpStream, totalLength, contentType, "Bearer " + key);
            file.close();
            if (resp.startsWith("Error")) {
                result = resp;
            } else {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, resp);
                if (err == DeserializationError::Ok) {
                    result = doc["text"].as<String>();
                } else {
                    result = "Error: Groq JSON parse failed";
                }
            }
        } else {
            WiFiClientSecure secureClient;
            secureClient.setTimeout(30000);
            secureClient.setConnectionTimeout(30000);
            HTTPClient http;
            http.setTimeout(30000);
            http.setConnectTimeout(30000);
            
            secureClient.setInsecure();
            http.begin(secureClient, url);
            http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
            http.addHeader("Authorization", "Bearer " + key);
            http.addHeader("Content-Type", contentType);

            int code = http.sendRequest("POST", &mpStream, totalLength);
            file.close();

            if (code == 200) {
                String resp = http.getString();
                JsonDocument doc;
                deserializeJson(doc, resp);
                http.end();
                result = doc["text"].as<String>();
            } else {
                String err = http.getString();
                http.end();
                result = "Error: Groq STT failed (" + String(code) + "): " + err;
            }
            delay(150); // Give lwIP stack time to fully close the SSL socket
        }
        return result;
    }

    // --- OPENROUTER WHISPER STT CLIENT (Stream-Based multipart) ---
    String transcribeOpenRouter(const String& audioPath, const String& key, const MelvinConfig& cfg) {
        File file = SD_MMC.open(audioPath, FILE_READ);
        if (!file) return "Error: Open audio failed";
        size_t fileSize = file.size();

        String result;
        String url = "https://openrouter.ai/api/v1/audio/transcriptions";
        if (cfg.api_proxy.length() > 0) {
            url = cfg.api_proxy + "/openrouter/api/v1/audio/transcriptions";
        }

        String boundary = "----MelvinBoundary7731";
        String contentType = "multipart/form-data; boundary=" + boundary;
        String header = "--" + boundary + "\r\n" +
                        "Content-Disposition: form-data; name=\"model\"\r\n\r\n" +
                        "openai/whisper-1\r\n" +
                        "--" + boundary + "\r\n" +
                        "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n" +
                        "Content-Type: audio/wav\r\n\r\n";
        String footer = "\r\n--" + boundary + "--\r\n";
        size_t totalLength = header.length() + fileSize + footer.length();
        MultipartStream mpStream(file, header, footer);

        if (cfg.api_proxy.length() > 0) {
            String resp = sendCustomPost(url, &mpStream, totalLength, contentType, "Bearer " + key);
            file.close();
            if (resp.startsWith("Error")) {
                result = resp;
            } else {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, resp);
                if (err == DeserializationError::Ok) {
                    result = doc["text"].as<String>();
                } else {
                    result = "Error: OpenRouter JSON parse failed";
                }
            }
        } else {
            WiFiClientSecure secureClient;
            secureClient.setTimeout(30000);
            secureClient.setConnectionTimeout(30000);
            HTTPClient http;
            http.setTimeout(30000);
            http.setConnectTimeout(30000);
            
            secureClient.setInsecure();
            http.begin(secureClient, url);
            http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
            http.addHeader("Authorization", "Bearer " + key);
            http.addHeader("Content-Type", contentType);

            int code = http.sendRequest("POST", &mpStream, totalLength);
            file.close();

            Serial.printf("[AGENT] OpenRouter STT HTTP response: %d\n", code);

            if (code == 200) {
                size_t maxRespLen = 16 * 1024;
                char* respBuf = nullptr;
                if (psramFound()) {
                    respBuf = (char*)heap_caps_malloc(maxRespLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                }
                if (!respBuf) {
                    Serial.println("[AGENT] PSRAM allocation failed or PSRAM not found, falling back to SRAM (8KB)");
                    maxRespLen = 8 * 1024;
                    respBuf = (char*)malloc(maxRespLen);
                }
                if (respBuf) {
                    size_t got = readHttpToBuffer(http, respBuf, maxRespLen);
                    JsonDocument doc;
                    deserializeJson(doc, respBuf, got);
                    result = doc["text"].as<String>();
                    heap_caps_free(respBuf);
                } else {
                    result = "Error: Out of memory for OpenRouter STT response";
                }
            } else {
                String err = http.getString();
                Serial.printf("[AGENT] OpenRouter STT error body: %.300s\n", err.c_str());
                result = "Error: OpenRouter STT failed (" + String(code) + "): " + err;
            }
            http.end();
            delay(150); // Give lwIP stack time to fully close the SSL socket
        }
        return result;
    }

    // --- GROQ CHAT CLIENT ---
    String callGroqChat(const String& prompt, const String& key, const MelvinConfig& cfg) {
        // Use scoped block for clean SSL socket lifecycle
        String result;
        {
            WiFiClient client;
            client.setTimeout(30000);
            client.setConnectionTimeout(30000);
            WiFiClientSecure secureClient;
            secureClient.setTimeout(30000);
            secureClient.setConnectionTimeout(30000);
            HTTPClient http;
            http.setTimeout(30000);
            http.setConnectTimeout(30000);
            
            String url = "https://api.groq.com/openai/v1/chat/completions";
            if (cfg.api_proxy.length() > 0) {
                url = cfg.api_proxy + "/groq/openai/v1/chat/completions";
                if (url.startsWith("https://")) {
                    secureClient.setInsecure();
                    http.begin(secureClient, url);
                } else {
                    http.begin(client, url);
                }
            } else {
                secureClient.setInsecure();
                http.begin(secureClient, url);
            }
            http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
            http.addHeader("Authorization", "Bearer " + key);
            http.addHeader("Content-Type", "application/json");

            JsonDocument doc;
            // Try llama-3.3-70b-versatile first (best quality on Groq)
            doc["model"] = "llama-3.3-70b-versatile";
            JsonObject msg = doc["messages"].add<JsonObject>();
            msg["role"] = "user";
            msg["content"] = prompt;

            String body;
            serializeJson(doc, body);

            int code = http.POST(body);
            Serial.printf("[AGENT] Groq Chat HTTP %d\n", code);
            if (code == 200) {
                size_t maxRespLen = 64 * 1024;
                char* respBuf = nullptr;
                if (psramFound()) {
                    respBuf = (char*)heap_caps_malloc(maxRespLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                }
                if (!respBuf) {
                    Serial.println("[AGENT] PSRAM allocation failed or PSRAM not found, falling back to SRAM (32KB)");
                    maxRespLen = 32 * 1024;
                    respBuf = (char*)malloc(maxRespLen);
                }
                if (respBuf) {
                    size_t got = readHttpToBuffer(http, respBuf, maxRespLen);
                    JsonDocument res;
                    DeserializationError err = deserializeJson(res, respBuf, got);
                    if (err == DeserializationError::Ok) {
                        if (res["choices"] && res["choices"][0] && res["choices"][0]["message"]) {
                            result = res["choices"][0]["message"]["content"].as<String>();
                        } else {
                            result = "Error: Groq Chat response missing choices";
                        }
                    } else {
                        result = "Error: Groq Chat JSON parse failed";
                    }
                    heap_caps_free(respBuf);
                } else {
                    result = "Error: Out of memory for response buffer";
                }
            } else {
                String err = http.getString();
                Serial.printf("[AGENT] Groq Chat error body: %.200s\n", err.c_str());
                result = "Error: Groq Chat failed (" + String(code) + "): " + err;
            }
            http.end();
        } // WiFiClientSecure + HTTPClient fully destroyed here
        delay(150); // Give lwIP stack time to fully close the SSL socket
        return result;
    }

    // --- OPENROUTER CHAT CLIENT (с каскадом моделей) ---
    String callOpenRouterChat(const String& prompt, const String& key, const MelvinConfig& cfg) {
        // Каскад моделей: сначала мощная, потом авто-auto если не доступна
        const char* models[] = {
            "meta-llama/llama-3.3-70b-instruct:free",   // 1. Лучшая бесплатная Llama 3.3 70B
            "openrouter/auto"                             // 2. Авто-выбор оптимальной модели
        };
        const int numModels = 2;

        for (int mi = 0; mi < numModels; mi++) {
            Serial.printf("[AGENT] OpenRouter: trying model '%s'\n", models[mi]);

            String iterResult;
            bool iterOk = false;
            {
                // Scoped block: WiFiClient destructor runs before next model attempt
                WiFiClient client;
                client.setTimeout(30000);
                client.setConnectionTimeout(30000);
                WiFiClientSecure secureClient;
                secureClient.setTimeout(30000);
                secureClient.setConnectionTimeout(30000);
                HTTPClient http;
                http.setTimeout(30000);
                http.setConnectTimeout(30000);
                
                String url = "https://openrouter.ai/api/v1/chat/completions";
                if (cfg.api_proxy.length() > 0) {
                    url = cfg.api_proxy + "/openrouter/api/v1/chat/completions";
                    if (url.startsWith("https://")) {
                        secureClient.setInsecure();
                        http.begin(secureClient, url);
                    } else {
                        http.begin(client, url);
                    }
                } else {
                    secureClient.setInsecure();
                    http.begin(secureClient, url);
                }
                http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
                http.addHeader("Authorization", "Bearer " + key);
                http.addHeader("Content-Type", "application/json");
                http.addHeader("HTTP-Referer", "https://melvin.local");
                http.addHeader("X-Title", "Melvin Voice AI");

                JsonDocument doc;
                doc["model"] = models[mi];
                JsonObject msg = doc["messages"].add<JsonObject>();
                msg["role"] = "user";
                msg["content"] = prompt;

                String body;
                serializeJson(doc, body);

                int code = http.POST(body);
                Serial.printf("[AGENT] OpenRouter HTTP %d (model %d)\n", code, mi + 1);

                if (code == 200) {
                    size_t maxRespLen = 64 * 1024;
                    char* respBuf = nullptr;
                    if (psramFound()) {
                        respBuf = (char*)heap_caps_malloc(maxRespLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    }
                    if (!respBuf) {
                        Serial.println("[AGENT] PSRAM allocation failed or PSRAM not found, falling back to SRAM (32KB)");
                        maxRespLen = 32 * 1024;
                        respBuf = (char*)malloc(maxRespLen);
                    }
                    if (respBuf) {
                        size_t got = readHttpToBuffer(http, respBuf, maxRespLen);
                        JsonDocument res;
                        DeserializationError err = deserializeJson(res, respBuf, got);
                        if (err == DeserializationError::Ok) {
                            if (res["choices"] && res["choices"][0] && res["choices"][0]["message"]) {
                                iterResult = res["choices"][0]["message"]["content"].as<String>();
                                iterOk = true;
                            } else {
                                Serial.printf("[AGENT] OpenRouter model '%s' returned empty answer, trying fallback\n", models[mi]);
                            }
                        } else {
                            Serial.println("[AGENT] OpenRouter JSON parse failed");
                        }
                        heap_caps_free(respBuf);
                    } else {
                        Serial.println("[AGENT] Out of memory for OpenRouter response buffer");
                    }
                } else {
                    // 429 = rate limit, 503 = недоступна — пробуем следующую
                    String err = http.getString();
                    Serial.printf("[AGENT] OpenRouter model '%s' failed: %d %.100s\n", models[mi], code, err.c_str());
                    if (mi < numModels - 1) {
                        Serial.println("[AGENT] Switching to next OpenRouter model...");
                    }
                }
                http.end();
            } // WiFiClientSecure + HTTPClient fully destroyed here

            if (iterOk) {
                Serial.printf("[AGENT] OpenRouter model '%s' OK\n", models[mi]);
                return iterResult;
            }
            // Give lwIP stack time to fully close SSL socket before next attempt
            delay(150);
        }

        return "Error: All OpenRouter models failed.";
    }
};

#endif
