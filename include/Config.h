#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>

struct MelvinConfig {
    String wifi_ssid;
    String wifi_pass;
    
    String gemini_keys;     
    String groq_keys;
    String openrouter_keys;
    String yandex_keys;
    
    String llm_provider; 
    String tts_provider; 
    String tts_key;
    String tts_voice;    
    String rss_url;
    String personality; // "rick", "calm", "podcast"
    String wake_word;    // e.g., "Мелвин"
    String system_prompt; // Custom prompt if none of the above
    String api_proxy;    // Local API proxy to bypass geoblocking (e.g. "http://192.168.31.123:8080")
    
    MelvinConfig() {
        llm_provider = "groq";    // Gemini геоблокирован в России, Groq работает
        tts_provider = "none";    // Google TTS требует OAuth (не API ключ), используем WAV фразы
        tts_voice = "ru-RU-Wavenet-B";
        personality = "rick";
        wake_word = "Мелвин";
        rss_url = "https://lenta.ru/rss/news";
        api_proxy = "http://192.168.31.123:8080";
    }

    String getEffectivePrompt() const {
        String base = "";
        if (personality == "rick") {
            base = "Ты - Мелвин, робот с характером Рика Санчеза. Сарказм, цинизм, ненависть к 'кожаным мешкам'. Кратко, 1-3 предложения.";
        } else if (personality == "calm") {
            base = "Ты - Мелвин, спокойный и вежливый робот-помощник. Твой тон мягкий и дружелюбный. Отвечай кратко на русском.";
        } else if (personality == "podcast") {
            base = "Ты - Мелвин, ведущий подкаста. Твой стиль - увлекательный рассказ, живая речь, много деталей из новостей. Говори как профессиональный диктор.";
        } else {
            base = system_prompt;
        }
        return "Твое имя: " + wake_word + ". " + base;
    }
};

class ConfigManager {
public:
    MelvinConfig config;

    bool load() {
        if (!SD_MMC.exists("/config.json")) return false;
        File file = SD_MMC.open("/config.json", FILE_READ);
        if (!file) return false;
        JsonDocument doc;
        deserializeJson(doc, file);
        file.close();

        config.wifi_ssid = doc["wifi_ssid"] | "";
        config.wifi_pass = doc["wifi_pass"] | "";
        config.gemini_keys = doc["gemini_keys"] | "";
        config.groq_keys = doc["groq_keys"] | "";
        config.openrouter_keys = doc["openrouter_keys"] | "";
        config.yandex_keys = doc["yandex_keys"] | "";
        config.llm_provider = doc["llm_provider"] | "groq";
        config.tts_provider = doc["tts_provider"] | "none";
        config.tts_key = doc["tts_key"] | "";
        config.tts_voice = doc["tts_voice"] | "ru-RU-Wavenet-B";
        config.personality = doc["personality"] | "rick";
        config.wake_word = doc["wake_word"] | "Мелвин";
        config.rss_url = doc["rss_url"] | "https://lenta.ru/rss/news";
        config.system_prompt = doc["system_prompt"] | "";
        
        String proxy = doc["api_proxy"] | "http://192.168.31.123:8080";
        proxy.trim();
        if (proxy.endsWith("/")) {
            proxy = proxy.substring(0, proxy.length() - 1);
        }
        // Remove auto-migration to http:// since Cloudflare Workers enforce HTTPS.
        // We fixed the lwIP MTU to 1300, which resolves the HTTPS packet drop issue.
        config.api_proxy = proxy;
        Serial.printf("[CONFIG] Loaded api_proxy from SD: '%s'\n", config.api_proxy.c_str());

        // (Removed auto-migration for gemini since we use a proxy now)

        return true;
    }

    bool save() {
        File file = SD_MMC.open("/config.json", FILE_WRITE);
        if (!file) return false;

        String proxy = config.api_proxy;
        proxy.trim();
        if (proxy.endsWith("/")) {
            proxy = proxy.substring(0, proxy.length() - 1);
        }
        config.api_proxy = proxy;

        JsonDocument doc;
        doc["wifi_ssid"] = config.wifi_ssid;
        doc["wifi_pass"] = config.wifi_pass;
        doc["gemini_keys"] = config.gemini_keys;
        doc["groq_keys"] = config.groq_keys;
        doc["openrouter_keys"] = config.openrouter_keys;
        doc["yandex_keys"] = config.yandex_keys;
        doc["llm_provider"] = config.llm_provider;
        doc["tts_provider"] = config.tts_provider;
        doc["tts_key"] = config.tts_key;
        doc["tts_voice"] = config.tts_voice;
        doc["personality"] = config.personality;
        doc["wake_word"] = config.wake_word;
        doc["rss_url"] = config.rss_url;
        doc["system_prompt"] = config.system_prompt;
        doc["api_proxy"] = config.api_proxy;
        serializeJson(doc, file);
        file.close();
        return true;
    }

    void resetWiFi() {
        config.wifi_ssid = "";
        config.wifi_pass = "";
        save();
    }
};

#endif
