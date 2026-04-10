#!/bin/bash
# Скрипт для выгрузки melvin_say на GitHub

cd /workspace/melvin_say

echo "📦 Проект melvin_say готов к выгрузке!"
echo ""
echo "✅ Распиновка подтверждена из working проекта:"
echo "   - I2S_WS: GPIO45 (нет конфликта с TFT_BL)"
echo "   - I2S_BCLK: GPIO9"
echo "   - I2S_DOUT: GPIO8"
echo "   - I2S_DIN: GPIO10"
echo ""
echo "📋 Коммиты готовы:"
git log --oneline -3
echo ""
echo "🚀 Для выгрузки выполните одну из команд:"
echo ""
echo "Вариант 1: Если у вас настроен SSH доступ:"
echo "   cd /workspace/melvin_say && git push origin main"
echo ""
echo "Вариант 2: Если нужен токен GitHub:"
echo "   cd /workspace/melvin_say && git push https://YOUR_GITHUB_TOKEN@github.com/nadyalife2/melvin_say.git main"
echo ""
echo "Вариант 3: Через GitHub Desktop или VS Code Git интерфейс"
echo ""
echo "📁 Структура проекта:"
ls -la
