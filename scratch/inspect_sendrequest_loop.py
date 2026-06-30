import os

path1 = r"C:\Users\nadya\.platformio\packages\framework-arduinoespressif32\libraries\HTTPClient\src\HTTPClient.cpp"
path2 = r"C:\Users\nadya\.platformio\packages\framework-arduinoespressif32@src-bb15c86ba309c3fc5e8fc895fe201b31\libraries\HTTPClient\src\HTTPClient.cpp"

target_path = path1 if os.path.exists(path1) else path2

if os.path.exists(target_path):
    with open(target_path, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()
        for i in range(716, min(760, len(lines))):
            print(f"L{i+1}: {lines[i].strip()}")
else:
    print("HTTPClient.cpp not found")
