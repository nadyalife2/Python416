class Rect:
    def __init__(self, width, height):
        self.width = width
        self.height = height

    def show_rect(self):
        print(f"Прямоугольник:\nШирина: {self.width}\nВысота: {self.height}")

class RectFon(Rect):
    def __init__(self, width, height, background):
        super().__init__(width, height)
        self.fon = background

    def show_rect(self):
        super().show_rect()
        print("Фон:", self.fon)


class RectBorder(Rect):
    def __init__(self, width, height, thin, typed, color):
        super().__init__(width, height)
        self.thin = thin
        self.typed = typed
        self.color = color

    def show_rect(self):
        super().show_rect()
        print("Толщина рамки:", self.thin)
        print("Тип рамки:", self.typed)
        print("Цвет рамки:", self.color)


shape1 = RectFon(400, 200, "yellow")
shape1.show_rect()
print()
shape2 = RectBorder(600, 300, "1px", "solid", "blue")
shape2.show_rect()