$adb = "C:\Program Files (x86)\OnePlus USB Drivers\Android\adb.exe"

Write-Host "Pushing patched libllama-common.so..."
& $adb push "D:\esp32\llama-b12014\libllama-common.so" /data/local/tmp/llama/

Write-Host "Pushing patched libllama-server-impl.so..."
& $adb push "D:\esp32\llama-b12014\libllama-server-impl.so" /data/local/tmp/llama/

Write-Host "Testing execution on phone..."
& $adb shell "export LD_LIBRARY_PATH=/data/local/tmp/llama; /data/local/tmp/llama/llama-server --help" | Select-Object -First 25
