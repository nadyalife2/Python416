with open(r'D:\esp32\llama-b12014\libllama-server-impl.so', 'rb') as f:
    data = f.read()

count = data.count(b'__write_chk\x00')
print(f"Occurrences of '__write_chk\\0': {count}")
pos = 0
for _ in range(count):
    pos = data.find(b'__write_chk\x00', pos)
    print(f"Found at offset: {hex(pos)}")
    pos += 1
