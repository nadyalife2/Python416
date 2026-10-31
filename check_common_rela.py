import struct

with open(r'D:\esp32\llama-b12014\libllama-common.so', 'rb') as f:
    data = f.read()

e_shoff = struct.unpack('<Q', data[40:48])[0]
e_shentsize = struct.unpack('<H', data[58:60])[0]
e_shnum = struct.unpack('<H', data[60:62])[0]
e_shstrndx = struct.unpack('<H', data[62:64])[0]

shstr_hdr = e_shoff + e_shstrndx * e_shentsize
_, _, _, _, shstr_off, shstr_sz, _, _, _, _ = struct.unpack('<IIQQQQIIQQ', data[shstr_hdr:shstr_hdr+64])
shstr_tab = data[shstr_off:shstr_off+shstr_sz]

for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize = struct.unpack('<IIQQQQIIQQ', data[off:off+64])
    end = shstr_tab.find(b'\x00', sh_name)
    name = shstr_tab[sh_name:end].decode('utf-8', errors='ignore')
    if sh_type in (4, 9):
        num_relas = sh_size // 24
        for r in range(num_relas):
            r_off, r_info, r_addend = struct.unpack('<QQq', data[sh_offset+r*24:sh_offset+(r+1)*24])
            sym_idx = r_info >> 32
            if sym_idx == 562: # __write_chk in libllama-common.so
                print(f"libllama-common.so: Found rela for sym 562 at off={hex(sh_offset+r*24)}: r_off={hex(r_off)}, type={r_info & 0xffffffff}")
