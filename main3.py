import os
file1=r"nested1\test.txt"
nested = os.path.exists(file1)
if nested != 0:
    print("test.txt",end= " ")
    print(os.path.dirname(file1),end= " ")
    print("last access time", os.path.getatime(file1))
else:
    print("Файл не существует")
