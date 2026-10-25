"""The pause menu's controller picture for the Vita (retail shows its own
controller, Race/game_image controller_info_*.brlyt): the project site's PS
Vita picture (site/pictures/vita.webp, already cut out) with a leader line
from each of the main controls to the label bar where the game writes that
control's action at runtime.

Layout units are the menu canvas (608x456, origin at its centre, y down).
draw() returns the image, its rectangle and the label bars, one per control
named as mkw::ControlButtonNames, or "Stick" for steering.
"""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
PICTURE = ROOT / 'site/pictures/vita.webp'
RECT = (-296, -176, 296, -20)  # the picture's place over the pause buttons
BAR_W, BAR_H = 120, 16
SCALE = 2  # pixels per canvas unit
LINE, WHITE = (70, 120, 255, 255), (255, 255, 255, 255)
# The picture's controls, as fractions of its bounds (left, top, right, bottom = 0..1).
CONTROLS = {
    'Cross': (0.903, 0.428), 'Circle': (0.948, 0.338), 'Square': (0.859, 0.338),
    'L': (0.110, 0.020), 'R': (0.890, 0.020), 'Stick': (0.117, 0.590),
}


def cutout():
    im = Image.open(PICTURE).convert('RGBA')
    im = im.crop(im.getchannel('A').getbbox())
    return im, im.getchannel('A')


def draw():
    x0, y0, x1, y1 = RECT
    img = Image.new('RGBA', ((x1 - x0) * SCALE, (y1 - y0) * SCALE))
    d = ImageDraw.Draw(img)

    def p(x, y):
        return ((x - x0) * SCALE, (y - y0) * SCALE)

    console, alpha = cutout()
    w = 300  # console width in canvas units
    h = round(w * console.height / console.width)
    cx0, cy0 = -w // 2, y0 + 10
    stroke = alpha.resize((w * SCALE + 8, h * SCALE + 8)).point(lambda v: 255 if v > 40 else 0)
    img.paste(Image.new('RGBA', stroke.size, WHITE), (int(p(cx0, cy0)[0]) - 4, int(p(cx0, cy0)[1]) - 4), stroke)
    img.alpha_composite(console.resize((w * SCALE, h * SCALE), Image.Resampling.LANCZOS), tuple(int(v) for v in p(cx0, cy0)))

    # Left column for the left-hand controls, right column for the others.
    right_x, left_x = x1 - BAR_W, x0
    rows = {'L': (left_x, cy0 + 4), 'Stick': (left_x, cy0 + 70),
            'R': (right_x, cy0 + 4), 'Circle': (right_x, cy0 + 34), 'Square': (right_x, cy0 + 56), 'Cross': (right_x, cy0 + 78)}
    bars = []
    for name, (bx, by) in rows.items():
        fx, fy = CONTROLS[name]
        ax, ay = cx0 + fx * w, cy0 + fy * h
        edge = bx if bx > 0 else bx + BAR_W
        d.line([p(ax, ay), p(edge, by + BAR_H / 2)], fill=LINE, width=SCALE * 2)
        d.ellipse([p(ax - 2, ay - 2), p(ax + 2, ay + 2)], fill=LINE)
        d.rounded_rectangle([p(bx, by), p(bx + BAR_W, by + BAR_H)], radius=4 * SCALE, fill=(30, 70, 200, 235), outline=WHITE, width=SCALE)
        d.rounded_rectangle([p(bx + 2, by + 2), p(bx + BAR_W - 2, by + BAR_H / 2)], radius=3 * SCALE, fill=(90, 150, 255, 120))
        bars.append((name, bx, by, BAR_W, BAR_H))
    return img, list(RECT), bars


if __name__ == '__main__':
    import sys
    image, _, _ = draw()
    image.save(sys.argv[1] if len(sys.argv) > 1 else 'vita_controls.png')
