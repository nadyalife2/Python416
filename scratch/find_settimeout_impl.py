import os

def search():
    root_dir = r"C:\Users\nadya\.platformio\packages\framework-arduinoespressif32"
    if not os.path.exists(root_dir):
        print("PlatformIO packages path not found")
        return
        
    for r, d, files in os.walk(root_dir):
        for f in files:
            if f.endswith(".cpp"):
                path = os.path.join(r, f)
                try:
                    with open(path, "r", encoding="utf-8", errors="ignore") as file:
                        content = file.read()
                        if "setTimeout" in content and "::setTimeout" in content:
                            print(f"File: {path}")
                            for line in content.splitlines():
                                if "::setTimeout" in line:
                                    print("  ", line.strip())
                except:
                    pass

if __name__ == "__main__":
    search()
