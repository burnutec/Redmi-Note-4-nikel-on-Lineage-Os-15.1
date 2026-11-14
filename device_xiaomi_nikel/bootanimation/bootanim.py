"""nikel boot animation: own artwork, teal palette, 1080x1920.

Story (part0, plays once): a dusty, cobwebbed phone on a shelf -> the teal
hologram robot beams in -> wipes it clean -> "Happy 10th birthday!" /
"Come on, you've got work to do." -> a portal opens -> the robot carries the
phone into it -> flash -> "2026".
Loop (part1): the clean phone glows in front of the portal, cookie on screen.
No real logos, mascots or brand marks; the phone only loosely resembles a
2016 slab phone (three key dots under the screen, "nikel" on the shelf label).
"""
import math, os, random, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_screens import robot_upright, TEAL, TEAL_L, TEAL_G, FONT_B, FONT_M  # noqa: E402

W, H = 1080, 1920
FPS = 15
OUT = sys.argv[1] if len(sys.argv) > 1 else 'bootanim_out'
QUICK = os.environ.get('QUICK')          # render only key frames for a preview

F_TITLE = ImageFont.truetype(FONT_B, 64)
F_BUBBLE = ImageFont.truetype(FONT_B, 50)
F_YEAR = ImageFont.truetype(FONT_B, 230)
F_SMALL = ImageFont.truetype(FONT_M, 46)
F_LABEL = ImageFont.truetype(FONT_M, 30)

# ---------------------------------------------------------------- helpers
def clamp(x, a=0.0, b=1.0):
    return max(a, min(b, x))

def ease(t):                     # smoothstep
    t = clamp(t)
    return t * t * (3 - 2 * t)

def seg(f, a, b):                # 0..1 progress of frame f inside [a, b]
    return clamp((f - a) / float(b - a))

def lerp(a, b, t):
    return a + (b - a) * t

# ---------------------------------------------------------------- static art
random.seed(1016)
PH_W, PH_H = 380, 700            # phone body size (front view)

def phone_art(screen='off'):
    """Cartoon front view of a 2016 metal slab phone. screen: 'off' | 'cookie'."""
    im = Image.new('RGBA', (PH_W, PH_H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, PH_W - 1, PH_H - 1), radius=54, fill=(150, 140, 125, 255))   # metal rim
    d.rounded_rectangle((8, 8, PH_W - 9, PH_H - 9), radius=48, fill=(24, 24, 26, 255))      # front glass
    d.rounded_rectangle((150, 46, 230, 56), radius=5, fill=(60, 60, 64, 255))               # earpiece
    d.ellipse((246, 42, 262, 58), fill=(40, 46, 60, 255))                                   # front camera
    sx0, sy0, sx1, sy1 = 28, 92, PH_W - 29, PH_H - 120
    d.rectangle((sx0, sy0, sx1, sy1), fill=(6, 7, 9, 255))                                  # screen
    for x in (96, PH_W // 2, PH_W - 96):                                                    # 3 key dots
        d.ellipse((x - 7, PH_H - 72, x + 7, PH_H - 58), fill=(70, 72, 76, 255))
    if screen == 'cookie':
        cx, cy = PH_W // 2, (sy0 + sy1) // 2 - 10
        glow = Image.new('RGBA', im.size, (0, 0, 0, 0)); gd = ImageDraw.Draw(glow)
        gd.ellipse((cx - 90, cy - 70, cx + 90, cy + 70), fill=TEAL_G + (120,))
        im.alpha_composite(glow.filter(ImageFilter.GaussianBlur(24)))
        d = ImageDraw.Draw(im)
        d.rounded_rectangle((cx - 70, cy + 4, cx + 70, cy + 24), radius=9, fill=(38, 30, 28, 255))
        d.rounded_rectangle((cx - 64, cy - 8, cx + 64, cy + 8), radius=7, fill=TEAL_L + (255,))
        d.ellipse((cx - 70, cy - 40, cx + 70, cy - 4), fill=(48, 38, 35, 255), outline=(70, 58, 54, 255), width=2)
        for k in range(10):
            a = 2 * math.pi * k / 10
            px, py = cx + math.cos(a) * 46, cy - 22 + math.sin(a) * 10
            d.ellipse((px - 3, py - 2, px + 3, py + 2), fill=(70, 58, 54, 255))
        for k in range(3):                                                                  # key dots lit
            x = (96, PH_W // 2, PH_W - 96)[k]
            d.ellipse((x - 7, PH_H - 72, x + 7, PH_H - 58), fill=TEAL_L + (255,))
    return im

def grime_layer():
    """Dust, smudges and cobwebs over the phone (faded out while it is cleaned)."""
    im = Image.new('RGBA', (PH_W + 160, PH_H + 160), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    ox, oy = 80, 80
    for _ in range(2600):                                    # dust specks
        x = ox + random.randint(4, PH_W - 4); y = oy + random.randint(4, PH_H - 4)
        c = random.randint(90, 150)
        d.point((x, y), fill=(c, c - 8, c - 20, random.randint(120, 230)))
    for _ in range(14):                                      # smudges
        x = ox + random.randint(20, PH_W - 20); y = oy + random.randint(60, PH_H - 60)
        r = random.randint(25, 70)
        d.ellipse((x - r, y - r // 2, x + r, y + r // 2), fill=(120, 108, 86, 38))
    im = im.filter(ImageFilter.GaussianBlur(1.2))
    d = ImageDraw.Draw(im)
    web = (210, 210, 205, 150)
    for (cx, cy, sx, sy) in ((ox, oy, 1, 1), (ox + PH_W, oy, -1, 1), (ox + PH_W, oy + PH_H, -1, -1)):
        for k in range(7):                                   # radial threads
            a = math.radians(5 + k * 13.5)
            d.line((cx, cy, cx + sx * math.cos(a) * 170, cy + sy * math.sin(a) * 170), fill=web, width=2)
        for rr in (40, 80, 120, 160):                         # spiral rings
            pts = [(cx + sx * math.cos(math.radians(5 + k * 13.5)) * rr * (0.92 + 0.08 * (k % 2)),
                    cy + sy * math.sin(math.radians(5 + k * 13.5)) * rr * (0.92 + 0.08 * (k % 2))) for k in range(7)]
            d.line(pts, fill=web, width=2)
    d.line((ox + PH_W - 40, oy + PH_H, ox + PH_W + 60, oy + PH_H + 70), fill=web, width=2)  # loose thread
    return im

PHONE_OFF = phone_art('off')
PHONE_ON = phone_art('cookie')
GRIME = grime_layer()
ROBOT = robot_upright(0.82)

def scanline(layer, f, strength=0.4):
    """Hologram look: scan lines that crawl, plus slight transparency."""
    w, h = layer.size
    mask = Image.new('L', (w, h), 255)
    md = ImageDraw.Draw(mask)
    off = (f * 2) % 6
    for y in range(off, h, 6):
        md.line((0, y, w, y), fill=int(255 * (1 - strength)))
    out = layer.copy()
    a = out.getchannel('A')
    out.putalpha(Image.composite(a, Image.new('L', (w, h), 0), mask).point(lambda v: v))
    from PIL import ImageChops
    out.putalpha(ImageChops.multiply(a, mask))
    return out

def glow_paste(base, layer, xy, blur=14, glow_alpha=1.0):
    g = Image.new('RGBA', base.size, (0, 0, 0, 0))
    g.alpha_composite(layer, (int(xy[0]), int(xy[1])))
    if glow_alpha > 0:
        gl = g.filter(ImageFilter.GaussianBlur(blur))
        if glow_alpha < 1:
            gl.putalpha(gl.getchannel('A').point(lambda v: int(v * glow_alpha)))
        base.alpha_composite(gl)
    base.alpha_composite(g)

def centered_text(base, text, font, cy, fill, glow=None):
    d = ImageDraw.Draw(base)
    tw = d.textlength(text, font=font)
    asc, desc = font.getmetrics()
    x, y = (W - tw) / 2, cy - (asc + desc) / 2
    if glow:
        g = Image.new('RGBA', base.size, (0, 0, 0, 0))
        ImageDraw.Draw(g).text((x, y), text, font=font, fill=glow)
        base.alpha_composite(g.filter(ImageFilter.GaussianBlur(16)))
    d.text((x, y), text, font=font, fill=fill)

def bubble(base, text, anchor, alpha):
    """Speech bubble above the robot."""
    if alpha <= 0:
        return
    layer = Image.new('RGBA', base.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    lines = text.split('\n')
    tw = max(d.textlength(l, font=F_BUBBLE) for l in lines)
    lh = 64
    bw, bh = int(tw + 70), int(lh * len(lines) + 46)
    x0, y0 = int(anchor[0] - bw / 2), int(anchor[1] - bh)
    x0 = max(30, min(W - 30 - bw, x0))
    d.rounded_rectangle((x0, y0, x0 + bw, y0 + bh), radius=28, fill=(8, 30, 32, 235), outline=TEAL_L + (255,), width=4)
    tx = anchor[0]
    d.polygon([(tx - 26, y0 + bh - 2), (tx + 8, y0 + bh - 2), (tx - 30, y0 + bh + 44)], fill=(8, 30, 32, 235))
    d.line([(tx - 26, y0 + bh), (tx - 30, y0 + bh + 44), (tx + 8, y0 + bh)], fill=TEAL_L + (255,), width=4)
    for i, l in enumerate(lines):
        lw = d.textlength(l, font=F_BUBBLE)
        d.text((x0 + (bw - lw) / 2, y0 + 22 + i * lh), l, font=F_BUBBLE, fill=(225, 255, 250, 255))
    if alpha < 1:
        layer.putalpha(layer.getchannel('A').point(lambda v: int(v * alpha)))
    base.alpha_composite(layer)

def portal(base, cx, cy, r, f, alpha=1.0):
    """Swirling teal ring."""
    if r < 4 or alpha <= 0:
        return
    layer = Image.new('RGBA', base.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.ellipse((cx - r, cy - r * 1.35, cx + r, cy + r * 1.35), fill=(3, 18, 20, 255))
    for k in range(9):                                       # swirl arcs
        rr = r * (0.25 + 0.085 * k)
        start = (f * (14 + k * 3) + k * 40) % 360
        d.arc((cx - rr, cy - rr * 1.35, cx + rr, cy + rr * 1.35), start, start + 200,
              fill=(TEAL_G if k % 2 else TEAL_L) + (int(90 + 15 * k),), width=max(3, int(r * 0.03)))
    d.ellipse((cx - r, cy - r * 1.35, cx + r, cy + r * 1.35), outline=TEAL_L + (255,), width=max(4, int(r * 0.05)))
    glow = layer.filter(ImageFilter.GaussianBlur(26))
    if alpha < 1:
        glow.putalpha(glow.getchannel('A').point(lambda v: int(v * alpha)))
        layer.putalpha(layer.getchannel('A').point(lambda v: int(v * alpha)))
    base.alpha_composite(glow)
    base.alpha_composite(layer)

DUST = [(random.uniform(0, W), random.uniform(0, H), random.uniform(1, 3), random.uniform(0.2, 0.8)) for _ in range(90)]

def background(f, dust_level=1.0):
    base = Image.new('RGBA', (W, H), (4, 6, 8, 255))
    d = ImageDraw.Draw(base)
    d.rectangle((0, 1420, W, 1470), fill=(30, 26, 22, 255))               # shelf top
    d.rectangle((0, 1470, W, 1540), fill=(18, 16, 14, 255))               # shelf front
    d.rounded_rectangle((W // 2 - 70, 1484, W // 2 + 70, 1526), radius=6, fill=(46, 42, 36, 255))  # label
    lw = d.textlength('nikel', font=F_LABEL)
    d.text((W // 2 - lw / 2, 1488), 'nikel', font=F_LABEL, fill=(150, 150, 140, 255))
    if dust_level > 0:                                                     # floating dust in the light
        for (x, y, r, s) in DUST:
            yy = (y + f * s * 3) % H
            a = int(110 * dust_level)
            d.ellipse((x - r, yy - r, x + r, yy + r), fill=(150, 140, 120, a))
    return base

# ---------------------------------------------------------------- timeline
# part0 frames (15 fps):   0-19 dusty phone | 20-39 robot beams in | 40-64 cleaning
# 65-94 speech | 95-124 portal + carry | 125-144 flash + 2026
P0 = 145
P1 = 30
PHONE_X, PHONE_Y = W // 2 - PH_W // 2 + 120, 1420 - PH_H        # phone stands on the shelf
ROB_X, ROB_Y = 90, 1420 - ROBOT.height

def frame_part0(f):
    clean = ease(seg(f, 40, 64))
    base = background(f, dust_level=1.0 - 0.6 * clean)
    portal_t = seg(f, 95, 108)
    carry = ease(seg(f, 104, 124))

    # portal (behind everything else that moves into it)
    pcx, pcy = W // 2, 860
    if f >= 95:
        portal(base, pcx, pcy, lerp(10, 300, ease(portal_t)) * (1 - 0.15 * seg(f, 120, 124)), f)

    # phone position: on shelf, then carried up into the portal
    ph = PHONE_ON if f >= 64 else PHONE_OFF
    scale = lerp(1.0, 0.05, carry ** 1.6)
    px = lerp(PHONE_X, pcx - PH_W / 2 + 40, carry)
    py = lerp(PHONE_Y, pcy - PH_H / 2, carry)
    if scale < 0.999:
        ph = ph.resize((max(2, int(PH_W * scale)), max(2, int(PH_H * scale))), Image.LANCZOS)
        px += (PH_W - ph.width) / 2; py += (PH_H - ph.height) / 2
    if f < 125:
        lay = ph.copy()
        if clean < 1 and scale > 0.999:                       # grime with a cleaning sweep
            g = GRIME.copy()
            if clean > 0:
                m = Image.new('L', g.size, 255)
                sweep = int(lerp(0, g.height + 200, clean))
                ImageDraw.Draw(m).rectangle((0, 0, g.width, sweep), fill=int(255 * (1 - clean) * 0.25))
                from PIL import ImageChops
                g.putalpha(ImageChops.multiply(g.getchannel('A'), m))
            full = Image.new('RGBA', g.size, (0, 0, 0, 0))
            full.alpha_composite(lay, (80, 80))
            full.alpha_composite(g)
            base.alpha_composite(full, (int(px) - 80, int(py) - 80))
        else:
            glow_paste(base, lay, (px, py), blur=18, glow_alpha=0.5 * clean)

    # robot: beam in, clean, talk, then walk to the phone and go with it
    if 20 <= f < 125:
        appear = ease(seg(f, 20, 39))
        rob = ROBOT
        rx, ry = ROB_X, ROB_Y
        if f >= 100:                                            # walks to the phone, then is pulled in
            walk = ease(seg(f, 100, 106))
            rx = lerp(ROB_X, PHONE_X - ROBOT.width + 70, walk)
            if carry > 0:
                s = lerp(1.0, 0.05, carry ** 1.6)
                rob = ROBOT.resize((max(2, int(ROBOT.width * s)), max(2, int(ROBOT.height * s))), Image.LANCZOS)
                tx = lerp(rx, pcx - rob.width / 2 - 60, carry)
                ty = lerp(ROB_Y, pcy - rob.height / 2, carry)
                rx, ry = tx, ty
        if 40 <= f < 64:                                        # scrubbing wobble
            rx += 18 * math.sin(f * 1.3)
        holo = scanline(rob, f)
        if appear < 1:                                          # materialise from a beam
            a = holo.getchannel('A')
            cut = int(holo.height * (1 - appear))
            m = Image.new('L', holo.size, 255); ImageDraw.Draw(m).rectangle((0, 0, holo.width, cut), fill=0)
            from PIL import ImageChops
            holo.putalpha(ImageChops.multiply(a, m))
            beam = Image.new('RGBA', base.size, (0, 0, 0, 0)); bd = ImageDraw.Draw(beam)
            bx = rx + rob.width / 2
            bd.polygon([(bx - 14, 0), (bx + 14, 0), (bx + rob.width / 2, ry + cut), (bx - rob.width / 2, ry + cut)],
                       fill=TEAL_G + (int(90 * (1 - appear)),))
            base.alpha_composite(beam.filter(ImageFilter.GaussianBlur(10)))
        glow_paste(base, holo, (rx, ry), blur=12, glow_alpha=0.8)

    # speech
    bx, by = ROB_X + ROBOT.width / 2 + 40, ROB_Y - 30
    if 65 <= f < 82:
        bubble(base, 'Happy 10th\nbirthday!', (bx, by), ease(seg(f, 65, 69)) * (1 - seg(f, 79, 82)))
    if 81 <= f < 100:
        bubble(base, 'Come on, you\'ve\ngot work to do.', (bx, by), ease(seg(f, 81, 85)) * (1 - seg(f, 97, 100)))

    # flash + year
    if f >= 121:
        fl = seg(f, 121, 126) if f < 126 else 1 - seg(f, 126, 134)
        if fl > 0:
            ov = Image.new('RGBA', base.size, (200, 255, 248, int(255 * fl)))
            base.alpha_composite(ov)
    if f >= 128:
        a = ease(seg(f, 128, 136))
        txt = Image.new('RGBA', base.size, (0, 0, 0, 0))
        centered_text(txt, '2026', F_YEAR, 860, TEAL_L + (255,), glow=TEAL_G + (255,))
        if a < 1:
            txt.putalpha(txt.getchannel('A').point(lambda v: int(v * a)))
        base.alpha_composite(txt)
    return base

def frame_part1(f):
    """Loop: clean phone in front of a calm portal, 2026 above, version below."""
    base = background(0, dust_level=0.0)
    pulse = 0.5 + 0.5 * math.sin(2 * math.pi * f / P1)
    portal(base, W // 2, 900, 330 + 14 * pulse, f, alpha=0.55 + 0.25 * pulse)
    ph = PHONE_ON
    glow_paste(base, ph, (W // 2 - PH_W / 2, 900 - PH_H / 2), blur=22, glow_alpha=0.45 + 0.35 * pulse)
    centered_text(base, '2026', ImageFont.truetype(FONT_B, 120), 330, TEAL_L + (255,), glow=TEAL_G + (int(150 + 100 * pulse),))
    centered_text(base, 'LineageOS 15.1', F_SMALL, 1620, (200, 225, 222, 255))
    return base

# ---------------------------------------------------------------- output
def save(img, path):
    img.convert('RGB').save(path, 'JPEG', quality=86, optimize=True)

if __name__ == '__main__':
    os.makedirs(os.path.join(OUT, 'part0'), exist_ok=True)
    os.makedirs(os.path.join(OUT, 'part1'), exist_ok=True)
    frames0 = range(P0) if not QUICK else (5, 30, 50, 67, 88, 112, 132, 140)
    for f in frames0:
        save(frame_part0(f), os.path.join(OUT, 'part0', '%03d.jpg' % f))
    frames1 = range(P1) if not QUICK else (0, 15)
    for f in frames1:
        save(frame_part1(f), os.path.join(OUT, 'part1', '%03d.jpg' % f))
    with open(os.path.join(OUT, 'desc.txt'), 'w') as fh:
        fh.write('%d %d %d\n' % (W, H, FPS))
        fh.write('p 1 0 part0\n')        # story once (bootanimation may cut it when boot completes)
        fh.write('p 0 0 part1\n')        # loop until boot completes
    print('frames written to', OUT)
