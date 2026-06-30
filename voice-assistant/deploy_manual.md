# Полное руководство по развертыванию локального окружения голосового ассистента

Эта инструкция содержит все команды и файлы конфигурации для развертывания системы на любой Windows-машине с нуля.

---

## Шаг 1. Установка Docker и WSL 2 (Требуются права Администратора)

Откройте **PowerShell от имени Администратора** и выполните:

```powershell
# 1. Установка Docker Desktop через Windows Package Manager
winget install Docker.DockerDesktop --accept-package-agreements --accept-source-agreements

# 2. Установка WSL 2 (подсистема Windows для Linux, необходима для Docker)
wsl.exe --install
```

> ⚠️ **ВАЖНО**: После выполнения этих команд обязательно перезагрузите компьютер!

---

## Шаг 2. Создание структуры папок

После перезагрузки откройте обычный терминал (PowerShell или CMD) и создайте папки проекта:

```powershell
# Создаем корневую папку и подпапки для n8n и Piper
mkdir C:\voice-assistant\n8n_data, C:\voice-assistant\files\piper
cd C:\voice-assistant
```
*(Вы можете выбрать любой другой диск, например `D:\voice-assistant`)*.

---

## Шаг 3. Скачивание Piper TTS и русского голоса

Перейдите в папку `files\piper` и скачайте необходимые бинарники и файлы голосов:

```powershell
cd C:\voice-assistant\files\piper

# 1. Скачивание Windows-версии Piper (для тестов прямо на ПК)
curl.exe -L -o piper_windows_amd64.zip https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_windows_amd64.zip
Expand-Archive -Path piper_windows_amd64.zip -DestinationPath . -Force
Remove-Item piper_windows_amd64.zip -Force

# 2. Скачивание Linux-версии Piper (необходима для работы внутри Docker-контейнера n8n)
curl.exe -L -o piper_linux_x86_64.tar.gz https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_linux_x86_64.tar.gz
tar.exe -xf piper_linux_x86_64.tar.gz -C .
Remove-Item piper_linux_x86_64.tar.gz -Force

# 3. Скачивание модели качественного русского голоса Ирина
curl.exe -L -o ru_RU-irina-medium.onnx https://huggingface.co/rhasspy/piper-voices/resolve/main/ru/ru_RU/irina/medium/ru_RU-irina-medium.onnx
curl.exe -L -o ru_RU-irina-medium.onnx.json https://huggingface.co/rhasspy/piper-voices/resolve/main/ru/ru_RU/irina/medium/ru_RU-irina-medium.onnx.json
```

---

## Шаг 4. Создание файла docker-compose.yml

В корневой папке (`C:\voice-assistant`) создайте файл `docker-compose.yml` со следующим содержимым:

```yaml

services:
  n8n:
    image: docker.n8n.io/n8nio/n8n:latest
    restart: unless-stopped
    ports:
      - "5678:5678"
    environment:
      - GENERIC_TIMEZONE=Europe/Samara
      - TZ=Europe/Samara
      - N8N_SECURE_COOKIE=false
      - N8N_BASIC_AUTH_ACTIVE=true
      - N8N_BASIC_AUTH_USER=admin
      - N8N_BASIC_AUTH_PASSWORD=${N8N_PASSWORD}
      # Переменные для API ключей (n8n сможет читать их из окружения)
      - GROQ_API_KEY=${GROQ_API_KEY}
      - GEMINI_API_KEY=${GEMINI_API_KEY}
    volumes:
      - ./n8n_data:/home/node/.n8n
      - ./files:/files   # Папка с Piper монтируется в корень /files контейнера
```

---

## Шаг 5. Создание файла .env

В корневой папке (`C:\voice-assistant`) создайте файл `.env`. 
Если вы хотите настроить ключи прямо в интерфейсе n8n, оставьте строки для ключей пустыми или вставьте заглушки:

```env
GROQ_API_KEY=gsk_placeholder_or_empty
GEMINI_API_KEY=placeholder_or_empty
N8N_PASSWORD=melvin_password_123
```

---

## Шаг 6. Скрипт автоматического запуска (start_n8n.cmd)

Для удобства создайте файл `start_n8n.cmd` в корневой папке `C:\voice-assistant`:

```cmd
@echo off
chcp 65001 >nul
echo ============================================================
echo Запуск локального окружения голосового ассистента (n8n + Piper)
echo ============================================================
echo.
echo Проверяем, запущен ли Docker...
docker info >nul 2>&1
if %errorlevel% neq 0 (
    echo [!] Служба Docker не запущенна!
    echo [!] Запускаем Docker Desktop...
    start "" "C:\Program Files\Docker\Docker\Docker Desktop.exe"
    echo.
    echo Пожалуйста, подождите, пока Docker Desktop запустится...
    echo Это окно автоматически продолжит запуск n8n, как только служба будет готова.
    echo.
    :loop
    timeout /t 5 >nul
    docker info >nul 2>&1
    if %errorlevel% neq 0 goto loop
)

echo [+] Docker готов! Запуск контейнеров...
docker compose up -d
if %errorlevel% neq 0 (
    echo [!] Не удалось запустить контейнеры.
    pause
    exit /b
)

echo [+] n8n успешно запущен!
echo [+] Открываем http://localhost:5678 в браузере...
start http://localhost:5678
pause
```

---

## Шаг 7. Сценарий n8n (voice_workflow.json)

Сохраните следующий JSON в файл `voice_workflow.json` в корневой папке и импортируйте его в n8n (+ -> Import from file):

```json
[Вставьте полное содержимое файла voice_workflow.json]
```
*(Полный JSON-код воркфлоу уже подготовлен и записан в вашей рабочей директории).*

---

## Шаг 8. Запуск и использование

1. Запустите скрипт `start_n8n.cmd`.
2. Перейдите в браузере по адресу `http://localhost:5678` (логин: `admin`, пароль: тот, что указан в `.env`).
3. Импортируйте воркфлоу, вставьте ваши API-ключи в HTTP-ноды (если не прописали их в `.env`) и нажмите кнопку **Active** в правом верхнем углу.
