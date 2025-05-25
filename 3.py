import csv

with open("data2.csv", "r") as f:
    reader = csv.reader(f, delimiter=';')
    for row in reader:
        print(row)

