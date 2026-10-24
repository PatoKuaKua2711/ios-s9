#!/usr/bin/env python3
"""Generate the ios-s9 default wallpaper (original artwork, MIT like the rest of s9/).

  make-wallpaper.py OUT.jpg [--width 828 --height 1792]

Dark violet background with large soft glass orbs lit from one side, drawn
procedurally (numpy + Pillow) at 2x and downsampled; a little noise keeps
the gradients from banding on the AMOLED panel. 828x1792 is the @2x canvas
the S9 presents to iOS (iPhone XR screen), so the S9Wallpaper tweak shows
it 1:1. A small "Galaxy S9" mark sits low on the right.
"""
import argparse
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont


def orb(h, w, cx, cy, r, color, light, rim=0.10, alpha=0.92):
    """RGBA float layer: a disc with a soft edge, lit from direction `light`."""
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    dx, dy = (x - cx) / r, (y - cy) / r
    d = np.sqrt(dx * dx + dy * dy)
    inside = np.clip((1.0 - d) / 0.012, 0, 1)                   # anti-aliased edge
    lx, ly = light
    shade = np.clip(0.5 + 0.5 * (dx * lx + dy * ly) / np.maximum(d, 1e-3) * np.minimum(d, 1.0), 0, 1)
    body = 0.35 + 0.65 * shade ** 1.6                            # dark core, lit side
    edge = np.exp(-((1.0 - d) / rim) ** 2) * (0.6 + 0.4 * shade)  # thin bright rim
    c = np.array(color, np.float32)[None, None, :] / 255.0
    rgb = c * body[..., None] + edge[..., None] * (0.55 * c + 0.45)
    a = inside * alpha
    return np.dstack([rgb, a])


def over(dst, layer):
    a = layer[..., 3:4]
    dst[..., :3] = layer[..., :3] * a + dst[..., :3] * (1 - a)
    return dst


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--width', type=int, default=828)
    ap.add_argument('--height', type=int, default=1792)
    a = ap.parse_args()
    S = 2
    W, H = a.width * S, a.height * S
    y = np.linspace(0, 1, H, dtype=np.float32)[:, None]
    top, bottom = np.array([10, 6, 22]) / 255.0, np.array([22, 10, 44]) / 255.0
    img = np.ones((H, W, 3), np.float32) * (top + (bottom - top) * y[..., None])
    # Orbs, back to front (positions in canvas fractions, radius in widths)
    for fx, fy, fr, col, light in [
        (1.05, 0.18, 0.85, (64, 28, 120), (-0.8, 0.6)),
        (-0.10, 0.46, 0.70, (92, 40, 170), (0.9, -0.3)),
        (0.88, 0.70, 0.62, (120, 62, 214), (-0.7, -0.7)),
        (0.18, 0.95, 0.78, (150, 92, 236), (0.6, -0.8)),
        (0.62, 0.36, 0.26, (176, 128, 250), (-0.5, -0.85)),
    ]:
        img = over(img, orb(H, W, fx * W, fy * H, fr * W, col, light))
    # soft violet glow behind the middle, and a gentle vignette
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    g = np.exp(-(((xx - 0.55 * W) / (0.55 * W)) ** 2 + ((yy - 0.55 * H) / (0.35 * H)) ** 2))
    img += g[..., None] * np.array([0.10, 0.04, 0.18], np.float32)
    v = 1.0 - 0.35 * (((xx / W - 0.5) * 1.6) ** 2 + ((yy / H - 0.5) * 1.2) ** 2)
    img *= np.clip(v, 0.55, 1.0)[..., None]
    pil = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2))
    pil = pil.resize((a.width, a.height), Image.LANCZOS)
    # mark
    d = ImageDraw.Draw(pil)
    font = None
    for name in ('NotoSans-Medium.ttf', '/usr/share/fonts/noto/NotoSans-Medium.ttf', 'DejaVuSans.ttf',
                 '/usr/share/fonts/TTF/DejaVuSans.ttf'):
        try:
            font = ImageFont.truetype(name, 26)
            break
        except OSError:
            pass
    if font is not None:
        pos = (a.width - 40, a.height - 300)
        shadow = Image.new('L', pil.size, 0)
        ImageDraw.Draw(shadow).text(pos, 'Galaxy S9', fill=200, font=font, anchor='rs')
        shadow = shadow.filter(ImageFilter.GaussianBlur(6))
        pil.paste(Image.new('RGB', pil.size, (12, 4, 28)), (0, 0), shadow)
        d.text(pos, 'Galaxy S9', fill=(226, 210, 255), font=font, anchor='rs')
    arr = np.asarray(pil).astype(np.float32)
    arr += np.random.default_rng(9).normal(0, 1.2, arr.shape)   # dither
    Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8)).save(a.out, quality=93, optimize=True)


if __name__ == '__main__':
    main()
