import struct

with open(r'D:\esp32\llama-b12014\libllama-server-impl.so', 'rb') as f:
    data = f.read()

e_shoff = struct.unpack('<Q', data[40:48])[0]
e_shentsize = struct.unpack('<H', data[58:60])[0]
e_shnum = struct.unpack('<H', data[60:62])[0]
e_shstrndx = struct.unpack('<H', data[62:64])[0]

sections = []
for i in range(e_shnum):
    offset = e_shoff + i * e_shentsize
    sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize = struct.unpack('<IIQQQQIIQQ', data[offset:offset+64])
    sections.append({
        'type': sh_type,
        'offset': sh_offset,
        'size': sh_size,
        'link': sh_link,
        'entsize': sh_entsize
    })

dynamic = None
for s in sections:
    if s['type'] == 6: # SHT_DYNAMIC
        dynamic = s
        break

strtab = sections[dynamic['link']]
str_data = data[strtab['offset']:strtab['offset']+strtab['size']]

num_entries = dynamic['size'] // 16
needed = []
for i in range(num_entries):
    d_tag, d_val = struct.unpack('<QQ', data[dynamic['offset']+i*16:dynamic['offset']+(i+1)*16])
    if d_tag == 1: # DT_NEEDED
        end = str_data.find(b'\x00', d_val)
        name = str_data[d_val:end].decode('utf-8', errors='ignore')
        needed.append(name)

print("DT_NEEDED entries for libllama-server-impl.so:")
for n in needed:
    print(f"  {n}")
