from flask import Flask, render_template, request

app = Flask(__name__)

menu = [
    {"name": "Главная", "url": "index"},
    {"name": "Новости", "url": "news"},
    {"name": "Блог", "url": "blog"},
    {"name": "О нас", "url": "about"},
    {"name": "Контакты", "url": "contacts"}
]

@app.route("/")
@app.route("/index")
def index():
    return render_template("index.html", title="Главная", menu=menu)

@app.route("/about")
def about():
    return render_template("about.html", title="О нас", menu=menu)

@app.route("/news")
def news():
    return render_template("news.html", title="Новости", menu=menu)

@app.route("/blog")
def blog():
    return render_template("blog.html", title="Блог", menu=menu)

@app.route("/contacts", methods=["GET", "POST"])
def contacts():
    if request.method == "POST":
        print(request.form)
    return render_template("contacts.html", title="Контакты", menu=menu)

if __name__ == "__main__":
    app.run(debug=True)
