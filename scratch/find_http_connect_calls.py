import os

path1 = r"C:\Users\nadya\.platformio\packages\framework-arduinoespressif32\libraries\HTTPClient\src\HTTPClient.cpp"
path2 = r"C:\Users\nadya\.platformio\packages\framework-arduinoespressif32@src-bb15c86ba309c3fc5e8fc895fe201b31\libraries\HTTPClient\src\HTTPClient.cpp"

target_path = path1 if os.path.exists(path1) else path2

if os.path.exists(target_path):
    with open(target_path, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()
        for i, line in enumerate(lines):
            if "->connect(" in line:
                print(f"L{i+1}: {line.strip()}")
                for j in range(max(0, i-4), min(len(lines), i+8)):
                    print(f"  L{j+1}: {lines[j].strip()}")
else:
    print("HTTPClient.cpp not found")
