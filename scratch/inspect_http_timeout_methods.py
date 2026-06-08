import os

path1 = r"C:\Users\nadya\.platformio\packages\framework-arduinoespressif32\libraries\HTTPClient\src\HTTPClient.h"
path2 = r"C:\Users\nadya\.platformio\packages\framework-arduinoespressif32@src-bb15c86ba309c3fc5e8fc895fe201b31\libraries\HTTPClient\src\HTTPClient.h"

target_path = path1 if os.path.exists(path1) else path2

if os.path.exists(target_path):
    with open(target_path, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()
        for i, line in enumerate(lines):
            if "Timeout" in line or "timeout" in line:
                print(f"L{i+1}: {line.strip()}")
else:
    print("HTTPClient.h not found")
