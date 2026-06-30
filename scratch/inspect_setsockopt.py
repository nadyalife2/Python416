import os

path = r"C:\Users\nadya\.platformio\packages\framework-arduinoespressif32\libraries\Network\src\NetworkClient.cpp"
if os.path.exists(path):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()
        for i, line in enumerate(lines):
            if "int NetworkClient::setSocketOption" in line:
                print(f"L{i+1}: {line.strip()}")
                for j in range(max(0, i-2), min(len(lines), i+15)):
                    print(f"  L{j+1}: {lines[j].strip()}")
else:
    print("NetworkClient.cpp not found")
