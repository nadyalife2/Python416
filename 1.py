class  Auto:
    model = "модель"
    year = "20хх"
    brand = "марка"
    power = "мощность"
    color_car = "цвет"
    price = "1000000"

    def print_info(self):
        print(" Данные автомобиля ".center(40,"*"))
        print(f"Название модели: {self.model}\nГод выпуска: {self.year}\n"
              f"Производитель: {self.brand}\nМощность двигателя: {self.power}\n"
              f"Цвет машины: {self.color_car}\nЦена: {self.price}"
              )
        print("="*40)

    def input_info(self, name, year,brand, power, color_car, price):
        self.model=name
        self.year=year
        self.brand=brand
        self.power=power
        self.color_car=color_car
        self.price=price


a1=Auto()
# a1.print_info()
a1.input_info("X7 M50i", "2021", "BMW", "530 л.с.", "white", "10790000")
a1.print_info()







