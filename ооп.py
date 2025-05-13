class Cat:
    # Атрибуты (данные кошки)
    name = ""
    color = ""
    weight = 0

    # Метод (действие кошки)
    def meow(self):
        print(self.name, "говорит Мяу!")

my_cat = Cat()
my_cat.name = "Мурка"
my_cat.color = "рыжий"
my_cat.weight = 5

my_cat.meow()  # Выведет: Мурка говорит Мяу!