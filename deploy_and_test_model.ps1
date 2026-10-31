$adb = "C:\Program Files (x86)\OnePlus USB Drivers\Android\adb.exe"
$modelSource = "D:\esp32\models\DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_M.gguf"
$modelDest = "/sdcard/melvin/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_M.gguf"

Write-Host "1. Проверка файла модели на ПК..."
$item = Get-Item $modelSource
Write-Host "Размер: $($item.Length) байт ($([Math]::Round($item.Length / 1MB, 2)) МБ)"

Write-Host "`n2. Передача модели на смартфон по USB (это займет ~2-3 минуты)..."
& $adb push $modelSource $modelDest

Write-Host "`n3. Проверка файла на смартфоне..."
& $adb shell "ls -lh /sdcard/melvin/models/"

Write-Host "`n4. Тестовый запуск llama-server (1 токен-тест)..."
$testScript = @"
#!/bin/sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
echo "Запуск тестовой генерации llama-cli на Cortex-A72 (-t 2)..."
llama-cli -m /melvin/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_M.gguf \
          -p "Ты Рик Санчез. Ответь дерзко и кратко: кто ты и где находишься?" \
          -n 64 \
          -t 2 \
          --prompt-cache /melvin/data/test.cache
"@
$testScript | Out-File -FilePath "D:\esp32\test_cli.sh" -Encoding ascii
& $adb push "D:\esp32\test_cli.sh" /data/local/tmp/alpine/root/test_cli.sh
& $adb shell "chmod 755 /data/local/tmp/alpine/root/test_cli.sh"

Write-Host "`n5. Исполнение на чипе Helio X20..."
& $adb shell "PROOT_TMP_DIR=/data/local/tmp /data/local/tmp/proot -r /data/local/tmp/alpine -0 -b /dev -b /proc -b /sys -b /sdcard/melvin:/melvin /bin/busybox sh /root/test_cli.sh"
