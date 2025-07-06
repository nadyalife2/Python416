import sqlite3
import time
import math



class FDataBase:
    def __init__(self, db):
        self.__db = db
        self.__cur = db.cursor()

    def get_menu(self):
        sql = "SELECT * FROM mainmenu"
        try:
            self.__cur.execute(sql)
            res = self.__cur.fetchall()
            if res:
                return res
        except IOError:
            print("Ошибка чтения из БД")
        return []

    def add_post(self, title, text):
        try:
            tm = math.floor(time.time())

        except sqlite3.Error as e:
            print("Ошибка добавления статьи в БД "+ str(e))
            return False
        return True
