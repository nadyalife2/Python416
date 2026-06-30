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
#include <vector>

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
                    size_t chunk = b64CacheLen - b64CacheIdx;
                    if (chunk > length - bytesRead) chunk = length - bytesRead;
                    memcpy(buffer + bytesRead, b64Buf + b64CacheIdx, chunk);
                    b64CacheIdx += chunk;
                    bytesRead += chunk;
                    streamPos += chunk;
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
                    
                    size_t chunk = b64CacheLen - b64CacheIdx;
                    if (chunk > length - bytesRead) chunk = length - bytesRead;
                    memcpy(buffer + bytesRead, b64Buf + b64CacheIdx, chunk);
                    b64CacheIdx += chunk;
                    bytesRead += chunk;
                    streamPos += chunk;
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

    bool isValid() const { return rawBuf != nullptr && b64Buf != nullptr; }

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
// Zero-Copy Streaming Class for Base64 JSON POST body (n8n bypass)
// ============================================================
class Base64JsonStream : public Stream {
private:
    File file;
    size_t fileSize;
    size_t filePos = 0;
    
    String header;
    String footer = "\"}";
    
    size_t base64Len;
    size_t totalLen;
    size_t streamPos = 0;

    uint8_t* buffer = nullptr;
    size_t bufferLen = 0;
    size_t bufferPos = 0;
    
    uint8_t* tempBuf = nullptr;

    void fillBuffer() {
        bufferLen = 0;
        bufferPos = 0;

        if (streamPos < header.length()) {
            size_t chunk = header.length() - streamPos;
            if (chunk > 4096) chunk = 4096;
            memcpy(buffer, header.c_str() + streamPos, chunk);
            bufferLen = chunk;
            return;
        }

        size_t afterHeader = streamPos - header.length();
        if (afterHeader < base64Len) {
            // Read up to 3072 bytes (which encodes exactly to 4096 bytes base64)
            size_t bytesToRead = 3072;
            if (filePos + bytesToRead > fileSize) {
                bytesToRead = fileSize - filePos;
            }
            if (bytesToRead == 0) return;

            size_t r = file.read(tempBuf, bytesToRead);
            if (r == 0) return;

            size_t olen = 0;
            // 4097 buffer size to allow mbedtls to write the null-terminator safely
            int ret = mbedtls_base64_encode(buffer, 4097, &olen, tempBuf, r);
            if (ret != 0) {
                Serial.printf("Base64 encode error: -0x%04X\n", -ret);
                return;
            }
            bufferLen = olen;
            filePos += r;
            return;
        }

        size_t afterBase64 = afterHeader - base64Len;
        if (afterBase64 < footer.length()) {
            size_t chunk = footer.length() - afterBase64;
            if (chunk > 4096) chunk = 4096;
            memcpy(buffer, footer.c_str() + afterBase64, chunk);
            bufferLen = chunk;
            return;
        }
    }

public:
    Base64JsonStream(File f) : file(f) {
        fileSize = file.size();
        header = "{\"audioSize\":" + String(fileSize) + ",\"audioBase64\":\"";
        base64Len = ((fileSize + 2) / 3) * 4;
        totalLen = header.length() + base64Len + footer.length();
        
        // Allocate buffers on heap to prevent stack overflow on ESP32 loopTask (8KB limit)
        buffer = new uint8_t[4097];
        tempBuf = new uint8_t[3072];
    }

    ~Base64JsonStream() {
        if (buffer) delete[] buffer;
        if (tempBuf) delete[] tempBuf;
    }

    size_t getTotalLen() const { return totalLen; }

    int available() override {
        return (int)(totalLen - streamPos);
    }

    size_t readBytes(uint8_t* buf, size_t len) override {
        size_t got = 0;
        while (got < len && streamPos < totalLen) {
            if (bufferPos >= bufferLen) {
                fillBuffer();
                if (bufferLen == 0) break;
            }
            size_t chunk = min(len - got, bufferLen - bufferPos);
            memcpy(buf + got, buffer + bufferPos, chunk);
            bufferPos += chunk;
            streamPos += chunk;
            got += chunk;
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

// ============================================================
// Fast read-only stream backed by a PSRAM buffer
// Used to send pre-loaded audio payloads at full speed
// ============================================================
class PSRAMReadStream : public Stream {
private:
    const uint8_t* _buf;
    size_t _len;
    size_t _pos = 0;
public:
    PSRAMReadStream(const uint8_t* buf, size_t len) : _buf(buf), _len(len) {}
    int available() override { return (int)(_len - _pos); }
    int read() override {
        if (_pos < _len) return _buf[_pos++];
        return -1;
    }
    size_t readBytes(uint8_t* out, size_t sz) override {
        size_t toRead = min(sz, _len - _pos);
        if (toRead > 0) {
            memcpy(out, _buf + _pos, toRead);
            _pos += toRead;
        }
        return toRead;
    }
    size_t readBytes(char* out, size_t sz) override {
        return readBytes((uint8_t*)out, sz);
    }
    int peek() override { return (_pos < _len) ? _buf[_pos] : -1; }
    size_t write(uint8_t) override { return 0; }
};

class MelvinAgent {
public:
    String lastTranscription;

    String transcribeRaw(const int16_t* buf, size_t len, const MelvinConfig& cfg) {
        String keys = cfg.groq_keys;
        if (keys.length() < 5) {
            return "Error: No Groq keys";
        }
        int firstComma = keys.indexOf(',');
        String key = (firstComma == -1) ? keys : keys.substring(0, firstComma);
        key.trim();

        // 1. Build WAV file in PSRAM
        size_t wavDataSize = len * sizeof(int16_t);
        size_t wavTotalSize = 44 + wavDataSize;

        // 2. Build multipart payload
        String boundary = "----MelvinBoundary123456789";
        String contentType = "multipart/form-data; boundary=" + boundary;
        String headerPart = "--" + boundary + "\r\n"
                            "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
                            "whisper-large-v3-turbo\r\n"
                            "--" + boundary + "\r\n"
                            "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
                            "Content-Type: audio/wav\r\n\r\n";
        String footerPart = "\r\n--" + boundary + "--\r\n";
        size_t totalPayloadSize = headerPart.length() + wavTotalSize + footerPart.length();

        uint8_t* psramBuf = (uint8_t*)heap_caps_malloc(totalPayloadSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!psramBuf) {
            psramBuf = (uint8_t*)malloc(totalPayloadSize);
        }
        if (!psramBuf) return "Error: Memory allocation failed for transcribeRaw";

        // Copy headerPart
        size_t offset = 0;
        memcpy(psramBuf + offset, headerPart.c_str(), headerPart.length());
        offset += headerPart.length();

        // Write WAV header
        struct {
            char chunkId[4] = {'R', 'I', 'F', 'F'};
            uint32_t chunkSize;
            char format[4] = {'W', 'A', 'V', 'E'};
            char subchunk1Id[4] = {'f', 'm', 't', ' '};
            uint32_t subchunk1Size = 16;
            uint16_t audioFormat = 1; // PCM
            uint16_t numChannels = 1;
            uint32_t sampleRate = 16000;
            uint32_t byteRate = 32000;
            uint16_t blockAlign = 2;
            uint16_t bitsPerSample = 16;
            char subchunk2Id[4] = {'d', 'a', 't', 'a'};
            uint32_t subchunk2Size;
        } wavHeader;
        wavHeader.chunkSize = 36 + wavDataSize;
        wavHeader.subchunk2Size = wavDataSize;

        memcpy(psramBuf + offset, &wavHeader, sizeof(wavHeader));
        offset += sizeof(wavHeader);

        // Copy audio data
        memcpy(psramBuf + offset, buf, wavDataSize);
        offset += wavDataSize;

        // Copy footerPart
        memcpy(psramBuf + offset, footerPart.c_str(), footerPart.length());
        offset += footerPart.length();
        totalPayloadSize = offset;

        // 3. Send HTTP request
        String result;
        String url = "https://api.groq.com/openai/v1/audio/transcriptions";
        if (cfg.api_proxy.length() > 0) {
            url = cfg.api_proxy + "/groq/openai/v1/audio/transcriptions";
        }

        PSRAMReadStream psStream(psramBuf, totalPayloadSize);

        if (cfg.api_proxy.length() > 0) {
            String resp = sendCustomPost(url, &psStream, totalPayloadSize, contentType, "Bearer " + key);
            free(psramBuf);
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
            secureClient.setTimeout(15000); // faster timeout for wake word check
            secureClient.setConnectionTimeout(10000);
            HTTPClient http;
            http.setTimeout(15000);
            http.setConnectTimeout(10000);

            secureClient.setInsecure();
            http.begin(secureClient, url);
            http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
            http.addHeader("Authorization", "Bearer " + key);
            http.addHeader("Content-Type", contentType);

            int code = http.sendRequest("POST", &psStream, totalPayloadSize);
            free(psramBuf);

            if (code == 200) {
                String resp = http.getString();
                JsonDocument doc;
                deserializeJson(doc, resp);
                result = doc["text"].as<String>();
            } else {
                String err = http.getString();
                result = "Error: Groq STT failed (" + String(code) + "): " + err;
            }
            http.end();
        }
        return result;
    }

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

    void trimHistoryIfNeeded(size_t maxBytes = 32 * 1024) {
        if (!SD_MMC.exists("/history.jsonl")) return;
        File f = SD_MMC.open("/history.jsonl", FILE_READ);
        if (!f || f.size() < maxBytes) { if(f) f.close(); return; }
        
        size_t keepFrom = f.size() - (maxBytes / 2);
        f.seek(keepFrom);
        String tail = f.readString();
        f.close();
        
        // Выровнять по границе строки JSONL
        int nl = tail.indexOf('\n');
        if (nl > 0) tail = tail.substring(nl + 1);
        
        File out = SD_MMC.open("/history.jsonl", FILE_WRITE);
        if (out) { out.print(tail); out.close(); }
        Serial.printf("[AGENT] History trimmed to ~%d bytes\n", tail.length());
    }

    void recordExchange(const String& userText, const String& botAnswer) {
        if (userText.length() > 0) appendToHistory("user", userText);
        if (botAnswer.length() > 0) appendToHistory("model", botAnswer);
        trimHistoryIfNeeded(); // обрезать если > 32KB
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
                bool skipFirst = true; // первый <title> = название канала, всегда пропускаем

                while ((pos = payload.indexOf("<title>", pos)) != -1 && count < 5) {
                    int end = payload.indexOf("</title>", pos);
                    if (end == -1) break;
                    
                    if (skipFirst) {
                        skipFirst = false; // пропустить заголовок канала
                        pos = end;
                        continue;
                    }
                    
                    String title = payload.substring(pos + 7, end);
                    title.replace("<![CDATA[", "");
                    title.replace("]]>", "");
                    title.trim();
                    if (title.length() > 5) {
                        headlines += "- " + title + "\n";
                        count++;
                    }
                    pos = end;
                }
            }
        }
        http.end();
        return headlines;
    }

    // --- Direct N8N Webhook POST ---
    String askAI(const String& audioPath, const MelvinConfig& cfg) {
        if (cfg.api_proxy.length() < 10) return "Error: webhook URL not configured.";
        
        File file = SD_MMC.open(audioPath, FILE_READ);
        if (!file) return "Error: Open audio failed";
        size_t fileSize = file.size();

        // 1. Send to n8n webhook using base64 encoded JSON
        Base64JsonStream b64Stream(file);
        size_t totalLen = b64Stream.getTotalLen();

        Serial.printf("[AGENT] Sending %d bytes (JSON base64) to n8n via sendCustomPost...\n", totalLen);
        
        String responseBody = sendCustomPost(cfg.api_proxy, &b64Stream, totalLen, "application/json", "");
        if (responseBody.startsWith("Error")) {
            Serial.println("[AGENT] sendCustomPost failed: " + responseBody);
        }
        
        file.close(); // Ensure file is closed after stream


        String result;
        if (responseBody.startsWith("Error")) {
            Serial.printf("[AGENT] sendCustomPost failed: %s\n", responseBody.c_str());
            result = responseBody;
        } else {
            Serial.printf("[AGENT] Response received: %d bytes\n", responseBody.length());
            
            // Check if response is JSON (starts with {)
            if (responseBody.startsWith("{")) {
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, responseBody);
                if (error) {
                    Serial.println("[AGENT] JSON parse failed!");
                    Serial.println(responseBody);
                    result = "Error: Failed to parse n8n response JSON";
                } else {
                    const char* text_spoken = doc["text_spoken"];
                    if (text_spoken) {
                        result = String(text_spoken);
                    } else {
                        result = "Error: No text_spoken in response";
                    }
                }
            } else if (responseBody.startsWith("RIFF")) {
                // n8n returned a raw WAV file (starts with RIFF)
                // Save it to SD card as /response.wav so main.cpp can play it
                File outWav = SD_MMC.open("/response.wav", FILE_WRITE);
                if (outWav) {
                    outWav.write((const uint8_t*)responseBody.c_str(), responseBody.length());
                    outWav.close();
                    Serial.printf("[AGENT] Saved raw WAV to /response.wav: %d bytes\n", responseBody.length());
                    result = "[PLAY_WAV]";
                } else {
                    result = "Error: Failed to open /response.wav for writing";
                }
            } else {
                // Default fallback: treat as MP3 and save to /response.mp3
                File outMp3 = SD_MMC.open("/response.mp3", FILE_WRITE);
                if (outMp3) {
                    outMp3.write((const uint8_t*)responseBody.c_str(), responseBody.length());
                    outMp3.close();
                    Serial.printf("[AGENT] Saved raw MP3 to /response.mp3: %d bytes\n", responseBody.length());
                    result = "[PLAY_MP3]";
                } else {
                    result = "Error: Failed to open /response.mp3 for writing";
                }
            }
        }

        if (result.startsWith("Error")) {
            Serial.println("[AGENT] POST failed — forcing WiFi reconnect to recover sockets...");
            WiFi.disconnect();
            delay(500);
            WiFi.reconnect();
            uint32_t t = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t < 8000) {
                delay(200);
            }
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("[AGENT] WiFi reconnected OK.");
            } else {
                Serial.println("[AGENT] WiFi reconnect timeout!");
            }
        } else {
            delay(150);
        }
        
        return result;
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

        const char* alpn[] = {"http/1.1", NULL};

        if (isHttps) {
            secureClientPtr = new WiFiClientSecure();
            secureClientPtr->setInsecure();
            secureClientPtr->setAlpnProtocols(alpn);
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
                     "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\n" +
                     "Accept-Encoding: gzip, deflate\r\n";
        if (authHeader.length() > 0) {
            req += "Authorization: " + authHeader + "\r\n";
        }
        req += "Content-Type: " + contentType + "\r\n" +
               "Content-Length: " + String(payloadLength) + "\r\n" +
               "Accept: */*\r\n" +
               "Connection: close\r\n\r\n";
        
        clientPtr->print(req);

        Serial.printf("[CUSTOM_HTTP] Sending payload (%d bytes)...\n", payloadLength);
        
        // Use 4096-byte chunks to minimize TLS fragmentation and overhead
        std::vector<uint8_t> temp(4096);
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

            size_t toRead = min(remaining, temp.size());
            size_t r = payloadStream->readBytes(temp.data(), toRead);
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
                int w = clientPtr->write(temp.data() + written, r - written);
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
                
                // CRITICAL: Yield to the FreeRTOS scheduler to allow the Wi-Fi/lwIP task
                // to process incoming TCP ACKs and actually transmit the encrypted TLS buffers.
                // Without this, pushing 300KB in a tight loop starves the network stack and
                // causes errno 9 (Bad file number) disconnects.
                vTaskDelay(pdMS_TO_TICKS(15));
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



    String callSingleAPI(const String& audioPath, const String& prompt, const String& provider, const String& key, const MelvinConfig& cfg) {
        String history = getRecentHistory(2048);
        String contextPrompt = prompt;
        if (history.length() > 10) {
            contextPrompt += "\n\nПоследние реплики диалога:\n" + history;
        }

        if (provider == "gemini") {
            return callGemini(audioPath, contextPrompt, key, cfg);
        } else if (provider == "yandex") {
            String text = transcribeYandexSTT(audioPath, key, cfg);
            if (text.startsWith("Error")) return text;
            Serial.printf("[AGENT] Transcribed text (Yandex): %s\n", text.c_str());
            return callYandexGPT(contextPrompt, text, key, cfg);
        } else if (provider == "groq") {
            String text = transcribeGroqWhisper(audioPath, key, cfg);
            if (text.startsWith("Error")) return text;
            Serial.printf("[AGENT] Transcribed text: %s\n", text.c_str());
            return callGroqChat(contextPrompt, text, key, cfg);
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
            return callOpenRouterChat(contextPrompt, text, key, cfg);
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
                lastTranscription = result;
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
    String callYandexGPT(const String& systemPrompt, const String& userText, const String& key, const MelvinConfig& cfg) {
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
            JsonObject sysMsg = messages.add<JsonObject>();
            sysMsg["role"] = "system";
            sysMsg["text"] = systemPrompt;

            JsonObject userMsg = messages.add<JsonObject>();
            userMsg["role"] = "user";
            userMsg["text"] = userText;

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
            if (!gStream.isValid()) {
                file.close();
                return "Error: GeminiStream memory allocation failed";
            }
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
            if (!gStream.isValid()) {
                file.close();
                return "Error: GeminiStream memory allocation failed";
            }
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
                    lastTranscription = result;
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
                lastTranscription = result;
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
                    lastTranscription = result;
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
                    lastTranscription = result;
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
    String callGroqChat(const String& systemPrompt, const String& userText, const String& key, const MelvinConfig& cfg) {
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
            JsonObject sysMsg = doc["messages"].add<JsonObject>();
            sysMsg["role"] = "system";
            sysMsg["content"] = systemPrompt;

            JsonObject userMsg = doc["messages"].add<JsonObject>();
            userMsg["role"] = "user";
            userMsg["content"] = userText;

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
    String callOpenRouterChat(const String& systemPrompt, const String& userText, const String& key, const MelvinConfig& cfg) {
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
                JsonObject sysMsg = doc["messages"].add<JsonObject>();
                sysMsg["role"] = "system";
                sysMsg["content"] = systemPrompt;

                JsonObject userMsg = doc["messages"].add<JsonObject>();
                userMsg["role"] = "user";
                userMsg["content"] = userText;

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
