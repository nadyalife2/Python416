from math import sqrt


class Area:
    __count=0

    @staticmethod
    def triangle_area(a,b,c):
        p=(a+b+c)/2
        Area.__count+=1
        return sqrt(p*(p-a)*(p-b)*(p-c))

    @staticmethod
    def triangle_area_2(a,h):
        Area.__count += 1
        return 0.5*a*h

    @staticmethod
    def square_area(a):
        Area.__count += 1
        return a**2

    @staticmethod
    def rectangle_area(a, b):
        Area.__count += 1
        return a * b

    @staticmethod
    def get_count():
        return Area.__count




print("Площадь треугольника по формуле Герона", Area.triangle_area(3,4,5))
print("Площадь треугольника через основание и высоту (6,7)", Area.triangle_area_2(6, 7))
print("Площадь квадрата (7): ", Area.square_area(7))
print("Площадь прямоугольника (2, 6): ", Area.rectangle_area(2,6))
print("Количество подсчетов площади: ", Area.get_count())