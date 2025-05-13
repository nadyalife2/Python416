import random


class Monster:
    max_hp = 150
    start_power = 10

    def __init__(self, name):
        self.name = name
        self.__hp = self.max_hp
        self.__power = self.start_power
        self.__is_alive = True

    def get_hp(self):
        return self.__hp

    def set_hp(self, new_value):
        self.__hp = max(new_value, 0)

    def get_power(self):
        return self.__power

    def set_power(self, new_power):
        self.__power = new_power

    def attack(self, target):
        pass

    def is_alive(self):
        return self.__is_alive

    def take_damage(self, damage):
        print("\t", self.name, "Получил удар с силой равной = ", round(damage), ". Осталось здоровья - ", round(self.get_hp()))
        if self.get_hp() <= 0:
            self.__is_alive = False

    def make_a_move(self, friends, enemies):
        pass

    def __str__(self):
        return 'Name: {0} | HP: {1}'.format(self.name, self.get_hp())


class MonsterBerserk(Monster):

    def __init__(self, name):
        super().__init__(name)
        self.madness = 1

    def attack(self, target):
        target.take_damage(self.get_power() * self.madness)
        self.madness += 0.1

    def take_damage(self, power):
        self.set_hp(self.get_hp() - power * (self.madness / 2))
        if self.get_hp() < 50:
            self.madness *= 2
        super().take_damage(power)

    def make_a_move(self, friends, enemies):
        print(self.name, end=' ')
        self.madness = min(self.madness, 4)
        if not enemies:
            return
        if self.madness < 3:
            print("Атакую того, кто стоит ближе -", enemies[0].name)
            self.attack(enemies[0])
        else:
            target = random.choice(enemies)
            print("BERSERK MODE!!! Уровень безумия - " + str(self.madness) + " Случайно атакую -", target.name)
            print()
            self.attack(target)
        print('\n')


class MonsterHunter(Monster):

    def __init__(self, name):
        super().__init__(name)
        self.potions = 10

    def attack(self, target):
        target.take_damage(self.get_power() + (10 - self.potions))

    def take_damage(self, power):
        self.set_hp(self.get_hp() - power)
        if random.randint(1, 10) == 1:
            self.potions -= 1
        super().take_damage(power)

    def give_a_potion(self, target):
        self.potions -= 1
        target.set_hp(target.get_hp() + self.get_power())

    def make_a_move(self, friends, enemies):
        print(self.name, end=' ')
        target_of_potion = friends[0]
        min_health = target_of_potion.get_hp()
        for friend in friends:
            if friend.get_hp() < min_health:
                target_of_potion = friend
                min_health = target_of_potion.get_hp()

        if min_health < 60 and self.potions > 0:
            print("Исцеляю", target_of_potion.name)
            self.give_a_potion(target_of_potion)
        else:
            if not enemies:
                return
            print("Атакую ближнего -", enemies[0].name)
            self.attack(enemies[0])
        print('\n')
