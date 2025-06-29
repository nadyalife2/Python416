import sqlite3
import os
from flask import Flask, render_template, flash, request, g, abort

from two.fdatabase import FDataBase


# конфигурация
DATABASE = '/tmp/flsk.db'
DEBUG = True
SECRET_KEY = 'ba92ab4e8b8755797efef56a86d6698e91db0ffc'

app = Flask(__name__)
app.config.from_object(__name__)
app.config.update(dict(DATABASE=os.path.join(app.root_path, "flsk.db")))


def connect_db():
    con = sqlite3.connect(app.config['DATABASE'])
    con.row_factory = sqlite3.Row
    return con


def create_db():  # сейчас в коде не участвует
    db = connect_db()
    with app.open_resource("sq_db.sql", "r") as f:
        db.cursor().executescript(f.read())
    db.commit()
    db.close()


def get_db():
    if not hasattr(g, 'link_db'):
        g.link_db = connect_db()
    return g.link_db

# menu = [
#     {"name": "Главная", "url": "index"},
#     {"name": "Новости", "url": "news"},
#     {"name": "Блог", "url": "blog"},
#     {"name": "О нас", "url": "about"},
#     {"name": "Контакты", "url": "contacts"}
# ]


@app.route("/")
def index():
    db = get_db()
    dbase = FDataBase(db)
    return render_template('index.html', menu=dbase.get_menu())



@app.route("/news")
def news():
    db = get_db()
    dbase = FDataBase(db)
    return render_template('news.html', menu=dbase.get_menu())



@app.route("/about")
def about():
    db = get_db()
    dbase = FDataBase(db)
    return render_template("about.html", menu=dbase.get_menu())


@app.route("/blog")
def blog():
    db = get_db()
    dbase = FDataBase(db)
    return render_template("blog.html", menu=dbase.get_menu())


@app.route("/contacts")
def contacts():
    db = get_db()
    dbase = FDataBase(db)
    return render_template("contacts.html", menu=dbase.get_menu())



if __name__ == "__main__":
    app.run(debug=True)
