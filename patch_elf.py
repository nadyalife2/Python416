import os

files = [
    r'D:\esp32\llama-b12014\libllama-common.so',
    r'D:\esp32\llama-b12014\libllama-server-impl.so'
]

for fp in files:
    with open(fp, 'rb') as f:
        data = f.read()
    
    target = b'__write_chk\x00'
    replacement = b'write\x00\x00\x00\x00\x00\x00\x00'
    
    if target in data:
        new_data = data.replace(target, replacement)
        with open(fp, 'wb') as f:
            f.write(new_data)
        print(f"Successfully patched {os.path.basename(fp)}")
    else:
        print(f"Target not found in {os.path.basename(fp)}")
