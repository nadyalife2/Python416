import os

def search():
    root_dir = r"C:\Users\nadya\.platformio\packages"
    for r, d, files in os.walk(root_dir):
        for f in files:
            if f.endswith(".h") or f.endswith(".cpp"):
                path = os.path.join(r, f)
                try:
                    with open(path, "r", encoding="utf-8", errors="ignore") as file:
                        if "WIFI_CLIENT_DEF_CONN_TIMEOUT_MS" in file.read():
                            print(f"Found in {path}")
                            file.seek(0)
                            for line in file.readlines():
                                if "WIFI_CLIENT_DEF_CONN_TIMEOUT_MS" in line:
                                    print("  ", line.strip())
                except:
                    pass

if __name__ == "__main__":
    search()
