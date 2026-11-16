#!/usr/bin/env python3
"""Regenerate the README screenshots from a real, isolated OBS.

Uses the same isolated harness as acceptance_test.py (its own OBS home and
websocket port; never touches your real OBS). Program shows the
README banner (docs/images/vexel-banner.png) scaled to fill the canvas.

Needs Pillow (pip install pillow). Writes to docs/images/:
  vexel-mark.png      the Vexel mark, cut from docs/images/vexel-banner.png
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

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import acceptance_test as at  # noqa: E402
import obsws  # noqa: E402

R = at.R
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "images")
CW, CH = 3840, 1080


def banner_mark(path):
    """Cut the Vexel mark out of the README banner, keying out the dark background."""
    im = Image.open(os.path.join(OUT, "vexel-banner.png")).convert("RGB").crop((316, 4, 538, 134))
    out = Image.new("RGBA", im.size)
    src, dst = im.load(), out.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b = src[x, y]
            a = max(0.0, min(1.0, (max(r, g, b) - 60) / 110))
            if a <= 0:
                dst[x, y] = (0, 0, 0, 0)
                continue
            dst[x, y] = (min(255, int(r / a)), min(255, int(g / a)), min(255, int(b / a)), int(a * 255))
    out.save(path)
    return path


def program_graphic(path):
    """The README banner, scaled to fill the 3840x1080 Program."""
    im = Image.open(os.path.join(OUT, "vexel-banner.png")).convert("RGB")
    scale = max(CW / im.width, CH / im.height)
    im = im.resize((round(im.width * scale), round(im.height * scale)), Image.LANCZOS)
    x, y = (im.width - CW) // 2, (im.height - CH) // 2
    im.crop((x, y, x + CW, y + CH)).save(path)


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
    mark = banner_mark(os.path.join(work, "mark.png"))
    Image.open(mark).save(os.path.join(OUT, "vexel-mark.png"), optimize=True)

    gfx = os.path.join(work, "program.png")
    program_graphic(gfx)
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
