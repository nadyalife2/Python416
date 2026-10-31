import urllib.request
import os
import sys

url = "https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf"
dest = "D:/esp32/qwen2.5-0.5b-instruct-q4_k_m.gguf"

if os.path.exists(dest) and os.path.getsize(dest) == 491400032:
    print("Already downloaded:", dest)
    sys.exit(0)

print(f"Downloading {url} to {dest}...")

def reporthook(blocknum, blocksize, totalsize):
    read = blocknum * blocksize
    if totalsize > 0:
        percent = read * 100 / totalsize
        if blocknum % 1000 == 0:
            print(f"Downloaded: {read // (1024*1024)}MB / {totalsize // (1024*1024)}MB ({percent:.1f}%)")

urllib.request.urlretrieve(url, dest, reporthook)
print("Download complete! Size:", os.path.getsize(dest))
