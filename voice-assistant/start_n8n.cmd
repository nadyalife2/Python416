@echo off
chcp 65001 >nul
echo ============================================================
echo Запуск локального окружения голосового ассистента (n8n + Piper)
echo ============================================================
echo.
echo Проверяем, запущен ли Docker...
docker info >nul 2>&1
if %errorlevel% neq 0 (
    echo [!] Служба Docker не запущена!
    echo [!] Запускаем Docker Desktop...
    start "" "C:\Program Files\Docker\Docker\Docker Desktop.exe"
    echo.
    echo Пожалуйста, подождите, пока Docker Desktop запустится 
    echo (индикатор в левом нижнем углу окна Docker должен стать зеленым).
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
    echo.
    echo [!] Не удалось запустить контейнеры. 
    echo Убедитесь, что вы заполнили API-ключи в файле .env!
    pause
    exit /b
)

echo.
echo [+] n8n успешно запущен!
echo [+] Открываем http://localhost:5678 в браузере...
start http://localhost:5678
echo.
echo Вы можете импортировать файл 'voice_workflow.json' из этой папки.
echo.
pause
