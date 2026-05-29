@echo off
echo Копирование WAV файлов на SD карту
echo ===================================

echo.
echo Убедитесь, что SD карта подключена как диск (например, D:)
echo.

set /p DRIVE="Введите букву диска SD карты (например, D): "

if not exist "%DRIVE%:\" (
    echo Ошибка: Диск %DRIVE%: не найден!
    pause
    exit /b 1
)

echo.
echo Копирование файлов в корень SD карты...
xcopy /Y wav_files\*.wav "%DRIVE%:\"

echo.
echo Создание директории для TTS файлов...
mkdir "%DRIVE%:\tts" 2>nul

echo Копирование TTS файлов...
xcopy /Y wav_files\*.wav "%DRIVE%:\tts\" 2>nul

echo.
echo Содержимое SD карты:
dir "%DRIVE%:\*.wav" /B

echo.
echo Готово! SD карта подготовлена.
echo.
pause