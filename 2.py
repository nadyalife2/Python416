import re

adr = "123456@i.ru, 123_456@ru.name.ru, login@i.ru, логин-1@i.ru, login.3-67@i.ru, 1login@ru.name.ru"
reg = r"[a-zA-ZА-я.0-9_-]+@\w.+"

print(re.findall(reg, adr))
