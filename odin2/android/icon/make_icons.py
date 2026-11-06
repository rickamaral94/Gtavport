#!/usr/bin/env python3
"""Gtavport: ícone do app (arte original "GV", sem marcas da Rockstar) -> res/ do APK.

    python3 -I odin2/android/icon/make_icons.py <saída res/>

Fundo claro em volta da moldura vira transparente; gera mipmap-*/ic_launcher.png (ícone antigo,
48..192 px) e o ícone adaptativo (Android 8+): fundo preto + frente com a arte em 74 dp de 108 dp.
"""
import os
import sys
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
out = sys.argv[1]
src = Image.open(os.path.join(HERE, "gtavport-icon-source.jpg")).convert("RGB")
w, h = src.size
KEY = (255, 0, 255)
for corner in [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1), (w // 2, 0), (w // 2, h - 1), (0, h // 2), (w - 1, h // 2)]:
    ImageDraw.floodfill(src, corner, KEY, thresh=90)
art = src.convert("RGBA")
px = art.load()
for y in range(h):
    for x in range(w):
        if px[x, y][:3] == KEY:
            px[x, y] = (0, 0, 0, 0)
bbox = art.getbbox()
art = art.crop(bbox)
side = max(art.size)
sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
sq.paste(art, ((side - art.size[0]) // 2, (side - art.size[1]) // 2))
art = sq

DENS = {"mdpi": 1.0, "hdpi": 1.5, "xhdpi": 2.0, "xxhdpi": 3.0, "xxxhdpi": 4.0}
for d, s in DENS.items():
    folder = os.path.join(out, "mipmap-" + d)
    os.makedirs(folder, exist_ok=True)
    n = round(48 * s)
    art.resize((n, n), Image.LANCZOS).save(os.path.join(folder, "ic_launcher.png"))
    layer = round(108 * s)
    fg = Image.new("RGBA", (layer, layer), (0, 0, 0, 0))
    a = round(74 * s)
    fg.paste(art.resize((a, a), Image.LANCZOS), ((layer - a) // 2, (layer - a) // 2))
    fg.save(os.path.join(folder, "ic_launcher_foreground.png"))
v26 = os.path.join(out, "mipmap-anydpi-v26")
os.makedirs(v26, exist_ok=True)
with open(os.path.join(v26, "ic_launcher.xml"), "w") as f:
    f.write('<?xml version="1.0" encoding="utf-8"?>\n'
            '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
            '    <background android:drawable="@color/ic_launcher_background" />\n'
            '    <foreground android:drawable="@mipmap/ic_launcher_foreground" />\n'
            '</adaptive-icon>\n')
values = os.path.join(out, "values")
os.makedirs(values, exist_ok=True)
with open(os.path.join(values, "ic_launcher_background.xml"), "w") as f:
    f.write('<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
            '    <color name="ic_launcher_background">#000000</color>\n</resources>\n')
print("ícones em", out, "arte", art.size)
