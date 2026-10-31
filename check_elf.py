import struct
import sys

def get_undefined_symbols(elf_path):
    with open(elf_path, 'rb') as f:
        data = f.read()
    
    if data[:4] != b'\x7fELF':
        print("Not an ELF file")
        return
    
    is_64 = data[4] == 2
    if not is_64:
        print("Not 64-bit")
        return
        
    e_shoff = struct.unpack('<Q', data[40:48])[0]
    e_shentsize = struct.unpack('<H', data[58:60])[0]
    e_shnum = struct.unpack('<H', data[60:62])[0]
    e_shstrndx = struct.unpack('<H', data[62:64])[0]
    
    # Read sections
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
        
    # Find SHT_DYNSYM (11) and SHT_STRTAB linked to it
    dynsym = None
    for s in sections:
        if s['type'] == 11:
            dynsym = s
            break
            
    if not dynsym:
        print("No dynsym section")
        return
        
    strtab = sections[dynsym['link']]
    str_data = data[strtab['offset']:strtab['offset']+strtab['size']]
    
    entsize = dynsym['entsize'] if dynsym['entsize'] > 0 else 24
    num_symbols = dynsym['size'] // entsize
    
    undef_syms = []
    for i in range(num_symbols):
        s_off = dynsym['offset'] + i * entsize
        st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack('<IBBHQQ', data[s_off:s_off+24])
        if st_shndx == 0 and st_name != 0: # SHN_UNDEF
            # read name
            end = str_data.find(b'\x00', st_name)
            name = str_data[st_name:end].decode('utf-8', errors='ignore')
            undef_syms.append(name)
            
    return undef_syms

if __name__ == '__main__':
    syms = get_undefined_symbols(sys.argv[1])
    print(f"Total undefined symbols: {len(syms)}")
    bionic_syms = [s for s in syms if s.startswith('__') or 'chk' in s or s.startswith('pthread_')]
    print("Special / Fortify / pthread symbols:")
    for s in sorted(set(bionic_syms)):
        print(f"  {s}")
