import csv
import requests
from bs4 import BeautifulSoup


# def get_data(html):
#     soup = BeautifulSoup(html, "lxml")
#     p1 = soup.find_all("div", class_="q-item js-rm-central-column-item js-load-pub")
#     for p in p1:
#         name=p.find("span", class_="q-item__title js-rm-central-column-item-text").text
#         return p
#
#
# def main():
#     url = "https://www.rbc.ru/quote?utm_source=topline"
#     print(get_data(get_html(url)))
#
#
# if __name__ == '__main__':
#     main()




def get_html(url):
    row = requests.get(url)
    return row.text


def get_data(html):
    soup = BeautifulSoup(html, "lxml")
    elements = soup.find_all("div", class_="article-card")
    print(f"Всего найдено блоков: {len(elements)}")
    for el in elements:
        url=el.find("div",class_="info-block-right").find("a", class_="info-block-title")["href"]
        name = el.find("div", class_="info-block-right").find("a", class_="info-block-title").text
        snippet = el.find("div", class_="info-block-right").find("div", class_="meta-info-row-date").text
        data={
            'url': url,
            'name': name,
            'snippet':snippet
        }
        write_csv(data)

def write_csv(data):
    with open("parserRBK.csv", "a") as f:
        writer= csv.writer(f, delimiter=";", lineterminator="\r")
        writer.writerow((data["url"], data["name"], data["snippet"]))


def main():
    url = "https://www.rbc.ru/industries/tag/national_projects"
    get_data(get_html(url))


if __name__ == '__main__':
    main()