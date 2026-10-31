import struct

def inspect_elf(elf_path):
    with open(elf_path, 'rb') as f:
        data = f.read()
    
    e_shoff = struct.unpack('<Q', data[40:48])[0]
    e_shentsize = struct.unpack('<H', data[58:60])[0]
    e_shnum = struct.unpack('<H', data[60:62])[0]
    
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
        
    dynsym = None
    for s in sections:
        if s['type'] == 11:
            dynsym = s
            break
            
    strtab = sections[dynsym['link']]
    str_data = data[strtab['offset']:strtab['offset']+strtab['size']]
    
    entsize = dynsym['entsize'] if dynsym['entsize'] > 0 else 24
    num_symbols = dynsym['size'] // entsize
    
    symbols = []
    for i in range(num_symbols):
        s_off = dynsym['offset'] + i * entsize
        st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack('<IBBHQQ', data[s_off:s_off+24])
        if st_name != 0:
            end = str_data.find(b'\x00', st_name)
            name = str_data[st_name:end].decode('utf-8', errors='ignore')
            symbols.append((name, st_shndx, st_value))
            
    return symbols

syms = inspect_elf(r'D:\esp32\libc64.so')
write_syms = [s for s in syms if 'write' in s[0]]
print("Write-related symbols in libc64.so:")
for name, shndx, val in write_syms:
    print(f"  {name} (shndx={shndx}, val={hex(val)})")
