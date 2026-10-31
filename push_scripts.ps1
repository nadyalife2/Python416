$adb = "C:\Program Files (x86)\OnePlus USB Drivers\Android\adb.exe"
& $adb push "D:\esp32\start_melvin.sh" /sdcard/melvin/start_melvin.sh
& $adb push "D:\esp32\agent_orchestrator.py" /sdcard/melvin/agent_orchestrator.py
& $adb push "D:\esp32\phone_tools.py" /sdcard/melvin/phone_tools.py
& $adb push "D:\esp32\melvin_react.gbnf" /sdcard/melvin/melvin_react.gbnf
& $adb shell "chmod 755 /sdcard/melvin/start_melvin.sh"
Write-Host "Files pushed to phone successfully."
