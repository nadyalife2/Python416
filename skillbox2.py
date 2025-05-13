# class Monitor:
#     name="Samsung"
#     matrix="VA"
#     res="WQHD"
#     freq="60"
#
# print(Monitor)
# monitor_1=Monitor()
# print("monitor_1",monitor_1.name, monitor_1.matrix,monitor_1.res, monitor_1.freq)
#
# monitor_2=Monitor()
# monitor_2.freq=144
# print("monitor_2",monitor_2.name, monitor_2.matrix,monitor_2.res, monitor_2.freq)
#
# monitor_3=Monitor()
# monitor_3.freq=70
# print("monitor_3",monitor_3.name, monitor_3.matrix,monitor_3.res, monitor_3.freq)

class Monitor:
    name = "Samsung"
    matrix = "VA"
    resolution = "WQHD"
    frequency = 0


class Headphones:
    name = "Sony"
    sensitivity = 108
    micro = True


monitors = [Monitor() for _ in range(4)]
headphones = [Headphones() for _ in range(3)]

for index, number in enumerate([60, 144, 70, 60]):
    monitors[index].frequency = number

headphones[0].micro = False