"""
Derives a tangent-space normal map from a color texture by treating its brightness as height.

    python3 tools/textures/make_normal_map.py projects/Demo/Textures/stone.png [strength] [invert]

Writes <name>_normal.png next to the input (OpenGL / glTF convention: +Y = up in the image).
Good enough for stone, bark, planks and other "bright = raised" surfaces; for anything else,
bake a real normal map in Blender. Requires Pillow (pip install pillow).
"""
import math
import sys
from PIL import Image, ImageFilter


def make(path, strength=2.5, invert=False):
    src = Image.open(path).convert("L").filter(ImageFilter.GaussianBlur(0.8))
    w, h = src.size
    px = src.load()
    out = Image.new("RGB", (w, h))
    dst = out.load()
    sign = -1.0 if invert else 1.0
    for y in range(h):
        for x in range(w):
            # Sobel gradient on the wrapped (tileable) height field
            def H(dx, dy): return px[(x + dx) % w, (y + dy) % h] / 255.0 * sign
            gx = (H(1, -1) + 2 * H(1, 0) + H(1, 1)) - (H(-1, -1) + 2 * H(-1, 0) + H(-1, 1))
            gy = (H(-1, 1) + 2 * H(0, 1) + H(1, 1)) - (H(-1, -1) + 2 * H(0, -1) + H(1, -1))
            nx, ny, nz = -gx * strength, gy * strength, 1.0   # image y grows downward; flip for +Y up
            l = math.sqrt(nx * nx + ny * ny + nz * nz)
            dst[x, y] = tuple(int(round((c / l * 0.5 + 0.5) * 255)) for c in (nx, ny, nz))
    name = path.rsplit(".", 1)[0] + "_normal.png"
    out.save(name, optimize=True)
    print("Wrote", name)


if __name__ == "__main__":
    make(sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 2.5, len(sys.argv) > 3 and sys.argv[3] == "invert")
