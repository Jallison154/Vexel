#!/usr/bin/env python3
"""Regenerate the README screenshots from a real, isolated OBS.

Uses the same isolated harness as acceptance_test.py (its own OBS home and
websocket port; never touches your real OBS). Program shows a generated
Vexel graphic, so the images contain no third-party footage.

Needs Pillow (pip install pillow). Writes to docs/images/:
  vexel-mark.png      the Vexel mark, as drawn by the plugin
  shot-editor.png     editor: a 3840x1080 Program split across two LED walls
  shot-projection.png editor: corner pin + soft edge on a blended projector
  shot-outputs.png    what the two blended projectors actually receive
  shot-pattern.png    per-slice test pattern on an output
  shot-dock.png       the Vexel dock

Usage:
  python3 tools/readme_shots.py --obs-app ~/Library/Caches/obs-output-mapping/OBS.app --work /tmp/omshots
"""

import argparse
import os
import secrets
import sys
import time

from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import acceptance_test as at  # noqa: E402
import obsws  # noqa: E402

R = at.R
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "images")
CW, CH = 3840, 1080
CYAN, VIOLET = (25, 211, 245), (139, 92, 246)


def font(size, heavy=True):
    for path, idx in (("/System/Library/Fonts/Avenir Next.ttc", 8 if heavy else 5),
                      ("/System/Library/Fonts/Supplemental/Futura.ttc", 2)):
        try:
            return ImageFont.truetype(path, size, index=idx)
        except OSError:
            continue
    return ImageFont.load_default()


def spaced(draw, xy, text, fnt, fill, spacing, anchor_center=True):
    widths = [draw.textlength(c, font=fnt) for c in text]
    total = sum(widths) + spacing * (len(text) - 1)
    x, y = xy
    if anchor_center:
        x -= total / 2
    for c, w in zip(text, widths):
        draw.text((x, y), c, font=fnt, fill=fill)
        x += w + spacing
    return total


def program_graphic(mark_path, path):
    """A 3840x1080 show graphic in the Vexel style."""
    img = Image.new("RGB", (CW, CH))
    px = img.load()
    for y in range(CH):
        t = y / (CH - 1)
        for x in range(0, CW):
            u = x / (CW - 1)
            c = 1 - abs(u - 0.5) * 2
            r = int(4 + 8 * c + 6 * t)
            g = int(8 + 16 * c * (1 - t))
            b = int(20 + 36 * c + 18 * t)
            px[x, y] = (r, g, b)
    glow = Image.new("RGB", (CW, CH))
    gd = ImageDraw.Draw(glow)
    # Screen panels receding left and right, like the Vexel banner.
    for side in (-1, 1):
        for i in range(5):
            w = 300 - i * 40
            h = 380 - i * 50
            cx = CW / 2 + side * (620 + i * 330)
            top = 330 + i * 25
            col = tuple(int(c * (0.55 - i * 0.08)) for c in (40, 120, 255))
            gd.rounded_rectangle([cx - w / 2, top, cx + w / 2, top + h], 10, fill=col)
            gd.rounded_rectangle([cx - w / 2, top, cx + w / 2, top + h], 10, outline=CYAN, width=3)
    # Floor grid.
    for i in range(-24, 25):
        gd.line([(CW / 2 + i * 40, 760), (CW / 2 + i * 420, CH)], fill=(20, 70, 120), width=2)
    for j, y in enumerate((790, 830, 880, 945, 1030)):
        gd.line([(0, y), (CW, y)], fill=(20, 70, 120), width=2)
    gd.line([(0, 760), (CW, 760)], fill=CYAN, width=4)
    img = Image.blend(img, glow, 0.55)
    img = Image.blend(img, glow.filter(ImageFilter.GaussianBlur(28)), 0.25)

    d = ImageDraw.Draw(img)
    mark = Image.open(mark_path).convert("RGBA").resize((520, 520), Image.LANCZOS)
    halo = Image.new("RGBA", (CW, CH), (0, 0, 0, 0))
    halo.paste(mark, (CW // 2 - 260, 90), mark)
    halo = halo.filter(ImageFilter.GaussianBlur(40))
    img.paste(halo, (0, 0), halo)
    img.paste(mark, (CW // 2 - 260, 90), mark)
    spaced(d, (CW / 2, 610), "VEXEL", font(190), (245, 248, 255), 46)
    sub = font(52, heavy=False)
    w = spaced(d, (CW / 2, 835), "OBS OUTPUT MAPPING", sub, (225, 235, 250), 22)
    d.line([(CW / 2 - w / 2 - 260, 870), (CW / 2 - w / 2 - 60, 870)], fill=CYAN, width=8)
    d.line([(CW / 2 + w / 2 + 60, 870), (CW / 2 + w / 2 + 260, 870)], fill=VIOLET, width=8)
    img.save(path)


def to_png(src, dst, width=None):
    im = Image.open(src).convert("RGB")
    if width and im.width > width:
        im = im.resize((width, round(im.height * width / im.width)), Image.LANCZOS)
    im.save(dst, optimize=True)
    return dst


def walls_layout():
    return {"format": "obs-output-mapping", "version": 1, "restoreOutputs": False, "excludeFromCapture": True,
            "displays": [
                {"id": "wl", "name": "LED Wall Left", "kind": "virtual", "width": 1920, "height": 1080,
                 "slices": [{"id": "l", "name": "Left half", "color": "#19D3F5",
                             "src": R(0, 0, 1920, 1080), "dst": R(0, 0, 1920, 1080)}]},
                {"id": "wr", "name": "LED Wall Right", "kind": "virtual", "width": 1920, "height": 1080,
                 "slices": [{"id": "r", "name": "Right half", "color": "#8B5CF6",
                             "src": R(1920, 0, 1920, 1080), "dst": R(0, 0, 1920, 1080)}]}]}


OVERLAP = 384  # Program px shared by the two projectors


def projector_layout():
    sw = (CW + OVERLAP) // 2
    return {"format": "obs-output-mapping", "version": 1, "restoreOutputs": False, "excludeFromCapture": True,
            "displays": [
                {"id": "pl", "name": "Projector L", "kind": "virtual", "width": 1920, "height": 1080,
                 "slices": [{"id": "pls", "name": "Left blend", "color": "#19D3F5",
                             "src": R(0, 0, sw, CH), "dst": R(0, 0, 1920, 1080)}]},
                {"id": "pr", "name": "Projector R", "kind": "virtual", "width": 1920, "height": 1080,
                 "slices": [{"id": "prs", "name": "Right blend", "color": "#8B5CF6",
                             "src": R(CW - sw, 0, sw, CH), "dst": R(0, 0, 1920, 1080)}]}]}


def shoot(ws, work):
    r = ws.vendor("LogoCapture", {"path": os.path.join(work, "mark.png"), "size": 1024})
    if not r.get("ok"):
        raise RuntimeError(f"LogoCapture: {r}")
    to_png(os.path.join(work, "mark.png"), os.path.join(OUT, "vexel-mark.png"), 256)

    gfx = os.path.join(work, "program.png")
    program_graphic(os.path.join(work, "mark.png"), gfx)
    ws.call("SetVideoSettings", {"baseWidth": CW, "baseHeight": CH, "outputWidth": 1920,
                                 "outputHeight": 540, "fpsNumerator": 30, "fpsDenominator": 1})
    scenes = [s["sceneName"] for s in ws.call("GetSceneList")["scenes"]]
    if "Show" not in scenes:
        ws.call("CreateScene", {"sceneName": "Show"})
        ws.call("CreateInput", {"sceneName": "Show", "inputName": "Show graphic",
                                "inputKind": "image_source", "inputSettings": {"file": gfx}})
    ws.call("SetCurrentSceneTransition", {"transitionName": "Cut"})
    ws.call("SetCurrentProgramScene", {"sceneName": "Show"})
    time.sleep(1.5)

    # 1. Two LED walls.
    ws.vendor("SetState", {"state": walls_layout()})
    ws.vendor("OpenEditor", {"width": 1600, "height": 920})
    ws.vendor("Select", {"sliceName": "Left half"})
    time.sleep(1.5)
    ws.vendor("EditorCapture", {"path": os.path.join(work, "editor.png")})
    to_png(os.path.join(work, "editor.png"), os.path.join(OUT, "shot-editor.png"), 1600)

    # 2. Test pattern on an output, drawn per slice.
    ws.vendor("SetIdentify", {"displayName": "LED Wall Left", "mode": "pattern"})
    time.sleep(0.8)
    at.snapshot(ws, "LED Wall Left", os.path.join(work, "pattern.ppm"))
    to_png(os.path.join(work, "pattern.ppm"), os.path.join(OUT, "shot-pattern.png"), 960)
    ws.vendor("SetIdentify", {"mode": "clear"})

    # 3. Two blended projectors, the left one corner-pinned.
    ws.vendor("SetState", {"state": projector_layout()})
    feather = round(OVERLAP * 1920 / ((CW + OVERLAP) // 2))
    ws.vendor("SetSliceFeather", {"sliceName": "Left blend", "right": feather})
    ws.vendor("SetSliceFeather", {"sliceName": "Right blend", "left": feather})
    pts = [{"x": 0.05, "y": 0.08}, {"x": 0.97, "y": 0.02}, {"x": 0.0, "y": 0.95}, {"x": 1.0, "y": 1.0}]
    ws.vendor("SetSliceWarp", {"sliceName": "Left blend", "cols": 2, "rows": 2, "points": pts})
    ws.vendor("SetSliceWarp", {"sliceName": "Right blend", "cols": 3, "rows": 3, "points": [
        {"x": 0.0, "y": 0.02}, {"x": 0.5, "y": 0.0}, {"x": 0.96, "y": 0.06},
        {"x": 0.0, "y": 0.5}, {"x": 0.52, "y": 0.52}, {"x": 0.98, "y": 0.5},
        {"x": 0.0, "y": 1.0}, {"x": 0.5, "y": 0.97}, {"x": 0.95, "y": 0.93}]})
    ws.vendor("Select", {"sliceName": "Left blend"})
    ws.vendor("SelectWarpPoint", {"sliceName": "Left blend", "index": 1})
    time.sleep(1.5)
    ws.vendor("EditorCapture", {"path": os.path.join(work, "projection.png")})
    to_png(os.path.join(work, "projection.png"), os.path.join(OUT, "shot-projection.png"), 1600)

    a = Image.open(at.snapshot(ws, "Projector L", os.path.join(work, "proj_l.ppm"))).convert("RGB")
    b = Image.open(at.snapshot(ws, "Projector R", os.path.join(work, "proj_r.ppm"))).convert("RGB")
    gap = 24
    pair = Image.new("RGB", (a.width + b.width + gap, a.height), (13, 17, 23))
    pair.paste(a, (0, 0))
    pair.paste(b, (a.width + gap, 0))
    pair.save(os.path.join(work, "outputs.png"))
    to_png(os.path.join(work, "outputs.png"), os.path.join(OUT, "shot-outputs.png"), 1600)

    # 4. Dock, with the walls layout so it shows two displays.
    ws.vendor("CloseEditor")
    ws.vendor("SetState", {"state": walls_layout()})
    ws.vendor("OpenWindow", {"displayName": "LED Wall Left"})
    time.sleep(1.0)
    ws.vendor("DockCapture", {"path": os.path.join(work, "dock.png"), "width": 380, "height": 640})
    to_png(os.path.join(work, "dock.png"), os.path.join(OUT, "shot-dock.png"), 380)
    ws.vendor("StopAll")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--obs-app", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--port", type=int, default=at.DEFAULT_PORT)
    ap.add_argument("--plugin", default=at.DEFAULT_PLUGIN)
    args = ap.parse_args()
    os.makedirs(args.work, exist_ok=True)
    at.set_test_home(os.path.join(args.work, "obs-home"))
    at.install_plugin(args.plugin)
    at.reset_plugin_state()
    password = secrets.token_urlsafe(24)
    at.seed_config(args.port, password)
    proc = at.launch_obs(args.obs_app, os.path.join(args.work, "obs-stdout.log"))
    try:
        ws = obsws.connect(port=args.port, password=password, retries=300)
        shoot(ws, args.work)
        ws.close()
    finally:
        at.quit_obs(proc)
    print("Wrote screenshots to", OUT)


if __name__ == "__main__":
    main()
