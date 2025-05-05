class Clock:
    __DAY = 86400

    def __init__(self, sec: int):
        if not isinstance(sec, int):
            raise ValueError("Секунды должны быть целым числом")
        self.sec = sec % self.__DAY

    def get_format_time(self):
        s = self.sec % 60
        m = (self.sec // 60) % 60
        h = (self.sec // 3600) % 24
        return f"{Clock.__get_form(h)}:{Clock.__get_form(m)}:{Clock.__get_form(s)}"

    @staticmethod
    def __get_form(x):
        return str(x) if x > 9 else "0" + str(x)

    def __add__(self, other):
        if not isinstance(other, Clock):
            raise ArithmeticError("Правый операнд должен быть типом Clock")
        return Clock(self.sec + other.sec)

    def __eq__(self, other):
        if not isinstance(other, Clock):
            raise ArithmeticError("Правый операнд должен быть типом Clock")
        return self.sec == other.sec

    def __sub__(self, other):
        if not isinstance(other, Clock):
            raise ArithmeticError("Правый операнд должен быть типом Clock")
        return Clock(self.sec - other.sec)

    def __mul__(self, other):
        if not isinstance(other, Clock):
            raise ArithmeticError("Правый операнд должен быть типом Clock")
        return Clock(self.sec * other.sec)

    def __floordiv__(self, other):
        if not isinstance(other, Clock):
            raise ArithmeticError("Правый операнд должен быть типом Clock")
        return Clock(self.sec // other.sec)

    def __mod__(self, other):
        if not isinstance(other, Clock):
            raise ArithmeticError("Правый операнд должен быть типом Clock")
        return Clock(self.sec % other.sec)

    def __ne__(self, other):
        return not self.__eq__(other)

    def __lt__(self, other):
        if not isinstance(other, Clock):
            raise ArithmeticError("Правый операнд должен быть типом Clock")
        return self.sec < other.sec

    def __le__(self, other):
        if not isinstance(other, Clock):
            raise ArithmeticError("Правый операнд должен быть типом Clock")
        return self.sec <= other.sec

    def __gt__(self, other):
        if not isinstance(other, Clock):
            raise ArithmeticError("Правый операнд должен быть типом Clock")
        return self.sec > other.sec

    def __ge__(self, other):
        if not isinstance(other, Clock):
            raise ArithmeticError("Правый операнд должен быть типом Clock")
        return self.sec >= other.sec

    # Новые методы для перегрузки операторов составного присваивания

    def __isub__(self, other):
        """Перегрузка оператора -="""
        if not isinstance(other, Clock):
            raise ArithmeticError("Правый операнд должен быть типом Clock")
        self.sec = (self.sec - other.sec) % self.__DAY
        return self

    def __imul__(self, other):
        """Перегрузка оператора *="""
        if not isinstance(other, Clock):
            raise ArithmeticError("Правый операнд должен быть типом Clock")
        self.sec = (self.sec * other.sec) % self.__DAY
        return self

    def __ifloordiv__(self, other):
        """Перегрузка оператора //="""
        if not isinstance(other, Clock):
            raise ArithmeticError("Правый операнд должен быть типом Clock")
        if other.sec == 0:
            raise ZeroDivisionError("Деление на ноль")
        self.sec = (self.sec // other.sec) % self.__DAY
        return self

    def __imod__(self, other):
        """Перегрузка оператора %="""
        if not isinstance(other, Clock):
            raise ArithmeticError("Правый операнд должен быть типом Clock")
        if other.sec == 0:
            raise ZeroDivisionError("Деление на ноль")
        self.sec = (self.sec % other.sec) % self.__DAY
        return self

c1 = Clock(600)
c2 = Clock(120)
c3 = c1 + c2
c4= c1-c2
c5=c1*c2
c6=c1//c2
c7=c1%c2

print("c1:", c1.get_format_time())
print("c2:",c2.get_format_time())
# c1 += c2
print("c1+c2:",c3.get_format_time())
print("c1-c2:", c4.get_format_time())
print("c1-c2:", c4.get_format_time())
print("c1*c2:", c5.get_format_time())
print("c1//c2:", c6.get_format_time())
print("c1%c2:", c7.get_format_time())
if c1 == c2:
    print("Время равно")
else:
    print("Время разное")

if c1 != c2:
    print("Время разное")
else:
    print("Время равно")

if c1<c2:
    print("То при вычитании получится отрицательное число")
else:
    print("с1>с2")

if c1 <= c2:
    print("Оператор С1 меньше С2 или равен ему")

if c1>c2:
     print("Оператор С1 > С2")

if c1>=c2:
    print("c1>=c2")

print("c1 до:", c1.get_format_time())

c1_copy = Clock(c1.sec)  # Создаем копию c1 для демонстрации
c1_copy -= c2
print("c1 после c1 -= c2:", c1_copy.get_format_time())

c1_copy = Clock(c1.sec)  # Сбрасываем к исходному значению
c1_copy *= c2
print("c1 после c1 *= c2:", c1_copy.get_format_time())

c1_copy = Clock(c1.sec)
c1_copy //= c2
print("c1 после c1 //= c2:", c1_copy.get_format_time())

c1_copy = Clock(c1.sec)
c1_copy %= c2
print("c1 после c1 %= c2:", c1_copy.get_format_time())

c1_copy = Clock(c1.sec)
c1_copy += c2
print("c1 после c1 += c2:", c1_copy.get_format_time())