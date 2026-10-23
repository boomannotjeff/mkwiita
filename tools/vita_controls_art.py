"""The pause menu's controller picture for the Vita (retail shows its own
controller, Race/game_image controller_info_*.brlyt): a photo of a PS Vita,
cut out of its background, with a leader line from each of the main controls
to the label bar where the game writes that control's action at runtime.

Photo: assets/vita-controls/vita_front.jpg, "Sony PlayStation Vita Slim
PCH-2000 (black) - front view" by ITEagle Europe (Sebastiaan Broekhoven),
CC BY-SA 4.0, Wikimedia Commons; see licenses/vita-photo.txt. Cut out and
outlined here.

Layout units are the menu canvas (608x456, origin at its centre, y down).
draw() returns the image, its rectangle and the label bars, one per control
named as mkw::ControlButtonNames, or "Stick" for steering.
"""
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
PHOTO = ROOT / 'assets/vita-controls/vita_front.jpg'
RECT = (-296, -176, 296, -20)  # the picture's place over the pause buttons
BAR_W, BAR_H = 120, 16
SCALE = 2  # pixels per canvas unit
LINE, WHITE = (70, 120, 255, 255), (255, 255, 255, 255)
# The photo's console, as fractions of its cut-out (left, top, right, bottom = 0..1).
CONTROLS = {
    'Cross': (0.910, 0.430), 'Circle': (0.948, 0.300), 'Square': (0.857, 0.300),
    'L': (0.110, 0.020), 'R': (0.890, 0.020), 'Stick': (0.100, 0.540),
}


def cutout():
    im = Image.open(PHOTO).convert('RGB')
    a = np.asarray(im, dtype=np.int32)
    # The console is near black; the cardboard is light brown.
    dark = a.max(axis=2) < 70
    mask = Image.fromarray(np.uint8(dark) * 255).filter(ImageFilter.MedianFilter(9))
    # Fill the buttons and screen reflections inside the outline: row-wise span fill.
    m = np.asarray(mask) > 0
    ys, xs = np.nonzero(m)
    filled = np.zeros_like(m)
    for y in range(ys.min(), ys.max() + 1):
        row = np.nonzero(m[y])[0]
        if len(row) > 50:
            filled[y, row.min():row.max() + 1] = True
    box = (xs.min(), ys.min(), xs.max() + 1, ys.max() + 1)
    alpha = Image.fromarray(np.uint8(filled) * 255).filter(ImageFilter.GaussianBlur(2))
    # Lift the black body so it reads on the dark pause menu; outlined in white by draw().
    lifted = Image.eval(im, lambda v: min(255, int(v * 1.6 + 25))).convert('RGBA')
    lifted.putalpha(alpha)
    return lifted.crop(box), alpha.crop(box)


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
