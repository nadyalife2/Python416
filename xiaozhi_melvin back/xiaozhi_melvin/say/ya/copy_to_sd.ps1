Write-Host "Копирование WAV файлов на SD карту" -ForegroundColor Green
Write-Host "===================================" -ForegroundColor Green

Write-Host "`nУбедитесь, что SD карта подключена как диск (например, D:)`n"

$drive = Read-Host "Введите букву диска SD карты (например, D)"

if (-not (Test-Path "${drive}:\")) {
    Write-Host "Ошибка: Диск ${drive}: не найден!" -ForegroundColor Red
    Read-Host "Нажмите Enter для выхода"
    exit 1
}

Write-Host "`nКопирование файлов в корень SD карты..."
Copy-Item -Path "wav_files\*.wav" -Destination "${drive}:\" -Force

Write-Host "`nСоздание директории для TTS файлов..."
New-Item -ItemType Directory -Path "${drive}:\tts" -Force | Out-Null

Write-Host "Копирование TTS файлов..."
Copy-Item -Path "wav_files\*.wav" -Destination "${drive}:\tts\" -Force

Write-Host "`nСодержимое SD карты:"
Get-ChildItem "${drive}:\*.wav" | Select-Object Name, Length | Format-Table -AutoSize

Write-Host "`nГотово! SD карта подготовлена." -ForegroundColor Green
Write-Host "`n"
Read-Host "Нажмите Enter для выхода"