import os

path = r"C:\Users\nadya\.platformio\packages\framework-arduinoespressif32\libraries\Network\src\NetworkClient.h"
if os.path.exists(path):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()
        for i in range(59, min(120, len(lines))):
            print(f"L{i+1}: {lines[i].strip()}")
else:
    print("NetworkClient.h not found")
