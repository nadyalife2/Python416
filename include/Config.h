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
    
    String llm_provider; 
    String tts_provider; 
    String tts_key;
    String tts_voice;    
    String rss_url;
    String personality; // "rick", "calm", "podcast"
    String wake_word;    // e.g., "Мелвин"
    String system_prompt; // Custom prompt if none of the above
    
    MelvinConfig() {
        llm_provider = "gemini";
        tts_provider = "google";
        tts_voice = "ru-RU-Wavenet-B";
        personality = "rick";
        wake_word = "Мелвин";
        rss_url = "https://lenta.ru/rss/news";
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
        config.llm_provider = doc["llm_provider"] | "gemini";
        config.tts_provider = doc["tts_provider"] | "google";
        config.tts_key = doc["tts_key"] | "";
        config.tts_voice = doc["tts_voice"] | "ru-RU-Wavenet-B";
        config.personality = doc["personality"] | "rick";
        config.wake_word = doc["wake_word"] | "Мелвин";
        config.rss_url = doc["rss_url"] | "https://lenta.ru/rss/news";
        config.system_prompt = doc["system_prompt"] | "";
        return true;
    }

    bool save() {
        File file = SD_MMC.open("/config.json", FILE_WRITE);
        if (!file) return false;
        JsonDocument doc;
        doc["wifi_ssid"] = config.wifi_ssid;
        doc["wifi_pass"] = config.wifi_pass;
        doc["gemini_keys"] = config.gemini_keys;
        doc["groq_keys"] = config.groq_keys;
        doc["openrouter_keys"] = config.openrouter_keys;
        doc["llm_provider"] = config.llm_provider;
        doc["tts_provider"] = config.tts_provider;
        doc["tts_key"] = config.tts_key;
        doc["tts_voice"] = config.tts_voice;
        doc["personality"] = config.personality;
        doc["wake_word"] = config.wake_word;
        doc["rss_url"] = config.rss_url;
        doc["system_prompt"] = config.system_prompt;
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
