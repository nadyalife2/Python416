import os

bdir = r'D:\esp32\llama-b12014'
for f in os.listdir(bdir):
    fp = os.path.join(bdir, f)
    if os.path.isfile(fp):
        with open(fp, 'rb') as fp_obj:
            d = fp_obj.read()
        if b'__write_chk\x00' in d:
            print(f"File {f} contains __write_chk")
