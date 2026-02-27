"""
Generate Mcaster1DSPEncoder_Qt.ico from the app-icon.svg design.
Uses Pillow with 4x supersampling for smooth antialiasing.
Outputs: resources/Mcaster1DSPEncoder_Qt.ico  (16,32,48,64,128,256 px)
"""
import math
from PIL import Image, ImageDraw
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_ICO = os.path.join(SCRIPT_DIR, "resources", "Mcaster1DSPEncoder_Qt.ico")

# ── Colors (matching app-icon.svg) ──────────────────────────────────────────
BG_TOP    = (26,  58,  92, 255)   # #1a3a5c
BG_BOT    = (15,  34,  64, 255)   # #0f2240
TEAL      = (0,  212, 170, 255)   # #00d4aa
TEAL_DIM  = (0,  212, 170, 153)   # #00d4aa 60% opacity
BLUE_ACC  = (0,  136, 204, 255)   # #0088cc
TRANSPARENT = (0, 0, 0, 0)

def lerp_color(a, b, t):
    return tuple(int(a[i] + (b[i]-a[i])*t) for i in range(4))

def draw_rounded_rect(draw, xy, radius, fill):
    """Draw antialiased rounded rectangle."""
    x0, y0, x1, y1 = xy
    r = radius
    draw.rectangle([x0+r, y0, x1-r, y1], fill=fill)
    draw.rectangle([x0, y0+r, x1, y1-r], fill=fill)
    draw.ellipse([x0, y0, x0+2*r, y0+2*r], fill=fill)
    draw.ellipse([x1-2*r, y0, x1, y0+2*r], fill=fill)
    draw.ellipse([x0, y1-2*r, x0+2*r, y1], fill=fill)
    draw.ellipse([x1-2*r, y1-2*r, x1, y1], fill=fill)

def draw_arc_stroke(img, cx, cy, rx, ry, start_deg, end_deg, color, width):
    """Draw a stroked arc by compositing thin ellipses."""
    draw = ImageDraw.Draw(img)
    # Approximate arc as a series of line segments
    steps = 120
    pts = []
    for i in range(steps+1):
        t = start_deg + (end_deg - start_deg) * i / steps
        rad = math.radians(t)
        x = cx + rx * math.cos(rad)
        y = cy + ry * math.sin(rad)
        pts.append((x, y))
    for i in range(len(pts)-1):
        x0, y0 = pts[i]
        x1, y1 = pts[i+1]
        draw.line([x0, y0, x1, y1], fill=color, width=width)

def render_icon(size):
    """Render the icon at `size` x `size` using 4x supersampling."""
    S = size * 4   # supersample size
    img = Image.new("RGBA", (S, S), TRANSPARENT)
    draw = ImageDraw.Draw(img)

    # Scale factor: SVG is 128x128 viewBox, we render at S
    scale = S / 128.0
    cx = S / 2.0
    cy = S / 2.0

    # ── Background rounded rect ──────────────────────────────────────────────
    pad = int(0 * scale)
    radius = int(24 * scale)
    # Vertical gradient: split into horizontal bands
    for row in range(pad, S - pad):
        t = (row - pad) / (S - 2*pad)
        c = lerp_color(BG_TOP, BG_BOT, t)
        draw.line([(pad, row), (S-pad, row)], fill=c)
    # Mask to rounded rect shape
    mask = Image.new("L", (S, S), 0)
    md = ImageDraw.Draw(mask)
    draw_rounded_rect(md, [pad, pad, S-pad, S-pad], radius, 255)
    # Apply mask
    r_bg = Image.new("RGBA", (S, S), TRANSPARENT)
    r_bg.paste(img, mask=mask)
    img = r_bg
    draw = ImageDraw.Draw(img)

    # ── Broadcast waves (3 arcs, SVG: start=180 sweep upward) ───────────────
    # SVG paths approximate arcs centered at (64, 64) opening upward
    wave_defs = [
        (16 * scale, 20 * scale),   # inner arc: rx=16, half-height=20
        (26 * scale, 30 * scale),   # middle arc
        (36 * scale, 40 * scale),   # outer arc
    ]
    wave_w = max(1, int(3 * scale))
    for rx, ry in wave_defs:
        # Arc from 200° to 340° (top half of ellipse, opening upward)
        draw_arc_stroke(img, cx, cy, rx, -ry, 200, 340,
                        (*TEAL[:3], 153), wave_w)

    # ── Microphone body ──────────────────────────────────────────────────────
    # SVG: rect x=56 y=52 w=16 h=28 rx=8 fill=teal
    mx0 = int((56) * scale)
    my0 = int((52) * scale)
    mx1 = int((72) * scale)
    my1 = int((80) * scale)
    mr  = int(8 * scale)
    draw_rounded_rect(draw, [mx0, my0, mx1, my1], mr, TEAL)

    # ── Mic stand (vertical line below mic body) ─────────────────────────────
    stand_x = int(64 * scale)
    stand_y0 = my1
    stand_y1 = int(92 * scale)
    stand_w = max(1, int(3 * scale))
    draw.line([(stand_x, stand_y0), (stand_x, stand_y1)],
              fill=TEAL, width=stand_w)

    # ── Mic base (horizontal line) ───────────────────────────────────────────
    base_x0 = int(52 * scale)
    base_x1 = int(76 * scale)
    base_y  = int(92 * scale)
    base_w  = max(1, int(3 * scale))
    draw.line([(base_x0, base_y), (base_x1, base_y)],
              fill=TEAL, width=base_w)

    # ── Downscale with LANCZOS ───────────────────────────────────────────────
    return img.resize((size, size), Image.LANCZOS)

def main():
    sizes = [256, 128, 64, 48, 32, 16]
    frames = [render_icon(s) for s in sizes]
    os.makedirs(os.path.dirname(OUT_ICO), exist_ok=True)
    # Pillow saves multi-size ICO when append_images is provided
    frames[0].save(
        OUT_ICO,
        format="ICO",
        sizes=[(s, s) for s in sizes],
        append_images=frames[1:],
    )
    print(f"Written: {OUT_ICO}  ({os.path.getsize(OUT_ICO)//1024} KB)")
    for s, f in zip(sizes, frames):
        print(f"  {s:3d}x{s:<3d}  mode={f.mode}")

if __name__ == "__main__":
    main()
