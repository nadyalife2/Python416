f = open("test3.txt", "w")
f.write("Замена строки в текстовом файле;\nизменить строку в списке;\nзаписать список в файл;\n")
f.close()

f = open("test3.txt", "r")
read_line = f.readlines()
print(read_line)
f.close()

pos1 = int(input("pos1 = "))
pos2 = int(input("pos2 = "))

if 0 <= pos1 < len(read_line) and 0 <= pos2 < len(read_line):
    read_line[pos1], read_line[pos2] = read_line[pos2],  read_line[pos1]
else:
    print("Такой строки нет")

print(read_line)

f = open("test3.txt", "w")
f.writelines(read_line)
f.close()