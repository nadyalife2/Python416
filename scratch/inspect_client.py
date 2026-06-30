import os

path = r"C:\Users\nadya\.platformio\packages\framework-arduinoespressif32\cores\esp32\Client.h"
if os.path.exists(path):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()
        for i, line in enumerate(lines):
            if "setTimeout" in line or "class Client" in line:
                print(f"L{i+1}: {line.strip()}")
                for j in range(max(0, i-3), min(len(lines), i+4)):
                    print(f"  L{j+1}: {lines[j].strip()}")
else:
    print("Client.h not found")
