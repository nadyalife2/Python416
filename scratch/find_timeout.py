import os

def find_set_timeout():
    search_paths = [
        r"C:\Users\nadya\.platformio",
        # also search project's local .pio if any
        r"D:\xiaoshi\say-ya\python416\.pio"
    ]
    
    found = False
    for path in search_paths:
        if not os.path.exists(path):
            continue
        print(f"Searching in: {path}")
        for root, dirs, files in os.walk(path):
            for file in files:
                if file in ["NetworkClient.h", "NetworkClientSecure.h", "WiFiClient.h", "WiFiClientSecure.h", "Client.h"]:
                    filepath = os.path.join(root, file)
                    try:
                        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                            content = f.read()
                            if "setTimeout" in content:
                                print(f"Found setTimeout in {filepath}:")
                                for line in content.splitlines():
                                    if "setTimeout" in line:
                                        print(f"  {line.strip()}")
                                found = True
                    except Exception as e:
                        pass
    if not found:
        print("Not found.")

if __name__ == "__main__":
    find_set_timeout()
