import os
file1=r"nested1\text.txt"
nested = os.path.exists(file1)
if nested != 0:
    print("text.txt",end= " ")
    print(os.path.dirname(file1),end= " ")
    print("last access time", os.path.getatime(file1),"sec")
else:
    print("Файл не существует")
