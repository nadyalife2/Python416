@echo off
set "JAVA_HOME=D:\esp32\jdk\jdk-21.0.13+9"
set "PATH=D:\esp32\jdk\jdk-21.0.13+9\bin;%PATH%"
set "GHIDRA_INSTALL_DIR=D:\esp32\ghidra\ghidra_12.1.4_PUBLIC"
"D:\esp32\.venv\Scripts\ghidra-mcp.exe" --ghidra-server http://127.0.0.1:8080/ %*
