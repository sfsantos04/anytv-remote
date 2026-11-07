#!/usr/bin/env python3
"""make_icon.py - generate the 1024x1024 app icon (dark TV + D-pad)."""
import json
import os
from PIL import Image, ImageDraw

SIZE = 1024
BG_TOP = (14, 20, 32)
BG_BOTTOM = (6, 9, 16)
SCREEN_A = (26, 46, 74)
SCREEN_B = (12, 20, 34)
ACCENT = (86, 214, 184)
GLOW = (32, 120, 110)


def radial_background() -> Image.Image:
    """Per-pixel radial gradient (no visible rings)."""
    img = Image.new("RGB", (SIZE, SIZE), BG_BOTTOM)
    pixels = img.load()
    cx, cy = SIZE * 0.5, SIZE * 0.42
    maxd = (SIZE ** 2 + SIZE ** 2) ** 0.5
    lut = []
    for i in range(512):
        t = i / 511
        k = max(0.0, 1.0 - t * 1.35)
        lut.append(tuple(int(BG_BOTTOM[c] + (BG_TOP[c] - BG_BOTTOM[c]) * k) for c in range(3)))
    for y in range(SIZE):
        dy = (y - cy) ** 2
        for x in range(SIZE):
            dist = ((x - cx) ** 2 + dy) ** 0.5 / maxd
            pixels[x, y] = lut[min(511, int(dist * 2 * 511))]
    return img


def rounded_rect(draw, box, radius, fill):
    draw.rounded_rectangle(box, radius=radius, fill=fill)


def main() -> int:
    img = radial_background()
    draw = ImageDraw.Draw(img, "RGBA")

    # TV body
    body = [SIZE * 0.14, SIZE * 0.22, SIZE * 0.86, SIZE * 0.66]
    rounded_rect(draw, body, int(SIZE * 0.06), (18, 26, 42, 255))
    draw.rounded_rectangle(body, radius=int(SIZE * 0.06), outline=(58, 78, 108, 255), width=8)

    # screen with vertical gradient inside the body
    inset = int(SIZE * 0.032)
    screen = [body[0] + inset, body[1] + inset, body[2] - inset, body[3] - inset]
    screen_h = int(screen[3] - screen[1])
    for y in range(screen_h):
        t = y / max(1, screen_h - 1)
        color = tuple(int(SCREEN_A[i] + (SCREEN_B[i] - SCREEN_A[i]) * t) for i in range(3))
        draw.line([(screen[0], screen[1] + y), (screen[2], screen[1] + y)], fill=color)
    draw.rounded_rectangle(screen, radius=int(SIZE * 0.045), outline=(70, 96, 128, 255), width=5)

    # stand
    draw.polygon([(SIZE * 0.46, body[3]), (SIZE * 0.54, body[3]),
                  (SIZE * 0.59, SIZE * 0.72), (SIZE * 0.41, SIZE * 0.72)],
                 fill=(24, 34, 52, 255))
    rounded_rect(draw, [SIZE * 0.34, SIZE * 0.715, SIZE * 0.66, SIZE * 0.75],
                 int(SIZE * 0.018), (30, 42, 62, 255))

    # D-pad cross inside the screen
    cx = (screen[0] + screen[2]) / 2
    cy = (screen[1] + screen[3]) / 2
    arm = SIZE * 0.085
    thick = SIZE * 0.062
    glow_layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow_layer)
    for grow in range(26, 0, -2):
        gd.rounded_rectangle([cx - thick / 2 - grow, cy - arm - grow,
                              cx + thick / 2 + grow, cy + arm + grow],
                             radius=int(thick / 2 + grow), fill=GLOW + (min(28, grow),))
        gd.rounded_rectangle([cx - arm - grow, cy - thick / 2 - grow,
                              cx + arm + grow, cy + thick / 2 + grow],
                             radius=int(thick / 2 + grow), fill=GLOW + (min(28, grow),))
    img = Image.alpha_composite(img.convert("RGBA"), glow_layer).convert("RGB")
    draw = ImageDraw.Draw(img, "RGBA")

    for box in ([cx - thick / 2, cy - arm, cx + thick / 2, cy + arm],
                [cx - arm, cy - thick / 2, cx + arm, cy + thick / 2]):
        draw.rounded_rectangle(box, radius=int(thick / 2), fill=ACCENT + (235,))
    circle_r = thick * 0.52
    draw.ellipse([cx - circle_r, cy - circle_r, cx + circle_r, cy + circle_r],
                 fill=(10, 20, 30, 255), outline=ACCENT + (255,), width=6)

    # status dot
    draw.ellipse([screen[2] - 54, screen[1] + 30, screen[2] - 30, screen[1] + 54],
                 fill=(120, 240, 160, 255))

    out_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                           "ios", "Assets", "Assets.xcassets", "AppIcon.appiconset")
    os.makedirs(out_dir, exist_ok=True)
    img.convert("RGB").save(os.path.join(out_dir, "icon-1024.png"), "PNG")
    contents = {
        "images": [{"filename": "icon-1024.png", "idiom": "universal",
                    "platform": "ios", "size": "1024x1024"}],
        "info": {"author": "xcode", "version": 1},
    }
    with open(os.path.join(out_dir, "Contents.json"), "w", encoding="utf-8") as fh:
        json.dump(contents, fh, indent=2)
    print("icon written to", out_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
