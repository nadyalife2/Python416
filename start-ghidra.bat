@echo off
set "JAVA_HOME=D:\esp32\jdk\jdk-21.0.13+9"
set "PATH=D:\esp32\jdk\jdk-21.0.13+9\bin;%PATH%"
set "GHIDRA_INSTALL_DIR=D:\esp32\ghidra\ghidra_12.1.4_PUBLIC"
cd /d "%GHIDRA_INSTALL_DIR%"
echo Starting Ghidra...
call "%GHIDRA_INSTALL_DIR%\support\launch.bat" fg jdk Ghidra "" "" ghidra.GhidraRun %*
