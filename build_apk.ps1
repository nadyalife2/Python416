param(
    [switch]$Install,
    [string]$DeviceId = "7LS8SKLV55IZSK7S"
)

$ErrorActionPreference = "Stop"

function Assert-LastExitCode($stepName) {
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Step failed: $stepName (Exit code: $LASTEXITCODE)"
        exit $LASTEXITCODE
    }
}

$appDir = "d:\esp32\melvin_android_app"
$buildDir = "$appDir\build"
$genDir = "$buildDir\gen"
$classesDir = "$buildDir\classes"

$javaHome = "d:\esp32\jdk\jdk-21.0.13+9"
$javac = "$javaHome\bin\javac.exe"
$keytool = "$javaHome\bin\keytool.exe"
$jar = "$javaHome\bin\jar.exe"

$sdk = "C:\Users\MSI\AppData\Local\Android\Sdk"
$buildTools = "$sdk\build-tools\36.1.0"
$aapt2 = "$buildTools\aapt2.exe"
$d8 = "$buildTools\d8.bat"
$zipalign = "$buildTools\zipalign.exe"
$apksigner = "$buildTools\apksigner.bat"
$androidJar = "$sdk\platforms\android-34\android.jar"
$adb = "C:\Users\MSI\AppData\Local\Android\Sdk\platform-tools\adb.exe"

$env:JAVA_HOME = $javaHome
$env:PATH = "$javaHome\bin;$env:PATH"

Write-Host "=== 1. PREPARING BUILD DIRS ==="
if (Test-Path $buildDir) { Remove-Item -Recurse -Force $buildDir }
New-Item -ItemType Directory -Path $genDir, $classesDir -Force | Out-Null

Write-Host "=== 2. COMPILING RESOURCES (AAPT2) ==="
& $aapt2 compile --dir "$appDir\res" -o "$buildDir\resources.zip"
Assert-LastExitCode "aapt2 compile"

Write-Host "=== 3. LINKING APK & GENERATING R.JAVA ==="
& $aapt2 link -I $androidJar `
    --manifest "$appDir\AndroidManifest.xml" `
    -o "$buildDir\app-unaligned.apk" `
    --java $genDir `
    "$buildDir\resources.zip" `
    --auto-add-overlay
Assert-LastExitCode "aapt2 link"

Write-Host "=== 4. COMPILING JAVA SOURCES ==="
$rJava = Get-ChildItem -Path $genDir -Filter "R.java" -Recurse | Select-Object -ExpandProperty FullName
$srcJava = Get-ChildItem -Path "$appDir\src" -Filter "*.java" -Recurse | Select-Object -ExpandProperty FullName

& $javac -encoding UTF-8 -source 8 -target 8 -bootclasspath $androidJar -d $classesDir $rJava $srcJava
Assert-LastExitCode "javac"

Write-Host "=== 5. DEXING WITH D8 (MIN-API 23) ==="
$classFiles = Get-ChildItem -Path $classesDir -Filter "*.class" -Recurse | Select-Object -ExpandProperty FullName
& $d8 --min-api 23 --lib $androidJar --output $buildDir $classFiles
Assert-LastExitCode "d8"

if (-not (Test-Path "$buildDir\classes.dex")) {
    throw "classes.dex was not generated!"
}

Write-Host "=== 6. ADDING CLASSES.DEX TO APK ==="
Push-Location $buildDir
try {
    & $jar uf "app-unaligned.apk" "classes.dex"
    Assert-LastExitCode "jar uf classes.dex"
} finally {
    Pop-Location
}

Write-Host "=== 7. ZIPALIGN ==="
& $zipalign -f -p 4 "$buildDir\app-unaligned.apk" "$buildDir\MelvinAI.apk"
Assert-LastExitCode "zipalign"

Write-Host "=== 8. SIGNING APK WITH DEBUG KEYSTORE ==="
$keystore = "$appDir\debug.keystore"
if (-not (Test-Path $keystore)) {
    & $keytool -genkeypair -v -keystore $keystore -alias androiddebugkey -keypass android -storepass android -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
    Assert-LastExitCode "keytool"
}

& $apksigner sign --ks $keystore --ks-pass pass:android --key-pass pass:android --ks-key-alias androiddebugkey "$buildDir\MelvinAI.apk"
Assert-LastExitCode "apksigner sign"

Write-Host "=== VERIFYING APK SIGNATURE ==="
& $apksigner verify -v "$buildDir\MelvinAI.apk"
Assert-LastExitCode "apksigner verify"

Write-Host "=== BUILD SUCCESSFUL ==="
$apk = Get-Item "$buildDir\MelvinAI.apk"
Write-Host "Output APK: $($apk.FullName) ($($apk.Length) bytes)"

if ($Install) {
    Write-Host "=== 9. INSTALLING TO DEVICE ($DeviceId) ==="
    & $adb -s $DeviceId install -r "$buildDir\MelvinAI.apk"
    Assert-LastExitCode "adb install"

    Write-Host "=== 10. LAUNCHING APP ==="
    & $adb -s $DeviceId shell am start -n ai.melvin.app/.MainActivity
    Assert-LastExitCode "adb shell am start"
    Write-Host "App launched successfully!"
}
