"""Original boot/fastboot screens for nikel (1080x1920). Teal palette, own artwork."""
import math, random
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops

W, H = 1080, 1920
TEAL = (22, 124, 128)        # deep teal
TEAL_L = (78, 205, 196)      # light teal
TEAL_G = (43, 213, 200)      # glow
FONT_B = '/usr/share/fonts/opentype/inter/Inter-Bold.otf'
FONT_M = '/usr/share/fonts/opentype/inter/Inter-Medium.otf'
random.seed(4100)            # deterministic output


def robot_upright(scale=1.0):
    """Original robot: boxy head, visor stripe, single offset antenna, segmented body."""
    s = scale
    w, h = int(420 * s), int(640 * s)
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    line = TEAL_L + (255,)
    fill = TEAL + (110,)
    lw = max(3, int(6 * s))
    S = lambda *v: [int(x * s) for x in v]
    # antenna (single, off-centre)
    d.line(S(270, 30, 290, 95), fill=line, width=lw)
    d.ellipse(S(256, 10, 286, 40), outline=line, width=lw, fill=fill)
    # head
    d.rounded_rectangle(S(90, 95, 330, 250), radius=int(26 * s), outline=line, width=lw, fill=fill)
    # visor stripe
    d.rounded_rectangle(S(120, 145, 300, 190), radius=int(18 * s), outline=line, width=lw, fill=TEAL_G + (170,))
    d.line(S(140, 167, 280, 167), fill=(200, 255, 250, 255), width=max(2, int(3 * s)))
    # neck
    d.rectangle(S(185, 250, 235, 275), outline=line, width=lw, fill=fill)
    # torso
    d.rounded_rectangle(S(70, 275, 350, 480), radius=int(18 * s), outline=line, width=lw, fill=fill)
    # chest panel + indicator dots
    d.rounded_rectangle(S(125, 310, 295, 400), radius=int(10 * s), outline=line, width=max(2, int(4 * s)))
    for i, x in enumerate((150, 190, 230, 270)):
        d.ellipse(S(x - 9, 425, x + 9, 443), fill=(TEAL_G if i % 2 == 0 else TEAL_L) + (255,))
    # arms: two segments each
    for side in (-1, 1):
        x0 = 70 if side < 0 else 350
        xa = x0 - 50 if side < 0 else x0
        d.rounded_rectangle(S(xa, 285, xa + 50, 370), radius=int(12 * s), outline=line, width=lw, fill=fill)
        d.rounded_rectangle(S(xa + 4, 378, xa + 46, 460), radius=int(12 * s), outline=line, width=lw, fill=fill)
    # hips + legs (segmented)
    d.rounded_rectangle(S(110, 480, 310, 515), radius=int(10 * s), outline=line, width=lw, fill=fill)
    for x in (125, 235):
        d.rounded_rectangle(S(x, 520, x + 60, 575), radius=int(10 * s), outline=line, width=lw, fill=fill)
        d.rounded_rectangle(S(x - 6, 580, x + 66, 625), radius=int(12 * s), outline=line, width=lw, fill=fill)
    return im


def hologram(layer):
    """Scan lines, glitch bands and a dissolve toward the right end."""
    w, h = layer.size
    px = layer.load()
    # dissolve: probability of dropping a pixel rises toward the right (robot's feet)
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            t = x / w
            drop = max(0.0, (t - 0.55) / 0.45) ** 1.6
            if random.random() < drop:
                px[x, y] = (0, 0, 0, 0)
            elif y % 6 in (0, 1):                     # scan lines
                px[x, y] = (r, g, b, int(a * 0.35))
    # stray hologram particles beyond the dissolving end
    d = ImageDraw.Draw(layer)
    for _ in range(260):
        x = int(w * (0.7 + 0.3 * random.random()))
        y = random.randint(0, h - 1)
        if random.random() < 0.6:
            rr = random.choice((2, 3, 4))
            d.rectangle((x, y, x + rr, y + rr), fill=TEAL_L + (random.randint(90, 220),))
    # glitch: shift a few horizontal bands sideways
    out = layer.copy()
    for _ in range(5):
        y0 = random.randint(0, h - 30); bh = random.randint(6, 22); dx = random.choice((-1, 1)) * random.randint(10, 34)
        band = layer.crop((0, y0, w, y0 + bh))
        out.paste((0, 0, 0, 0), (0, y0, w, y0 + bh))
        out.alpha_composite(band, (max(0, dx), y0)) if dx > 0 else out.alpha_composite(band.crop((-dx, 0, w, bh)), (0, y0))
    return out


def glow_text(base, text, font, cy, color, glow, streaks=True):
    d = ImageDraw.Draw(base)
    tw = d.textlength(text, font=font)
    x = (W - tw) / 2
    asc, desc = font.getmetrics()
    y = cy - (asc + desc) / 2
    g = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(g)
    gd.text((x, y), text, font=font, fill=glow + (255,))
    if streaks:   # horizontal light streaks behind the word, like the stock screen
        for i in range(-5, 6):
            yy = cy + i * 7
            span = 330 - abs(i) * 38
            gd.line((W / 2 - span, yy, W / 2 + span, yy), fill=glow + (int(70 - abs(i) * 10),), width=2)
    base.alpha_composite(g.filter(ImageFilter.GaussianBlur(14)))
    base.alpha_composite(g.filter(ImageFilter.GaussianBlur(4)))
    d.text((x, y), text, font=font, fill=color + (255,))


def fastboot():
    base = Image.new('RGBA', (W, H), (0, 0, 0, 255))
    rob = robot_upright(1.0).rotate(90, expand=True, resample=Image.BICUBIC)   # lying on its back-side
    holo = hologram(rob)
    cx, cy = W // 2, 860
    x0, y0 = cx - holo.width // 2, cy - holo.height // 2
    # projector: emitter line + faint light cone under the hologram
    cone = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    cd = ImageDraw.Draw(cone)
    by = y0 + holo.height + 40
    cd.polygon([(cx - 120, by), (cx + 120, by), (x0 + holo.width - 20, y0 + 40), (x0 + 20, y0 + 40)], fill=TEAL + (34,))
    cd.ellipse((cx - 300, by - 14, cx + 300, by + 14), outline=TEAL_L + (200,), width=3)
    cd.ellipse((cx - 140, by - 6, cx + 140, by + 6), fill=TEAL_G + (230,))
    base.alpha_composite(cone.filter(ImageFilter.GaussianBlur(3)))
    # hologram glow + body
    g = Image.new('RGBA', (W, H), (0, 0, 0, 0)); g.alpha_composite(holo, (x0, y0))
    base.alpha_composite(g.filter(ImageFilter.GaussianBlur(12)))
    base.alpha_composite(g)
    glow_text(base, 'FASTBOOT', ImageFont.truetype(FONT_B, 110), 1330, TEAL_L, TEAL_G)
    return base


def bootlogo():
    base = Image.new('RGBA', (W, H), (0, 0, 0, 255))
    d = ImageDraw.Draw(base)
    cx, cy = W // 2, 930
    rw, rh = 120, 30            # biscuit half-width / thickness
    dark, edge = (38, 30, 28), (70, 58, 54)
    # bottom biscuit, cream, top biscuit (side view, slight perspective)
    d.rounded_rectangle((cx - rw, cy + 18, cx + rw, cy + 18 + rh), radius=14, fill=dark, outline=edge, width=3)
    cream = Image.new('RGBA', (W, H), (0, 0, 0, 0)); cd = ImageDraw.Draw(cream)
    cd.rounded_rectangle((cx - rw + 8, cy - 2, cx + rw - 8, cy + 22), radius=10, fill=TEAL_L + (255,))
    base.alpha_composite(cream.filter(ImageFilter.GaussianBlur(10)))
    d.rounded_rectangle((cx - rw + 8, cy - 2, cx + rw - 8, cy + 22), radius=10, fill=TEAL_L)
    d.ellipse((cx - rw, cy - 62, cx + rw, cy - 2), fill=dark, outline=edge, width=3)
    d.rounded_rectangle((cx - rw, cy - 44, cx + rw, cy - 2 - 4), radius=14, fill=dark)
    d.ellipse((cx - rw, cy - 80, cx + rw, cy - 20), fill=(48, 38, 35), outline=edge, width=3)
    # own simple top pattern: ring of small dots
    for k in range(12):
        a = 2 * math.pi * k / 12
        px, py = cx + math.cos(a) * 78, cy - 50 + math.sin(a) * 18
        d.ellipse((px - 5, py - 3, px + 5, py + 3), fill=edge)
    d.ellipse((cx - 22, cy - 56, cx + 22, cy - 44), outline=edge, width=3)
    # wordmark
    f = ImageFont.truetype(FONT_M, 40)
    tw = d.textlength('nikel', font=f)
    d.text(((W - tw) / 2, cy + 80), 'nikel', font=f, fill=(170, 175, 175))
    return base


if __name__ == '__main__':
    fastboot().save('new_fastboot.png')
    bootlogo().save('new_bootlogo.png')
    print('ok')
