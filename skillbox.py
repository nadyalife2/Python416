import random


class Toyota:
    color = 'red'
    price = 1e6
    max_speed = 200
    current_speed = 0

    def print_info(self):
        print("Color: {}\nPrise: {}\nMax_speed: {}\ncurrent_speed: {}\n".format(self.color, self.price, self.max_speed, self.current_speed))

    def speed(self, amount):
        self.current_speed+=amount
        print("why_speed:{}".format(amount))


first_car = Toyota()
first_car.print_info()
first_car.speed(200)

first_car.print_info()

first_car.current_speed = random.randint(0, 200)
second_car = Toyota()
second_car.current_speed = random.randint(0, 200)
third_car = Toyota()
third_car.current_speed = random.randint(0, 200)





print(first_car.current_speed, second_car.current_speed, third_car.current_speed)
