"""
Generates the art and Tiled maps for the 2D project templates (2D Platformer, 2D Top-Down):

  projects/Starter2D/Art/        tilesets, characters, pickups, enemy, background (PNG)
  projects/Starter2D/Maps/       Platformer.tmx, TopDown.tmx and their .tsx tilesets

    python3 tools/pixelart/make_2d_starter.py [output_dir]

Everything is drawn here from code (original art, same license as the engine), seeded so
re-running gives identical files. The maps open in Tiled (https://www.mapeditor.org) for editing.
Requires Pillow.
"""
import os
import random
import sys
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "projects", "Starter2D")
ART = os.path.join(OUT, "Art")
MAPS = os.path.join(OUT, "Maps")
os.makedirs(ART, exist_ok=True)
os.makedirs(MAPS, exist_ok=True)
T = 16  # tile size

# ---------------------------------------------------------------- palette
def hexc(h):
    h = h.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), 255)

P = {k: hexc(v) for k, v in {
    "ink": "1a1c2c", "plum": "5d275d", "red": "b13e53", "orange": "ef7d57", "yellow": "ffcd75",
    "lime": "a7f070", "green": "38b764", "teal": "257179", "navy": "29366f", "blue": "3b5dc9",
    "sky": "41a6f6", "cyan": "73eff7", "white": "f4f4f4", "grey": "94b0c2", "slate": "566c86", "dark": "333c57",
    "dirt": "b86f50", "dirtdark": "733e39", "dirtlight": "e4a672", "skin": "f2c3a0", "skinshade": "d68f6e",
    "wood": "a0522d", "woodlight": "c8763f", "leaf": "2e8b57", "leafdark": "1e5e45", "leaflight": "6cc26a",
    "water": "3e7fd6", "waterlight": "7ec4f2", "waterdark": "2c5aa8", "foam": "d8f4ff",
    "grassA": "5fb85a", "grassB": "4fa24e", "grassC": "79c86a", "path": "d8b07a", "pathdark": "b88a5a", "pathlight": "ead0a0",
}.items()}
CLEAR = (0, 0, 0, 0)


def new(w, h, fill=CLEAR):
    return Image.new("RGBA", (w, h), fill)


def px(img, x, y, c):
    if 0 <= x < img.width and 0 <= y < img.height:
        img.putpixel((x, y), c)


def rect(img, x0, y0, x1, y1, c):  # inclusive
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px(img, x, y, c)


def from_ascii(rows, keys):
    h, w = len(rows), max(len(r) for r in rows)
    img = new(w, h)
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if ch in keys:
                img.putpixel((x, y), keys[ch])
    return img


def save(img, name, folder=ART):
    img.save(os.path.join(folder, name))
    print("wrote", os.path.relpath(os.path.join(folder, name), OUT), img.size)


# ================================================================ platformer tileset
# 8 columns x 4 rows of 16px tiles.
#   row 0: ground top-left, top, top-right, | platform left, middle, right, | crate, brick
#   row 1: ground left, middle, right,      | pillar top, pillar,           | flower, grass tuft, mushroom
#   row 2: ground bottom-left, bottom, bottom-right, | single block, sign, bush left, bush right, rock
#   row 3: coin-block, ...
def dirt_fill(img, ox, oy, rnd):
    rect(img, ox, oy, ox + 15, oy + 15, P["dirt"])
    for _ in range(10):
        x, y = rnd.randrange(16), rnd.randrange(16)
        px(img, ox + x, oy + y, P["dirtdark"])
        if rnd.random() < 0.5:
            px(img, ox + x + 1, oy + y, P["dirtdark"])
    for _ in range(5):
        x, y = rnd.randrange(15), rnd.randrange(15)
        px(img, ox + x, oy + y, P["dirtlight"])
    # a couple of pebbles
    for _ in range(2):
        x, y = rnd.randrange(2, 13), rnd.randrange(2, 13)
        rect(img, ox + x, oy + y, ox + x + 1, oy + y, P["slate"])
        px(img, ox + x, oy + y - 1, P["grey"])


def ground_tile(col, row, rnd):
    """9-slice ground: col 0..2 = left/middle/right, row 0..2 = top/middle/bottom."""
    img = new(T, T)
    dirt_fill(img, 0, 0, rnd)
    if col == 0:
        rect(img, 0, 0, 0, 15, P["ink"]); rect(img, 1, 0, 1, 15, P["dirtdark"])
    if col == 2:
        rect(img, 15, 0, 15, 15, P["ink"]); rect(img, 14, 0, 14, 15, P["dirtdark"])
    if row == 2:
        rect(img, 0, 15, 15, 15, P["ink"]); rect(img, 0, 14, 15, 14, P["dirtdark"])
        if col == 0: px(img, 0, 15, CLEAR); px(img, 1, 14, P["ink"])
        if col == 2: px(img, 15, 15, CLEAR); px(img, 14, 14, P["ink"])
    if row == 0:
        # grass cap with a ragged lower edge
        for x in range(16):
            depth = 4 + (1 if (x * 7 + col * 3) % 5 < 2 else 0) + (1 if (x * 3) % 7 == 0 else 0)
            for y in range(depth):
                px(img, x, y, P["green"])
            px(img, x, depth, P["teal"])
            px(img, x, 1, P["lime"])
            if (x + col) % 3 == 0:
                px(img, x, 2, P["lime"])
        rect(img, 0, 0, 15, 0, P["ink"])
        if col == 0:
            rect(img, 0, 0, 0, 6, P["ink"]); px(img, 0, 0, CLEAR); px(img, 1, 1, P["ink"]); px(img, 1, 0, CLEAR)
        if col == 2:
            rect(img, 15, 0, 15, 6, P["ink"]); px(img, 15, 0, CLEAR); px(img, 14, 1, P["ink"]); px(img, 14, 0, CLEAR)
    return img


def platform_tile(part):
    img = new(T, T)
    rect(img, 0, 0, 15, 6, P["wood"])
    rect(img, 0, 0, 15, 0, P["ink"]); rect(img, 0, 6, 15, 6, P["ink"])
    rect(img, 0, 1, 15, 1, P["woodlight"])
    for x in (3, 11):
        px(img, x, 3, P["dirtdark"]); px(img, x + 1, 4, P["dirtdark"])
    if part == 0:
        rect(img, 0, 0, 0, 6, P["ink"]); px(img, 0, 0, CLEAR); px(img, 0, 6, CLEAR)
    if part == 2:
        rect(img, 15, 0, 15, 6, P["ink"]); px(img, 15, 0, CLEAR); px(img, 15, 6, CLEAR)
    if part == 1:
        rect(img, 7, 1, 7, 5, P["dirtdark"])
    # brackets under the plank
    if part in (0, 2):
        bx = 3 if part == 0 else 11
        rect(img, bx, 7, bx + 1, 9, P["dirtdark"]); rect(img, bx, 10, bx + 1, 10, P["ink"])
    return img


def crate_tile():
    img = new(T, T)
    rect(img, 0, 0, 15, 15, P["ink"])
    rect(img, 1, 1, 14, 14, P["woodlight"])
    rect(img, 2, 2, 13, 13, P["wood"])
    for i in range(12):
        px(img, 2 + i, 2 + i, P["woodlight"]); px(img, 3 + i if i < 11 else 13, 2 + i, P["dirtdark"])
    rect(img, 1, 7, 14, 8, P["woodlight"]); rect(img, 1, 9, 14, 9, P["dirtdark"])
    return img


def brick_tile():
    img = new(T, T)
    rect(img, 0, 0, 15, 15, P["dark"])
    for row in range(4):
        y = row * 4
        off = 0 if row % 2 == 0 else 4
        for bx in range(-1, 3):
            x = bx * 8 + off
            rect(img, x + 1, y + 1, x + 7, y + 3, P["slate"])
            rect(img, x + 1, y + 1, x + 6, y + 1, P["grey"])
    return img


def flower_tile(color):
    img = new(T, T)
    rect(img, 7, 9, 7, 15, P["teal"]); px(img, 6, 12, P["green"]); px(img, 8, 11, P["green"])
    for dx, dy in ((0, -1), (-1, 0), (1, 0), (0, 1)):
        px(img, 7 + dx, 7 + dy, P[color])
    px(img, 7, 7, P["yellow"])
    return img


def tuft_tile():
    img = new(T, T)
    for x, h in ((3, 3), (5, 5), (7, 4), (9, 6), (11, 3), (12, 4)):
        rect(img, x, 15 - h, x, 15, P["green"]); px(img, x, 15 - h, P["lime"])
    return img


def mushroom_tile():
    img = from_ascii([
        "................", "................", "................", "................",
        "................", "................", "................", "......oooo......",
        ".....orrwrro....", "....orrrrrwro...", "....owrrrrrro...", ".....oooooooo...",
        "......owwwo.....", "......owwwo.....", "......owwwo.....", "......ooooo.....",
    ], {"o": P["ink"], "r": P["red"], "w": P["white"]})
    return img


def bush_tile(side):
    img = new(T, T)
    import math
    for y in range(16):
        for x in range(16):
            cx = 16 if side == 0 else -1
            d = math.hypot((x - cx) * 0.9, (y - 15) * 1.15)
            if d < 14.5:
                c = P["leaf"]
                if d > 13.2: c = P["ink"]
                elif (x + y * 3) % 7 == 0 or d < 6: c = P["leaflight"] if y < 9 else P["leaf"]
                elif y > 12: c = P["leafdark"]
                px(img, x, y, c)
    return img


def rock_tile():
    return from_ascii([
        "................", "................", "................", "................",
        "................", "................", "................", "................",
        "......oooo......", "....oogggwoo....", "...oggggggwgo...", "..ogsggggggggo..",
        "..ossgggggggso..", "..osssggggssso..", "...ossssssssoo..", "....oooooooo....",
    ], {"o": P["ink"], "g": P["grey"], "s": P["slate"], "w": P["white"]})


def sign_tile():
    return from_ascii([
        "................", "................", "................", "..oooooooooooo..",
        "..oLLLLLLLLLLo..", "..oLdddLLdddLo..", "..oLLLLLLLLLLo..", "..oLddLLddddLo..",
        "..oWWWWWWWWWWo..", "..oooooooooooo..", "......oLWo......", "......oLWo......",
        "......oLWo......", "......oLWo......", ".....ooLWoo.....", "................",
    ], {"o": P["ink"], "L": P["woodlight"], "W": P["wood"], "d": P["dirtdark"]})


def block_single(rnd):
    img = ground_tile(0, 0, rnd)
    right = ground_tile(2, 0, rnd)
    for y in range(16):
        for x in range(8, 16):
            img.putpixel((x, y), right.getpixel((x, y)))
    rect(img, 0, 15, 15, 15, P["ink"]); rect(img, 1, 14, 14, 14, P["dirtdark"])
    px(img, 0, 15, CLEAR); px(img, 15, 15, CLEAR)
    return img


def make_platformer_tiles():
    rnd = random.Random(3)
    sheet = new(8 * T, 4 * T)
    put = lambda img, c, r: sheet.alpha_composite(img, (c * T, r * T))
    for r in range(3):
        for c in range(3):
            put(ground_tile(c, r, rnd), c, r)
    for i in range(3):
        put(platform_tile(i), 3 + i, 0)
    put(crate_tile(), 6, 0)
    put(brick_tile(), 7, 0)
    put(flower_tile("red"), 3, 1)
    put(flower_tile("yellow"), 4, 1)
    put(tuft_tile(), 5, 1)
    put(mushroom_tile(), 6, 1)
    put(rock_tile(), 7, 1)
    put(block_single(rnd), 3, 2)
    put(sign_tile(), 4, 2)
    put(bush_tile(0), 5, 2)
    put(bush_tile(1), 6, 2)
    put(flower_tile("cyan"), 7, 2)
    save(sheet, "platformer_tiles.png")
    return sheet


# ================================================================ background (repeating)
def make_background():
    import math
    W, H = 256, 320
    img = new(W, H)
    bands = [P["cyan"], (150, 230, 246, 255), (176, 236, 248, 255)]
    for y in range(H):
        c = P["sky"] if y < 70 else (bands[0] if y < 150 else bands[1] if y < 210 else bands[2])
        for x in range(W):
            img.putpixel((x, y), c)
    # dithered band edges
    for y0, a, b in ((70, P["sky"], bands[0]), (150, bands[0], bands[1]), (210, bands[1], bands[2])):
        for x in range(W):
            if x % 2 == 0: img.putpixel((x, y0), a)
            if x % 4 == 1: img.putpixel((x, y0 + 1), a)
    # clouds
    rnd = random.Random(9)
    for cx, cy, r in ((40, 60, 10), (150, 40, 13), (215, 90, 9)):
        for k in range(5):
            ox, oy, rr = cx + (k - 2) * r * 0.8, cy + (abs(k - 2) * 2), r - abs(k - 2) * 2
            for y in range(int(oy - rr), int(oy + rr) + 1):
                for x in range(int(ox - rr), int(ox + rr) + 1):
                    if (x - ox) ** 2 + (y - oy) ** 2 <= rr * rr and 0 <= y < H:
                        img.putpixel((x % W, y), P["white"] if y < oy + rr * 0.4 else P["grey"])
    # far hills, near hills (seamless: periodic in x)
    for base, amp, freq, col, top in ((215, 30, 2, (95, 180, 150, 255), (130, 205, 160, 255)), (250, 22, 3, P["green"], P["lime"])):
        for x in range(W):
            hgt = base - amp * (0.6 * math.sin(2 * math.pi * freq * x / W) + 0.4 * math.sin(2 * math.pi * (freq * 2 + 1) * x / W + 1.3))
            for y in range(int(hgt), H):
                img.putpixel((x, y), col)
            img.putpixel((x, int(hgt)), top)
    save(img, "background.png")


# ================================================================ characters
def outline(img, color=None):
    """Adds a 1px dark outline around opaque pixels."""
    color = color or P["ink"]
    out = img.copy()
    for y in range(img.height):
        for x in range(img.width):
            if img.getpixel((x, y))[3] == 0:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < img.width and 0 <= ny < img.height and img.getpixel((nx, ny))[3] > 0 and img.getpixel((nx, ny)) != color:
                        out.putpixel((x, y), color)
                        break
    return out


HERO_KEYS = {"h": P["red"], "H": P["orange"], "s": P["skin"], "S": P["skinshade"], "e": P["ink"], "w": P["white"],
             "t": P["blue"], "T": P["sky"], "b": P["dirtdark"], "p": P["dark"], "y": P["yellow"], "o": P["ink"]}

# Side view, facing right. Head + body (legs are drawn per frame).
HERO_SIDE_TOP = [
    "................",
    "................",
    "......hhhhh.....",
    ".....hhhhhHH....",
    ".....hhhhhhhhh..",
    ".....hSsssss....",
    ".....Ssssesw....",
    ".....Sssssss....",
    "......ssSss.....",
    ".....tTTTtt.....",
    "....stTTTtts....",
    "....stttytts....",
]


def hero_side(legs, body_dy=0, arms=None, jump=False):
    img = new(16, 16)
    top = from_ascii(HERO_SIDE_TOP, HERO_KEYS)
    img.alpha_composite(top, (0, body_dy))
    if arms == "up":
        for x, y in ((4, 9), (4, 8), (11, 9), (11, 8)):
            px(img, x, y + body_dy, P["skin"])
        px(img, 4, 10 + body_dy, P["blue"]); px(img, 11, 10 + body_dy, P["blue"]); px(img, 4, 11 + body_dy, P["blue"]); px(img, 11, 11 + body_dy, P["blue"])
    # legs: list of (x, length, bend) for back and front leg; feet point right
    y0 = 12 + body_dy
    for i, (x, length) in enumerate(legs):
        col = P["dark"] if i == 0 else P["slate"]
        for k in range(length):
            px(img, x, y0 + k, col)
        px(img, x, y0 + length, P["dirtdark"]); px(img, x + 1, y0 + length, P["dirtdark"])
    return outline(img)


def make_hero_side():
    # rows: idle (2), run (6), jump (1) + fall (1)
    sheet = new(6 * 16, 3 * 16)
    idle = [hero_side([(6, 2), (9, 2)]), hero_side([(6, 2), (9, 2)], body_dy=1)]
    for i, f in enumerate(idle):
        sheet.alpha_composite(f, (i * 16, 0))
    run_legs = [[(5, 2), (10, 1)], [(6, 1), (9, 2)], [(7, 2), (8, 2)], [(10, 1), (5, 2)], [(9, 2), (6, 1)], [(8, 2), (7, 2)]]
    for i, legs in enumerate(run_legs):
        sheet.alpha_composite(hero_side(legs, body_dy=1 if i in (2, 5) else 0), (i * 16, 16))
    sheet.alpha_composite(hero_side([(5, 1), (10, 2)], body_dy=-1, arms="up"), (0, 32))
    sheet.alpha_composite(hero_side([(6, 2), (10, 2)], body_dy=0, arms="up"), (16, 32))
    save(sheet, "hero_platformer.png")


# Top-down hero: rows down, up, right (left = right mirrored); 4 walk frames each.
TD_DOWN = [
    "................",
    "......hhhh......",
    ".....hhhhhh.....",
    "....hHhhhhHh....",
    "....hssssssh....",
    "....sesssses....",
    "....ssssssss....",
    ".....SssssS.....",
    "....tTTyyTTt....",
    "...stTTTTTTts...",
    "...stttttttts...",
    "....tttttttt....",
]
TD_UP = [
    "................",
    "......hhhh......",
    ".....hhhhhh.....",
    "....hhhhhhhh....",
    "....hhhhhhhh....",
    "....hhHhhHhh....",
    "....hhhhhhhh....",
    ".....hhhhhh.....",
    "....tttttttt....",
    "...sttttttttS...",
    "...sttttttttS...",
    "....tttttttt....",
]
TD_RIGHT = [
    "................",
    "......hhhhh.....",
    ".....hhhhhHH....",
    ".....hhhhhhhh...",
    ".....hSssssss...",
    ".....Sssssess...",
    ".....Ssssssss...",
    "......SsssS.....",
    ".....tTTTTt.....",
    ".....tTTTTts....",
    ".....tttttts....",
    ".....tttttt.....",
]


def hero_td(top, legs, dy=0):
    img = new(16, 16)
    img.alpha_composite(from_ascii(top, HERO_KEYS), (0, dy))
    for x, length in legs:
        for k in range(length):
            px(img, x, 12 + dy + k, P["dark"])
        px(img, x, 12 + dy + length, P["dirtdark"])
        px(img, x + 1, 12 + dy + length, P["dirtdark"])
        px(img, x + 1, 12 + dy + length - 1, P["dark"]) if length > 1 else None
    return outline(img)


def make_hero_topdown():
    sheet = new(4 * 16, 3 * 16)
    front = [[(5, 2), (9, 2)], [(5, 1), (9, 2)], [(5, 2), (9, 2)], [(5, 2), (9, 1)]]
    side = [[(7, 2), (8, 2)], [(5, 2), (10, 1)], [(7, 2), (8, 2)], [(10, 2), (5, 1)]]
    for i in range(4):
        dy = 0 if i % 2 == 0 else -1
        sheet.alpha_composite(hero_td(TD_DOWN, front[i], dy), (i * 16, 0))
        sheet.alpha_composite(hero_td(TD_UP, front[i], dy), (i * 16, 16))
        sheet.alpha_composite(hero_td(TD_RIGHT, side[i], dy), (i * 16, 32))
    save(sheet, "hero_topdown.png")


# ================================================================ pickups and enemy
def make_coin():
    sheet = new(4 * 16, 16)
    widths = [5, 3, 1, 3]
    for i, w in enumerate(widths):
        img = new(16, 16)
        for y in range(4, 13):
            dy = abs(y - 8)
            half = max(0, w - (1 if dy == 4 else 0))
            for x in range(8 - half, 8 + half + 1):
                c = P["yellow"]
                if x == 8 - half: c = P["white"] if w > 1 else P["yellow"]
                elif x == 8 + half: c = P["orange"]
                px(img, x, y, c)
        if w >= 3:
            rect(img, 8, 6, 8, 10, P["orange"])
        sheet.alpha_composite(outline(img), (i * 16, 0))
    save(sheet, "coin.png")


def make_gem():
    sheet = new(4 * 16, 16)
    base = from_ascii([
        "................", "................", "................", "................",
        ".....ccccc......", "....cwcccbc.....", "...cwccccbbc....", "...ccccccbbb....",
        "....cccccbb.....", ".....cccbb......", "......cbb.......", ".......b........",
    ], {"c": P["cyan"], "w": P["white"], "b": P["sky"]})
    for i in range(4):
        img = new(16, 16)
        img.alpha_composite(base, (0, 1 if i in (1, 2) else 0))
        if i == 2:
            px(img, 11, 3, P["white"]); px(img, 12, 4, P["white"]); px(img, 10, 4, P["white"]); px(img, 11, 5, P["white"])
        sheet.alpha_composite(outline(img), (i * 16, 0))
    save(sheet, "gem.png")


def make_slime():
    import math
    sheet = new(2 * 16, 16)
    for i, (w, h) in enumerate(((6.0, 9.0), (7.0, 7.5))):
        img = new(16, 16)
        for y in range(16):
            for x in range(16):
                dx, dy = (x - 7.5) / w, (y - 15.5) / h
                if dx * dx + dy * dy <= 1.0 and y <= 15:
                    c = P["green"]
                    if dy < -0.55 and dx < 0.1: c = P["lime"]
                    if y >= 14: c = P["teal"]
                    px(img, x, y, c)
        top = int(15.5 - h)
        for ex in (5, 9):
            px(img, ex, top + 3, P["ink"]); px(img, ex, top + 4, P["ink"])
        px(img, 4, top + 2, P["white"])
        sheet.alpha_composite(outline(img), (i * 16, 0))
    save(sheet, "slime.png")


# ================================================================ top-down tileset
def grass(img, ox, oy, rnd, variant):
    base = P["grassA"]
    rect(img, ox, oy, ox + 15, oy + 15, base)
    for _ in range(14):
        x, y = rnd.randrange(16), rnd.randrange(1, 16)
        px(img, ox + x, oy + y, P["grassC"])
        px(img, ox + x, oy + y - 1, P["grassC"] if rnd.random() < 0.4 else base)
    for _ in range(4 + variant * 3):
        x, y = rnd.randrange(16), rnd.randrange(16)
        px(img, ox + x, oy + y, P["grassB"])


def hash01(x, y):
    """Deterministic per-pixel noise in [0, 1)."""
    h = (x * 374761393 + y * 668265263) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFF) / 65536.0


def blob9(fill, edge_fn, size=3):
    """Draws a (size*16)^2 rounded rectangle 'blob' and returns it; cut into a 3x3 9-slice."""
    S = size * T
    img = new(S, S)
    r = 6
    for y in range(S):
        for x in range(S):
            cx = min(max(x, r), S - 1 - r)
            cy = min(max(y, r), S - 1 - r)
            d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            if d <= r:
                img.putpixel((x, y), edge_fn(x, y, r - d, S))
    return img


def make_topdown_tiles():
    """8 columns x 6 rows:
       row 0: grass x3, flowers x2, tall grass, stones, stump
       rows 1-3, cols 0-2: path 9-slice    cols 3-5: water 9-slice (shore)   col 6-7: water frames (animated)
       row 4: fence left, fence mid, fence right, fence post, ...
    """
    rnd = random.Random(5)
    sheet = new(8 * T, 6 * T)
    for i in range(3):
        grass(sheet, i * T, 0, rnd, i)
    # flowers on grass
    for i, col in enumerate(("red", "yellow")):
        grass(sheet, (3 + i) * T, 0, rnd, 0)
        for fx, fy in ((3, 4), (10, 3), (6, 10), (12, 12)):
            ox, oy = (3 + i) * T + fx, fy
            for dx, dy in ((0, -1), (-1, 0), (1, 0), (0, 1)):
                px(sheet, ox + dx, oy + dy, P[col] if col != "yellow" else P["white"])
            px(sheet, ox, oy, P["yellow"])
    # tall grass
    grass(sheet, 5 * T, 0, rnd, 1)
    for x in range(1, 15, 2):
        h = 5 + (x * 7) % 5
        rect(sheet, 5 * T + x, 15 - h, 5 * T + x, 14, P["green"])
        px(sheet, 5 * T + x, 15 - h, P["lime"])
    # stones
    grass(sheet, 6 * T, 0, rnd, 0)
    for sx, sy, w in ((3, 9, 3), (9, 4, 2), (10, 11, 2)):
        rect(sheet, 6 * T + sx, sy, 6 * T + sx + w, sy + 1, P["grey"])
        rect(sheet, 6 * T + sx, sy + 2, 6 * T + sx + w, sy + 2, P["slate"])
    # stump
    grass(sheet, 7 * T, 0, rnd, 1)
    stump = from_ascii([
        "................", "................", "................", "................",
        "................", "....oooooooo....", "...oyyyyyyyyo...", "...oydyyyydyo...",
        "...oyyyyyyyyo...", "...owwyyyywwo...", "...oWwwwwwwWo...", "...oWwWwwWwWo...",
        "..ooWwWwwWwWoo..", "..o.oooooooo.o..", "................", "................",
    ], {"o": P["ink"], "y": P["dirtlight"], "d": P["dirt"], "w": P["wood"], "W": P["dirtdark"]})
    sheet.alpha_composite(stump, (7 * T, 0))

    # path 9-slice: path blob over grass
    def path_px(x, y, inner, S):
        if inner < 1.2: return P["pathdark"]
        n = hash01(x, y)
        return P["pathlight"] if n < 0.06 else P["pathdark"] if n > 0.93 else P["path"]
    under = new(3 * T, 3 * T)
    for r in range(3):
        for c in range(3):
            grass(under, c * T, r * T, rnd, (r + c) % 2)
    p = under.copy()
    p.alpha_composite(blob9(None, path_px))
    sheet.alpha_composite(p, (0, T))

    # water 9-slice with a sandy shore
    def water_px(x, y, inner, S):
        if inner < 1.0: return P["pathdark"]
        if inner < 2.6: return P["pathlight"]
        if inner < 3.6: return P["foam"]
        n = hash01(x // 3, y)
        return P["waterlight"] if n < 0.05 else P["water"]
    w = under.copy()
    w.alpha_composite(blob9(None, water_px))
    sheet.alpha_composite(w, (3 * T, T))
    # water center animation frames (cols 6-7, rows 1-2): 4 frames of ripples
    for k in range(4):
        tile = new(T, T, P["water"])
        for (x, y) in ((2, 3), (9, 6), (5, 11), (12, 13)):
            xx = (x + k * 2) % 16
            rect(tile, xx, y, min(15, xx + 2), y, P["waterlight"])
        if k % 2 == 0:
            px(tile, (7 + k * 3) % 16, 1, P["foam"])
        sheet.alpha_composite(tile, ((6 + k % 2) * T, (1 + k // 2) * T))

    # fence (row 4)
    for i in range(4):
        tile = new(T, T)
        if i in (0, 1, 2):
            rect(tile, 0 if i else 7, 5, 15 if i < 2 else 8, 6, P["woodlight"]); rect(tile, 0 if i else 7, 7, 15 if i < 2 else 8, 7, P["wood"])
            rect(tile, 0 if i else 7, 10, 15 if i < 2 else 8, 11, P["woodlight"]); rect(tile, 0 if i else 7, 12, 15 if i < 2 else 8, 12, P["wood"])
        rect(tile, 6, 2, 9, 14, P["woodlight"]); rect(tile, 8, 3, 9, 14, P["wood"]); rect(tile, 6, 2, 9, 2, P["pathlight"])
        sheet.alpha_composite(outline(tile), (i * T, 4 * T))
    save(sheet, "topdown_tiles.png")
    return sheet


def tree_image():
    import math
    img = new(32, 40)
    # trunk
    rect(img, 13, 26, 18, 37, P["wood"]); rect(img, 16, 26, 18, 37, P["dirtdark"]); rect(img, 12, 36, 19, 37, P["wood"])
    # canopy: three overlapping circles
    for cx, cy, r in ((16, 13, 11), (9, 19, 7), (23, 19, 7), (16, 21, 8)):
        for y in range(40):
            for x in range(32):
                d = math.hypot(x - cx, y - cy)
                if d <= r:
                    c = P["leaf"]
                    if (x - cx) + (y - cy) < -r * 0.5: c = P["leaflight"]
                    elif (x - cx) + (y - cy) > r * 0.6: c = P["leafdark"]
                    img.putpixel((x, y), c)
    for (x, y) in ((11, 8), (18, 10), (14, 15), (21, 14), (8, 17)):
        px(img, x, y, P["lime"])
    return outline(img)


def house_image():
    img = new(48, 48)
    # walls
    rect(img, 4, 22, 43, 45, P["pathlight"]); rect(img, 4, 40, 43, 45, P["path"])
    for y in range(24, 44, 4):
        rect(img, 4, y, 43, y, P["path"])
    # roof
    for y in range(4, 24):
        inset = max(0, 20 - y) // 1
        x0, x1 = 2 + (24 - y) // 3, 45 - (24 - y) // 3
        rect(img, x0, y, x1, y, P["red"] if (y // 3) % 2 else P["orange"])
    rect(img, 2, 22, 45, 23, P["plum"])
    # door and window
    rect(img, 20, 32, 27, 45, P["wood"]); rect(img, 21, 33, 26, 45, P["woodlight"]); px(img, 25, 39, P["yellow"])
    rect(img, 8, 28, 15, 34, P["ink"]); rect(img, 9, 29, 14, 33, P["cyan"]); rect(img, 11, 29, 11, 33, P["ink"]); rect(img, 9, 31, 14, 31, P["ink"])
    rect(img, 32, 28, 39, 34, P["ink"]); rect(img, 33, 29, 38, 33, P["cyan"]); rect(img, 35, 29, 35, 33, P["ink"]); rect(img, 33, 31, 38, 31, P["ink"])
    # chimney
    rect(img, 34, 4, 38, 12, P["slate"]); rect(img, 33, 3, 39, 4, P["dark"])
    return outline(img)


def bush_image():
    import math
    img = new(20, 14)
    for cx, cy, r in ((6, 8, 5.5), (13, 8, 5.5), (10, 5, 5)):
        for y in range(14):
            for x in range(20):
                d = math.hypot(x - cx, y - cy)
                if d <= r:
                    c = P["leaflight"] if (y < cy - 1 and x < cx) else P["leaf"]
                    if y > 10: c = P["leafdark"]
                    img.putpixel((x, y), c)
    px(img, 7, 4, P["red"]); px(img, 13, 6, P["red"]); px(img, 10, 9, P["red"])
    return outline(img)


def rock_image():
    return outline(from_ascii([
        "................", "....ggggggg.....", "...gwggggggs....", "..gggggggggss...",
        ".ggggggggggsss..", ".sggggggggssss..", ".ssggggggsssss..", "..sssssssssss...",
    ], {"g": P["grey"], "s": P["slate"], "w": P["white"]}))


def make_objects():
    imgs = {"tree.png": tree_image(), "house.png": house_image(), "bush.png": bush_image(), "rock.png": rock_image()}
    os.makedirs(os.path.join(ART, "Objects"), exist_ok=True)
    for name, img in imgs.items():
        save(img, name, os.path.join(ART, "Objects"))
    return imgs


# ================================================================ Tiled files
def tsx_atlas(name, image, cols, rows, solid=(), half=(), anims=None, image_rel="../Art/"):
    lines = [f'<?xml version="1.0" encoding="UTF-8"?>',
             f'<tileset version="1.10" tiledversion="1.11.2" name="{name}" tilewidth="16" tileheight="16" tilecount="{cols * rows}" columns="{cols}">',
             f' <image source="{image_rel}{image}" width="{cols * 16}" height="{rows * 16}"/>']
    tiles = sorted(set(solid) | set(half) | set((anims or {}).keys()))
    for t in tiles:
        lines.append(f' <tile id="{t}">')
        if t in solid or t in half:
            h = 16 if t in solid else 7
            lines.append('  <objectgroup draworder="index">')
            lines.append(f'   <object id="1" x="0" y="0" width="16" height="{h}"/>')
            lines.append('  </objectgroup>')
        if anims and t in anims:
            lines.append('  <animation>')
            for f, d in anims[t]:
                lines.append(f'   <frame tileid="{f}" duration="{d}"/>')
            lines.append('  </animation>')
        lines.append(' </tile>')
    lines.append('</tileset>')
    return "\n".join(lines) + "\n"


def tsx_collection(name, entries):
    """entries: list of (file, w, h, collider (x, y, w, h) or None)"""
    mw, mh = max(e[1] for e in entries), max(e[2] for e in entries)
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             f'<tileset version="1.10" tiledversion="1.11.2" name="{name}" tilewidth="{mw}" tileheight="{mh}" tilecount="{len(entries)}" columns="0">',
             ' <grid orientation="orthogonal" width="1" height="1"/>']
    for i, (f, w, h, col) in enumerate(entries):
        lines.append(f' <tile id="{i}">')
        lines.append(f'  <image source="../Art/Objects/{f}" width="{w}" height="{h}"/>')
        if col:
            x, y, cw, ch = col
            lines.append('  <objectgroup draworder="index">')
            lines.append(f'   <object id="1" x="{x}" y="{y}" width="{cw}" height="{ch}"/>')
            lines.append('  </objectgroup>')
        lines.append(' </tile>')
    lines.append('</tileset>')
    return "\n".join(lines) + "\n"


def layer_xml(lid, name, w, h, data, extra=""):
    rows = [",".join(str(v) for v in data[y * w:(y + 1) * w]) for y in range(h)]
    return (f' <layer id="{lid}" name="{name}" width="{w}" height="{h}"{extra}>\n  <data encoding="csv">\n'
            + ",\n".join(rows) + "\n</data>\n </layer>\n")


def write(path, text):
    with open(path, "w", newline="\n") as f:
        f.write(text)
    print("wrote", os.path.relpath(path, OUT))


# ---------------------------------------------------------------- platformer map
def make_platformer_map():
    W, H = 64, 20
    # tile ids (local) in platformer_tiles: row*8 + col
    TL, TOP, TR = 0, 1, 2
    L, MID, R = 8, 9, 10
    BL, BOT, BR = 16, 17, 18
    PL, PM, PR = 3, 4, 5
    CRATE, BRICK = 6, 7
    FLOWER_R, FLOWER_Y, TUFT, MUSH, ROCK = 11, 12, 13, 14, 15
    SINGLE, SIGN, BUSH_L, BUSH_R, FLOWER_C = 19, 20, 21, 22, 23
    ground = [0] * (W * H)
    deco = [0] * (W * H)
    first = 1

    def set_(layer, x, y, t):
        if 0 <= x < W and 0 <= y < H:
            layer[y * W + x] = first + t

    def block(x0, x1, top, bottom=H - 1):
        for y in range(top, bottom + 1):
            for x in range(x0, x1 + 1):
                c = 0 if x == x0 else 2 if x == x1 else 1
                r = 0 if y == top else 2 if y == bottom and bottom < H - 1 else 1
                set_(ground, x, y, [[TL, TOP, TR], [L, MID, R], [BL, BOT, BR]][r][c])

    # terrain: (x0, x1, top row)
    floor = 17
    segments = [(0, 14, floor), (15, 22, floor - 2), (26, 33, floor), (34, 38, floor - 3), (39, 46, floor), (50, 63, floor - 1)]
    for x0, x1, top in segments:
        block(x0, x1, top)
    # floating platforms and blocks
    for x0, x1, y in ((9, 11, 12), (23, 25, 11), (28, 31, 9), (41, 44, 12), (46, 48, 8), (52, 54, 11)):
        set_(ground, x0, y, PL)
        for x in range(x0 + 1, x1):
            set_(ground, x, y, PM)
        set_(ground, x1, y, PR)
    block(18, 20, 10, 12)               # a floating island
    for x, y in ((5, floor - 1), (6, floor - 1), (6, floor - 2), (44, floor - 1)):
        set_(ground, x, y, CRATE)
    for x in range(56, 60):
        set_(ground, x, floor - 4, BRICK)
    # walls at the ends
    block(0, 0, 0, H - 1)
    block(63, 63, 0, H - 1)
    for y in range(H):
        set_(ground, 0, y, MID); set_(ground, 63, y, MID)
    # decorations on top of the ground
    rnd = random.Random(11)
    for x0, x1, top in segments:
        for x in range(x0 + 1, x1):
            if ground[(top - 1) * W + x]: continue
            r = rnd.random()
            t = FLOWER_R if r < 0.08 else FLOWER_Y if r < 0.15 else TUFT if r < 0.32 else MUSH if r < 0.36 else ROCK if r < 0.4 else None
            if t is not None: set_(deco, x, top - 1, t)
    set_(deco, 2, floor - 1, SIGN)
    set_(deco, 12, floor - 1, BUSH_L); set_(deco, 13, floor - 1, BUSH_R)
    set_(deco, 29, floor - 1, BUSH_L); set_(deco, 30, floor - 1, BUSH_R)
    set_(deco, 60, floor - 2, SIGN)

    solid = [TL, TOP, TR, L, MID, R, BL, BOT, BR, CRATE, BRICK, SINGLE]
    write(os.path.join(MAPS, "platformer_tiles.tsx"), tsx_atlas("platformer_tiles", "platformer_tiles.png", 8, 4, solid=solid, half=[PL, PM, PR]))

    # objects: spawn, coins, enemies (points; the template places entities on them)
    objs = []
    oid = [1]
    def point(name, typ, x, y):
        objs.append(f'  <object id="{oid[0]}" name="{name}" type="{typ}" x="{x * 16 + 8}" y="{y * 16 + 16}">\n   <point/>\n  </object>')
        oid[0] += 1
    point("PlayerStart", "Spawn", 3, floor - 1)
    coins = [(7, 15), (10, 11), (11, 11), (19, 9), (24, 10), (29, 8), (30, 8), (36, 13), (42, 11), (43, 11), (47, 7), (53, 10), (57, 12), (58, 12), (61, 15)]
    for i, (x, y) in enumerate(coins):
        point(f"Coin{i + 1}", "Coin", x, y)
    for i, (x, y) in enumerate(((28, floor - 1), (41, floor - 1), (54, floor - 2))):
        point(f"Slime{i + 1}", "Enemy", x, y)

    text = ['<?xml version="1.0" encoding="UTF-8"?>',
            f'<map version="1.10" tiledversion="1.11.2" orientation="orthogonal" renderorder="right-down" width="{W}" height="{H}" tilewidth="16" tileheight="16" infinite="0" backgroundcolor="#41a6f6" nextlayerid="6" nextobjectid="{oid[0]}">',
            ' <tileset firstgid="1" source="platformer_tiles.tsx"/>',
            ' <imagelayer id="1" name="Sky" repeatx="1" parallaxx="0.35" parallaxy="0.6" offsety="-80">',
            '  <image source="../Art/background.png" width="256" height="320"/>',
            ' </imagelayer>']
    body = "\n".join(text) + "\n"
    body += layer_xml(2, "Ground", W, H, ground)
    body += layer_xml(3, "Decorations", W, H, deco)
    body += ' <objectgroup id="4" name="Spawns">\n' + "\n".join(objs) + "\n </objectgroup>\n</map>\n"
    write(os.path.join(MAPS, "Platformer.tmx"), body)


# ---------------------------------------------------------------- top-down map
def make_topdown_map(objects):
    W, H = 40, 26
    GRASS = [0, 1, 2]
    FL_R, FL_W, TALL, STONES, STUMP = 3, 4, 5, 6, 7
    P_TL, P_T, P_TR, P_L, P_C, P_R, P_BL, P_B, P_BR = 8, 9, 10, 16, 17, 18, 24, 25, 26
    W_TL, W_T, W_TR, W_L, W_C, W_R, W_BL, W_B, W_BR = 11, 12, 13, 19, 20, 21, 27, 28, 29
    WATER_ANIM = 14
    F_L, F_M, F_R, F_POST = 32, 33, 34, 35
    first = 1
    ground = [0] * (W * H)
    over = [0] * (W * H)
    rnd = random.Random(21)
    for i in range(W * H):
        r = rnd.random()
        ground[i] = first + (GRASS[0] if r < 0.55 else GRASS[1] if r < 0.85 else GRASS[2])
        if r > 0.965: ground[i] = first + rnd.choice([FL_R, FL_W, TALL, STONES])

    def set_(layer, x, y, t):
        if 0 <= x < W and 0 <= y < H:
            layer[y * W + x] = first + t

    def nine(x0, y0, x1, y1, ids, center=None):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                r = 0 if y == y0 else 2 if y == y1 else 1
                c = 0 if x == x0 else 2 if x == x1 else 1
                t = ids[r * 3 + c]
                if r == 1 and c == 1 and center is not None: t = center
                set_(ground, x, y, t)

    PATH = [P_TL, P_T, P_TR, P_L, P_C, P_R, P_BL, P_B, P_BR]
    WATER = [W_TL, W_T, W_TR, W_L, W_C, W_R, W_BL, W_B, W_BR]

    def autotile(rects, ids, center=None):
        """Fills the union of rectangles, picking edge/corner tiles from which neighbours are inside."""
        inside = set()
        for x0, y0, x1, y1 in rects:
            for y in range(y0, y1 + 1):
                for x in range(x0, x1 + 1):
                    inside.add((x, y))
        def inn(x, y):  # beyond the map's edge counts as inside (the road runs off the map)
            return (x, y) in inside or not (0 <= x < W and 0 <= y < H)
        for (x, y) in inside:
            r = 0 if not inn(x, y - 1) else 2 if not inn(x, y + 1) else 1
            c = 0 if not inn(x - 1, y) else 2 if not inn(x + 1, y) else 1
            t = ids[r * 3 + c]
            if r == 1 and c == 1 and center is not None: t = center
            set_(ground, x, y, t)

    autotile([(0, 11, W - 1, 13), (18, 6, 20, 12)], PATH)        # the road and the path to the house
    autotile([(26, 16, 34, 22)], WATER, center=WATER_ANIM)        # pond (animated water)
    # fence along the top of the garden
    for x in range(6, 15):
        set_(over, x, 6, F_M)
    set_(over, 5, 6, F_L); set_(over, 15, 6, F_R)
    write(os.path.join(MAPS, "topdown_tiles.tsx"), tsx_atlas(
        "topdown_tiles", "topdown_tiles.png", 8, 6,
        solid=[W_TL, W_T, W_TR, W_L, W_C, W_R, W_BL, W_B, W_BR, WATER_ANIM, STUMP],
        half=[F_L, F_M, F_R, F_POST],
        anims={WATER_ANIM: [(14, 400), (15, 400), (22, 400), (23, 400)]}))

    # objects tileset (image collection) - colliders sit at the base (trunks, walls)
    entries = [("tree.png", 32, 40, (10, 30, 12, 8)), ("house.png", 48, 48, (4, 30, 40, 16)),
               ("bush.png", 20, 14, (2, 6, 16, 8)), ("rock.png", 16, 8, (1, 1, 14, 7))]
    write(os.path.join(MAPS, "topdown_objects.tsx"), tsx_collection("topdown_objects", entries))
    obj_first = 1 + 48
    objs = []
    oid = [1]
    def tile_obj(idx, x, y):  # x, y = bottom-left in pixels
        _, w, h, _ = entries[idx]
        objs.append(f'  <object id="{oid[0]}" gid="{obj_first + idx}" x="{x}" y="{y}" width="{w}" height="{h}"/>')
        oid[0] += 1
    tile_obj(1, 19 * 16 - 16, 6 * 16 + 4)     # house above the path
    trees = [(1, 4), (3, 2), (6, 3), (10, 2), (13, 4), (24, 3), (27, 5), (31, 2), (35, 4), (37, 7), (2, 9), (1, 16), (3, 21), (7, 24),
             (12, 22), (16, 25), (21, 23), (37, 15), (38, 22), (23, 18), (9, 17)]
    for tx, ty in trees:
        tile_obj(0, tx * 16 - 8, ty * 16 + 12)
    for bx, by in ((8, 9), (14, 9), (24, 9), (30, 15), (12, 16), (36, 12)):
        tile_obj(2, bx * 16, by * 16 + 14)
    for rx, ry in ((5, 15), (22, 15), (33, 8), (15, 19)):
        tile_obj(3, rx * 16, ry * 16 + 12)
    points = []
    def point(name, typ, x, y):
        points.append(f'  <object id="{oid[0]}" name="{name}" type="{typ}" x="{x * 16 + 8}" y="{y * 16 + 12}">\n   <point/>\n  </object>')
        oid[0] += 1
    point("PlayerStart", "Spawn", 19, 9)
    gems = [(4, 6), (11, 8), (26, 7), (33, 12), (6, 19), (17, 17), (25, 20), (35, 24), (14, 24), (28, 3)]
    for i, (x, y) in enumerate(gems):
        point(f"Gem{i + 1}", "Gem", x, y)

    body = ['<?xml version="1.0" encoding="UTF-8"?>',
            f'<map version="1.10" tiledversion="1.11.2" orientation="orthogonal" renderorder="right-down" width="{W}" height="{H}" tilewidth="16" tileheight="16" infinite="0" nextlayerid="6" nextobjectid="{oid[0]}">',
            ' <tileset firstgid="1" source="topdown_tiles.tsx"/>',
            f' <tileset firstgid="{obj_first}" source="topdown_objects.tsx"/>']
    text = "\n".join(body) + "\n"
    text += layer_xml(1, "Ground", W, H, ground)
    text += layer_xml(2, "Fences", W, H, over)
    text += ' <objectgroup id="3" name="Objects">\n' + "\n".join(objs) + "\n </objectgroup>\n"
    text += ' <objectgroup id="4" name="Spawns">\n' + "\n".join(points) + "\n </objectgroup>\n</map>\n"
    write(os.path.join(MAPS, "TopDown.tmx"), text)


if __name__ == "__main__":
    make_platformer_tiles()
    make_background()
    make_hero_side()
    make_hero_topdown()
    make_coin()
    make_gem()
    make_slime()
    make_topdown_tiles()
    objs = make_objects()
    make_platformer_map()
    make_topdown_map(objs)
