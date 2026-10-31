# Script to download and set up Ghidra, OpenJDK 21, GhidraMCP, ESP32 Flash Loader, and Python MCP bridge

$ErrorActionPreference = "Stop"

$baseDir = "D:\esp32"
Set-Location $baseDir

function Download-FileWithCurl {
    param([string]$Url, [string]$OutFile)
    Write-Host "Downloading $Url to $OutFile ..."
    & curl.exe -L -k --retry 5 --retry-delay 2 -o $OutFile $Url
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to download $Url with curl, exit code: $LASTEXITCODE"
    }
}

Write-Host "=== Step 1: Setting up OpenJDK 21 ==="
$jdkZip = Join-Path $baseDir "openjdk21.zip"
$jdkDir = Join-Path $baseDir "jdk"
if (-not (Test-Path $jdkDir)) {
    Remove-Item $jdkZip -Force -ErrorAction SilentlyContinue
    $jdkUrl = "https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.13%2B9/OpenJDK21U-jdk_x64_windows_hotspot_21.0.13_9.zip"
    Download-FileWithCurl -Url $jdkUrl -OutFile $jdkZip
    Write-Host "Extracting OpenJDK 21..."
    Expand-Archive -Path $jdkZip -DestinationPath $jdkDir
    Remove-Item -Path $jdkZip -Force
}
$jdkFolder = (Get-ChildItem $jdkDir -Directory | Select-Object -First 1).FullName
Write-Host "JDK installed at: $jdkFolder"

Write-Host "=== Step 2: Setting up Ghidra 12.1.4 ==="
$ghidraZip = Join-Path $baseDir "ghidra_12.1.4.zip"
$ghidraDir = Join-Path $baseDir "ghidra"
if (-not (Test-Path $ghidraDir)) {
    Remove-Item $ghidraZip -Force -ErrorAction SilentlyContinue
    $ghidraUrl = "https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.4_build/ghidra_12.1.4_PUBLIC_20260921.zip"
    Download-FileWithCurl -Url $ghidraUrl -OutFile $ghidraZip
    Write-Host "Extracting Ghidra 12.1.4..."
    Expand-Archive -Path $ghidraZip -DestinationPath $ghidraDir
    Remove-Item -Path $ghidraZip -Force
}
$ghidraHome = (Get-ChildItem $ghidraDir -Directory | Select-Object -First 1).FullName
Write-Host "Ghidra home: $ghidraHome"

Write-Host "=== Step 3: Installing Ghidra Extensions ==="
$extDir = Join-Path $ghidraHome "Ghidra\Extensions"
if (-not (Test-Path $extDir)) {
    New-Item -ItemType Directory -Path $extDir -Force | Out-Null
}

# 3.1 GhidraMCP extension
$ghidraMcpSrc = "D:\esp32\ghidra_mcp_test\mcp_ext\GhidraMCP"
$ghidraMcpDest = Join-Path $extDir "GhidraMCP"
if (Test-Path $ghidraMcpSrc) {
    if (Test-Path $ghidraMcpDest) { Remove-Item -Path $ghidraMcpDest -Recurse -Force }
    Copy-Item -Path $ghidraMcpSrc -Destination $ghidraMcpDest -Recurse -Force
    Write-Host "Installed GhidraMCP extension to $ghidraMcpDest"
}

# 3.2 ESP32 Flash Loader extension
$esp32Src = "D:\esp32\esp32_loader_test\ghidra-esp32-flash-loader"
$esp32Dest = Join-Path $extDir "ghidra-esp32-flash-loader"
if (Test-Path $esp32Src) {
    $propFile = Join-Path $esp32Src "extension.properties"
    if (Test-Path $propFile) {
        (Get-Content $propFile) -replace "version=11\.3\.1", "version=12.1.4" | Set-Content $propFile
    }
    if (Test-Path $esp32Dest) { Remove-Item -Path $esp32Dest -Recurse -Force }
    Copy-Item -Path $esp32Src -Destination $esp32Dest -Recurse -Force
    Write-Host "Installed ESP32 Flash Loader extension to $esp32Dest"
}

Write-Host "=== Step 4: Python Virtual Environment & Ghidra MCP Bridge ==="
$venvDir = Join-Path $baseDir ".venv"
if (-not (Test-Path $venvDir)) {
    Write-Host "Creating Python venv..."
    python -m venv $venvDir
}
$pipExe = Join-Path $venvDir "Scripts\pip.exe"
$pythonExe = Join-Path $venvDir "Scripts\python.exe"

Write-Host "Installing python dependencies (mcp, requests, wheel)..."
& $pipExe install --upgrade pip
& $pipExe install mcp requests

$mcpWheel = "D:\esp32\ghidra_mcp_test\GhidraMCP-release-2-0\ghidra_mcp-2.0.0-py3-none-any.whl"
if (Test-Path $mcpWheel) {
    & $pipExe install $mcpWheel
    Write-Host "Installed ghidra-mcp wheel."
}

# Clean temporary test folders
Remove-Item -Path "D:\esp32\ghidra_mcp_test" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "D:\esp32\esp32_loader_test" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "D:\esp32\ghidra_mcp_test.zip" -Force -ErrorAction SilentlyContinue
Remove-Item -Path "D:\esp32\esp32_loader_test.zip" -Force -ErrorAction SilentlyContinue

Write-Host "=== Step 5: Creating Helper Scripts ==="

# 5.1 start-ghidra.bat
$startGhidraBat = @"
@echo off
set "JAVA_HOME=$jdkFolder"
set "PATH=$jdkFolder\bin;%PATH%"
echo Starting Ghidra 12.1.4 with ESP32 Flash Loader and GhidraMCP...
call "$ghidraHome\ghidraRun.bat"
"@
Set-Content -Path (Join-Path $baseDir "start-ghidra.bat") -Value $startGhidraBat

# 5.2 run-ghidra-mcp.bat
$runMcpBat = @"
@echo off
set "JAVA_HOME=$jdkFolder"
set "PATH=$jdkFolder\bin;%PATH%"
set "GHIDRA_INSTALL_DIR=$ghidraHome"
"$venvDir\Scripts\ghidra-mcp.exe" --ghidra-server http://127.0.0.1:8080/ %*
"@
Set-Content -Path (Join-Path $baseDir "run-ghidra-mcp.bat") -Value $runMcpBat

# 5.3 mcp-settings.json (for IDE / AI agents)
$mcpJson = @"
{
  "mcpServers": {
    "ghidra-esp32": {
      "command": "$($venvDir.Replace('\', '\\'))\\Scripts\\ghidra-mcp.exe",
      "args": [
        "--ghidra-server",
        "http://127.0.0.1:8080/"
      ],
      "env": {
        "JAVA_HOME": "$($jdkFolder.Replace('\', '\\'))",
        "GHIDRA_INSTALL_DIR": "$($ghidraHome.Replace('\', '\\'))"
      }
    }
  }
}
"@
Set-Content -Path (Join-Path $baseDir "mcp-settings.json") -Value $mcpJson

Write-Host "Setup completed successfully!"
