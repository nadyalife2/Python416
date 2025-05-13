class Family:
    def __init__(self):
        self.Family_name = "Common family"
        self.Family_funds = 100000
        self.Having_a_house = False

    def info(self):
        print("Имя:{}\nfamily_funds:{}\nhaving_a_house:{}".format(self.Family_name, self.Family_funds, self.Having_a_house))

    def earn_money(self, amount):
        self.Family_funds+=amount
        print("Earned {} money! Current value: {}".format(amount, self.Family_funds))


    def buy_house(self, house_price):
        if self.Family_funds>= house_price:
            self.Family_funds -= house_price
            self.Having_a_house = True
            print("House purchased! Current money:{}".format(self.Family_funds))

        else:
            print("Not enough money!")


my_family=Family()
my_family.info()


print("Try to buy a house")
my_family.buy_house(10**6)

if not my_family.Having_a_house:
    my_family.earn_money(900000)
    print("Try to buy a house again")
    my_family.buy_house(10**6)
my_family.info()