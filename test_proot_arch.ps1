$url = "https://github.com/proot-me/proot/releases/download/v5.5.0/proot"
$dest = "D:\esp32\proot-v5.5.0"
& curl.exe -L -k -o $dest $url
$bytes = [System.IO.File]::ReadAllBytes($dest)
$arch = $bytes[18] # e_machine in ELF header
Write-Host "ELF magic: $([System.Text.Encoding]::ASCII.GetString($bytes, 1, 3))"
Write-Host "e_machine: $arch (183 = aarch64, 62 = x86_64)"
