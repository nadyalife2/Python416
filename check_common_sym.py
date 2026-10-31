import struct

def inspect_syms(elf_path, query):
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
        
    dynsym = [s for s in sections if s['type'] == 11][0]
    strtab = sections[dynsym['link']]
    str_data = data[strtab['offset']:strtab['offset']+strtab['size']]
    
    entsize = dynsym['entsize'] if dynsym['entsize'] > 0 else 24
    num_symbols = dynsym['size'] // entsize
    
    for i in range(num_symbols):
        s_off = dynsym['offset'] + i * entsize
        st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack('<IBBHQQ', data[s_off:s_off+24])
        if st_name != 0:
            end = str_data.find(b'\x00', st_name)
            name = str_data[st_name:end].decode('utf-8', errors='ignore')
            if query in name:
                print(f"[{elf_path}] #{i}: '{name}', shndx={st_shndx}, val={hex(st_value)}")

inspect_syms(r'D:\esp32\llama-b12014\libllama-common.so', 'write')
