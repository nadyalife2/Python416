$adb = "C:\Program Files (x86)\OnePlus USB Drivers\Android\adb.exe"
Write-Host "Creating /data/local/tmp/llama..."
& $adb shell "mkdir -p /data/local/tmp/llama"

Write-Host "Pushing all llama binaries and libraries..."
Get-ChildItem "D:\esp32\llama-b12014" | ForEach-Object {
    & $adb push $_.FullName /data/local/tmp/llama/
}

Write-Host "Setting executable permissions..."
& $adb shell "chmod 755 /data/local/tmp/llama/llama* /data/local/tmp/llama/ggml*"

Write-Host "Testing execution..."
& $adb shell "export LD_LIBRARY_PATH=/data/local/tmp/llama; /data/local/tmp/llama/llama-cli --version"
