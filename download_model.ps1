$modelUrl = "https://huggingface.co/bartowski/DeepSeek-R1-Distill-Qwen-1.5B-GGUF/resolve/main/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_M.gguf"
$destDir = "D:\esp32\models"
if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Path $destDir | Out-Null }
$destFile = "$destDir\DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_M.gguf"

Write-Host "Downloading DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_M.gguf..."
& curl.exe -L -k --retry 5 -C - -o $destFile $modelUrl

Write-Host "Download complete. File size:"
Get-Item $destFile | Select-Object Name, Length
