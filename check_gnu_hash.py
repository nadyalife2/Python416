import struct

with open(r'D:\esp32\llama-b12014\libllama-server-impl.so', 'rb') as f:
    data = f.read()

# .gnu.hash at 0x19738
# struct GnuHashHeader {
#   uint32_t nbuckets;
#   uint32_t symoffset;
#   uint32_t bloom_size;
#   uint32_t bloom_shift;
# };
nbuckets, symoffset, bloom_size, bloom_shift = struct.unpack('<IIII', data[0x19738:0x19738+16])
print(f"nbuckets: {nbuckets}, symoffset: {symoffset}, bloom_size: {bloom_size}, bloom_shift: {bloom_shift}")
