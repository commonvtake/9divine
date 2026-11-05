"""
Generates the city textures for the demo project: building facades (albedo + lit-window
emissive + normal), asphalt, sidewalk pavers, interior floors and walls, and neon sign boards.

    python3 tools/textures/make_city_textures.py [output_dir]

Default output: projects/Demo/Textures. Everything is procedural and seeded, so re-running
produces identical files. Requires Pillow and NumPy.

Facades are drawn for world-space UVs (MeshRendererComponent::worldUV): one image covers
16 m x 16 m of wall = 4 window bays x 4 floors, so a building's tiling is 1/16 = 0.0625.
"""
import os
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "projects", "Demo", "Textures")
FONT = os.path.join(os.path.dirname(__file__), "..", "..", "assets", "fonts", "Roboto-Medium.ttf")
os.makedirs(OUT, exist_ok=True)


def save(img, name):
    img.save(os.path.join(OUT, name), optimize=True)
    print("wrote", name)


def normal_from_height(h, strength):
    """Tangent-space normal map (OpenGL convention, +Y up) from a 0..1 height array (tileable)."""
    gx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5
    gy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5
    n = np.dstack([-gx * strength, gy * strength, np.ones_like(h)])
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    return Image.fromarray(((n * 0.5 + 0.5) * 255).round().astype(np.uint8), "RGB")


def noise(rng, size, scale):
    """Smooth value noise: random grid upsampled with bicubic filtering (tileable by wrapping)."""
    g = rng.random((size // scale + 1, size // scale + 1))
    g[-1, :] = g[0, :]
    g[:, -1] = g[:, 0]
    img = Image.fromarray((g * 255).astype(np.uint8)).resize((size + scale, size + scale), Image.BICUBIC)
    return np.asarray(img, dtype=np.float32)[:size, :size] / 255.0


# ------------------------------------------------------------------ facades
S = 512          # pixels per 16 m
BAY = S // 4     # 4 m


def facade(name, seed, wall_fn, window_w, window_h, frame_rgb, lit_ratio):
    rng = np.random.default_rng(seed)
    albedo = wall_fn(rng)                       # HxWx3 float 0..1
    height = np.full((S, S), 0.6, np.float32)
    height += (albedo.mean(axis=2) - albedo.mean()) * 0.6   # wall surface detail
    emissive = np.zeros((S, S, 3), np.float32)
    lit_colors = [(1.0, 0.78, 0.45), (1.0, 0.86, 0.62), (0.62, 0.8, 1.0), (1.0, 0.5, 0.7), (0.95, 0.95, 0.85)]
    for fy in range(4):
        for bx in range(4):
            x0 = bx * BAY + (BAY - window_w) // 2
            y0 = fy * BAY + (BAY - window_h) // 2   # centred, so flipped images look the same
            x1, y1 = x0 + window_w, y0 + window_h
            f = 5
            albedo[y0 - f:y1 + f, x0 - f:x1 + f] = frame_rgb
            height[y0 - f:y1 + f, x0 - f:x1 + f] = 0.75             # frame stands proud
            # sill
            albedo[y1 + f:y1 + f + 4, x0 - f - 3:x1 + f + 3] = np.array(frame_rgb) * 0.8
            height[y1 + f:y1 + f + 4, x0 - f - 3:x1 + f + 3] = 0.8
            glass = np.array((0.10, 0.13, 0.17)) + rng.random() * 0.04
            # reflection gradient on the glass
            grad = np.linspace(1.25, 0.8, window_h)[:, None, None]
            albedo[y0:y1, x0:x1] = glass * grad
            height[y0:y1, x0:x1] = 0.2                              # recessed
            # mullion
            mx = (x0 + x1) // 2
            albedo[y0:y1, mx - 2:mx + 2] = frame_rgb
            height[y0:y1, mx - 2:mx + 2] = 0.7
            if rng.random() < lit_ratio:
                c = np.array(lit_colors[rng.integers(len(lit_colors))])
                # curtains: a lit window is brighter at the bottom, sometimes half covered
                glow = np.linspace(0.75, 1.0, window_h)[:, None, None] * c
                emissive[y0:y1, x0:x1] = glow
                if rng.random() < 0.4:
                    side = rng.integers(2)
                    cx0, cx1 = (x0, mx) if side == 0 else (mx, x1)
                    emissive[y0:y1, cx0:cx1] *= 0.25
                emissive[y0:y1, mx - 2:mx + 2] = 0
                albedo[y0:y1, x0:x1] = albedo[y0:y1, x0:x1] * 0.5 + c * 0.35
    a = Image.fromarray((np.clip(albedo, 0, 1) * 255).astype(np.uint8), "RGB")
    e = Image.fromarray((np.clip(emissive, 0, 1) * 255).astype(np.uint8), "RGB")
    save(a, name + ".png")
    save(e, name + "_emissive.png")
    save(normal_from_height(height.astype(np.float32), 6.0), name + "_normal.png")


def brick_wall(rng):
    base = np.array((0.58, 0.30, 0.24))
    img = np.zeros((S, S, 3), np.float32)
    bw, bh = 16, 6
    mortar = np.array((0.62, 0.58, 0.52))
    img[:] = mortar
    for row in range(S // bh):
        off = (bw // 2) * (row % 2)
        for col in range(-1, S // bw + 1):
            x0, y0 = col * bw + off + 1, row * bh + 1
            tint = base * (0.8 + rng.random() * 0.35)
            img[y0:y0 + bh - 1, max(0, x0):max(0, min(S, x0 + bw - 1))] = tint
    n = noise(rng, S, 8)[..., None]
    return img * (0.85 + 0.3 * n)


def concrete_wall(rng):
    base = np.array((0.66, 0.66, 0.64))
    n = noise(rng, S, 32)[..., None] * 0.6 + noise(rng, S, 4)[..., None] * 0.4
    img = base * (0.8 + 0.35 * n)
    # panel joints every 2 m, darker floor bands
    for i in range(0, S, BAY // 2):
        img[:, i:i + 2] *= 0.7
    for i in range(0, S, BAY):
        img[i:i + 10] = img[i:i + 10] * 0.75
    # rain streaks
    for _ in range(60):
        x = rng.integers(S)
        y = rng.integers(S)
        l = rng.integers(20, 120)
        img[y:y + l, x:x + 2] *= 0.85
    return img


def tile_wall(rng):
    base = np.array((0.32, 0.55, 0.55))
    img = np.zeros((S, S, 3), np.float32)
    t = 8
    img[:] = (0.8, 0.82, 0.8)
    for y in range(0, S, t):
        for x in range(0, S, t):
            img[y + 1:y + t, x + 1:x + t] = base * (0.85 + rng.random() * 0.3)
    return img


facade("facade_brick", 11, brick_wall, 56, 72, (0.85, 0.82, 0.76), 0.42)
facade("facade_concrete", 12, concrete_wall, 92, 56, (0.28, 0.29, 0.31), 0.5)
facade("facade_tile", 13, tile_wall, 60, 76, (0.92, 0.92, 0.9), 0.38)

# ------------------------------------------------------------------ ground
G = 256
rng = np.random.default_rng(21)
n = noise(rng, G, 4) * 0.5 + noise(rng, G, 16) * 0.3 + rng.random((G, G)) * 0.2
asphalt = np.dstack([0.20 + 0.12 * n, 0.20 + 0.12 * n, 0.22 + 0.12 * n])
img = Image.fromarray((asphalt * 255).astype(np.uint8), "RGB")
d = ImageDraw.Draw(img)
for _ in range(8):   # cracks and tar patches
    x, y = rng.integers(G), rng.integers(G)
    pts = [(x, y)]
    for _ in range(6):
        x += rng.integers(-14, 15); y += rng.integers(-14, 15)
        pts.append((x, y))
    d.line(pts, fill=(28, 28, 30), width=1)
save(img, "asphalt.png")
save(normal_from_height(np.asarray(img.convert("L"), np.float32) / 255.0, 3.0), "asphalt_normal.png")

rng = np.random.default_rng(22)
pav = np.zeros((G, G, 3), np.float32)
hgt = np.zeros((G, G), np.float32)
P = 64   # 4 pavers per image (tiling 0.5 -> 1 m pavers)
for y in range(0, G, P):
    for x in range(0, G, P):
        c = np.array((0.60, 0.58, 0.55)) * (0.88 + rng.random() * 0.2)
        pav[y + 2:y + P - 2, x + 2:x + P - 2] = c
        hgt[y + 2:y + P - 2, x + 2:x + P - 2] = 1.0
pav[hgt == 0] = (0.36, 0.35, 0.34)
nn = noise(rng, G, 8)[..., None]
pav *= 0.9 + 0.2 * nn
save(Image.fromarray((np.clip(pav, 0, 1) * 255).astype(np.uint8), "RGB"), "sidewalk.png")
save(normal_from_height(np.asarray(Image.fromarray((hgt * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2)), np.float32) / 255.0, 4.0), "sidewalk_normal.png")

# ------------------------------------------------------------------ interiors
rng = np.random.default_rng(31)
wood = np.zeros((G, G, 3), np.float32)
hgt = np.ones((G, G), np.float32)
PW = 32   # plank width (tiling 1 -> 8 planks per metre... set tiling per use)
for x in range(0, G, PW):
    stagger = rng.integers(G)
    c = np.array((0.55, 0.36, 0.22)) * (0.8 + rng.random() * 0.35)
    grain = noise(rng, G, 4)[:, :PW] if PW <= G else 0
    wood[:, x:x + PW] = c * (0.85 + 0.25 * grain[..., None])
    wood[:, x:x + 1] *= 0.4
    hgt[:, x:x + 1] = 0.3
    wood[stagger:stagger + 1, x:x + PW] *= 0.4
    hgt[stagger:stagger + 1, x:x + PW] = 0.3
save(Image.fromarray((np.clip(wood, 0, 1) * 255).astype(np.uint8), "RGB"), "wood_floor.png")
save(normal_from_height(hgt, 3.0), "wood_floor_normal.png")

checker = np.zeros((G, G, 3), np.float32)
T = 32
for y in range(0, G, T):
    for x in range(0, G, T):
        dark = ((x // T) + (y // T)) % 2
        checker[y:y + T, x:x + T] = (0.16, 0.15, 0.15) if dark else (0.86, 0.84, 0.78)
checker[::T, :] = 0.45
checker[:, ::T] = 0.45
save(Image.fromarray((checker * 255).astype(np.uint8), "RGB"), "tile_floor.png")

rng = np.random.default_rng(32)
wp = np.zeros((G, G, 3), np.float32)
wp[:] = (0.52, 0.18, 0.16)   # oxblood wallpaper with pale stripes
for x in range(0, G, 32):
    wp[:, x + 13:x + 19] = (0.64, 0.28, 0.24)
    wp[:, x + 15:x + 17] = (0.78, 0.62, 0.42)
wp *= 0.9 + 0.15 * noise(rng, G, 8)[..., None]
save(Image.fromarray((np.clip(wp, 0, 1) * 255).astype(np.uint8), "RGB"), "wallpaper.png")


# ------------------------------------------------------------------ shop windows
# Painted shop interiors seen through the glass. Used as both albedo and emissive map, so the
# bright parts glow and the shapes in front of them read as silhouettes.
def interior(name, draw):
    W, H = 256, 128
    img = Image.new("RGB", (W, H))
    px = np.zeros((H, W, 3), np.float32)
    # back wall: lit from above, falling off toward the floor
    px[:] = np.linspace(1.0, 0.45, H)[:, None, None] * np.array((1.0, 1.0, 1.0))
    img = Image.fromarray((px * 255).astype(np.uint8), "RGB")
    draw(ImageDraw.Draw(img), np.random.default_rng(len(name)))
    save(img, name + ".png")


def shelves(d, rng):
    for y in (34, 62, 90):
        d.rectangle([8, y, 248, y + 4], fill=(70, 60, 50))
        x = 12
        while x < 240:
            w = int(rng.integers(6, 14))
            h = int(rng.integers(10, 22))
            c = tuple(int(v) for v in rng.integers(60, 255, 3))
            d.rectangle([x, y - h, x + w, y - 1], fill=c)
            x += w + int(rng.integers(1, 4))
    d.rectangle([0, 104, 256, 128], fill=(40, 34, 30))          # counter in front
    for x in (50, 128, 206):                                     # hanging lamps
        d.line([x, 0, x, 12], fill=(30, 30, 30), width=2)
        d.ellipse([x - 9, 10, x + 9, 22], fill=(255, 240, 200))


def bar(d, rng):
    d.rectangle([0, 0, 256, 128], fill=(180, 90, 140))
    for y in (30, 56):
        d.rectangle([10, y, 246, y + 3], fill=(60, 30, 50))
        for x in range(16, 240, 9):
            h = int(rng.integers(14, 22))
            c = tuple(int(v) for v in rng.integers(80, 230, 3))
            d.rectangle([x, y - h, x + 5, y - 1], fill=c)
            d.rectangle([x + 1, y - h - 5, x + 3, y - h], fill=c)
    d.rectangle([0, 86, 256, 128], fill=(35, 18, 30))           # bar counter
    for x in range(20, 250, 34):                                 # stools
        d.rectangle([x, 98, x + 14, 104], fill=(20, 10, 15))
        d.rectangle([x + 6, 104, x + 8, 128], fill=(20, 10, 15))


def machines(d, rng):
    d.rectangle([0, 0, 256, 128], fill=(210, 240, 220))
    for i, x in enumerate(range(6, 250, 40)):
        d.rectangle([x, 26, x + 34, 112], fill=(30, 40, 50))        # cabinet
        screen = tuple(int(v) for v in rng.integers(80, 255, 3))
        d.rectangle([x + 4, 32, x + 30, 58], fill=screen)          # glowing screen
        d.rectangle([x + 6, 68, x + 28, 74], fill=(200, 60, 60))   # control panel
        d.ellipse([x + 8, 66, x + 13, 71], fill=(255, 220, 80))
    d.rectangle([0, 114, 256, 128], fill=(25, 25, 30))


def shutter(d, rng):
    d.rectangle([0, 0, 256, 128], fill=(95, 98, 102))
    for y in range(0, 128, 8):
        d.rectangle([0, y, 256, y + 1], fill=(55, 57, 60))
        d.rectangle([0, y + 2, 256, y + 3], fill=(130, 133, 137))
    for _ in range(6):                                           # graffiti tags
        x, y = int(rng.integers(10, 200)), int(rng.integers(20, 100))
        c = tuple(int(v) for v in rng.integers(40, 255, 3))
        d.line([(x, y), (x + 20, y - 10), (x + 35, y + 6), (x + 50, y - 4)], fill=c, width=3)


interior("window_shelves", shelves)
interior("window_bar", bar)
interior("window_arcade", machines)
interior("window_shutter", shutter)

# ------------------------------------------------------------------ neon signs
def sign(name, text, color, w=512, h=160, sub=None):
    img = Image.new("RGB", (w, h), (12, 10, 16))
    d = ImageDraw.Draw(img)
    size = int(h * (0.52 if sub else 0.62))
    font = ImageFont.truetype(FONT, size)
    while d.textlength(text, font=font) > w * 0.86:
        size -= 4
        font = ImageFont.truetype(FONT, size)
    tw = d.textlength(text, font=font)
    y = h * (0.12 if sub else 0.16)
    glow = Image.new("RGB", (w, h), (0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.text(((w - tw) / 2, y), text, font=font, fill=color)
    if sub:
        sf = ImageFont.truetype(FONT, int(h * 0.2))
        sw = d.textlength(sub, font=sf)
        gd.text(((w - sw) / 2, h * 0.72), sub, font=sf, fill=tuple(int(c * 0.85) for c in color))
    halo = glow.filter(ImageFilter.GaussianBlur(10))
    img = Image.fromarray(np.clip(np.asarray(img, np.int32) + np.asarray(halo, np.int32) * 0.8, 0, 255).astype(np.uint8))
    core = Image.fromarray(np.clip(np.asarray(glow, np.int32) * 0.6 + 255 * 0.4 * (np.asarray(glow.convert("L"))[..., None] > 40), 0, 255).astype(np.uint8))
    img = Image.fromarray(np.maximum(np.asarray(img), np.asarray(core)))
    d = ImageDraw.Draw(img)
    d.rectangle([3, 3, w - 4, h - 4], outline=tuple(int(c * 0.7) for c in color), width=4)
    save(img, name + ".png")


sign("sign_noodles", "NOODLES", (255, 70, 60), sub="OPEN LATE")
sign("sign_hotel", "HOTEL", (90, 200, 255))
sign("sign_bar9", "BAR 9", (255, 80, 200))
sign("sign_arcade", "ARCADE", (120, 255, 120), sub="1 CREDIT")
sign("sign_subway", "SUBWAY", (255, 210, 80), sub="EMBER ISLE LINE")
sign("sign_apartments", "APT 9", (255, 170, 90))
sign("sign_divine", "DIVINE CITY", (255, 255, 255), w=768, h=160)
