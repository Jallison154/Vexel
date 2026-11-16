#!/usr/bin/env python3
"""End-to-end test of projection mapping (corner pin, mesh warp, soft edges).

Runs a real, isolated OBS (same harness as acceptance_test.py). Program shows
a smooth gradient (R follows x, G follows y), so the colour of every output
pixel tells exactly which Program point it came from. Expected colours are
computed here independently (Python homography / blend curve) and compared
with Snapshot, which renders through the same GPU path as the outputs.

Checks: identity warps stay pixel-exact; corner pin geometry and perspective;
outside the shape is black; mesh warp through its control points; dragging
a point in the editor; arrow-key nudges (1 / 10 px), Tab and Esc; the warp
follows when the slice moves; undo; soft-edge ramp; warped test pattern;
persistence across a restart.

Usage:
  python3 tools/warp_test.py --obs-app ~/Library/Caches/obs-output-mapping/OBS.app --work /tmp/omwarp
"""

import argparse
import json
import os
import secrets
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import acceptance_test as at  # noqa: E402
import obsws  # noqa: E402
from testpattern import write_png  # noqa: E402

R, check, log = at.R, at.check, at.log
W, H = 1920, 1080
KEY_LEFT, KEY_UP, KEY_RIGHT, KEY_DOWN = 0x01000012, 0x01000013, 0x01000014, 0x01000015
KEY_TAB, KEY_ESC = 0x01000001, 0x01000000


def grad(x, y):
    """Program gradient (3840x1080 canvas; the slice uses the left half)."""
    return (round(x * 255 / 3839), round(y * 255 / 1079), 128)


def srgb_to_lin(c):
    c /= 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def lin_to_srgb(v):
    v = max(0.0, min(1.0, v))
    return 255.0 * (v * 12.92 if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055)


def square_to_quad(p00, p10, p01, p11):
    sx = p00[0] - p10[0] + p11[0] - p01[0]
    sy = p00[1] - p10[1] + p11[1] - p01[1]
    dx1, dx2 = p10[0] - p11[0], p01[0] - p11[0]
    dy1, dy2 = p10[1] - p11[1], p01[1] - p11[1]
    den = dx1 * dy2 - dx2 * dy1
    g = (sx * dy2 - dx2 * sy) / den
    h = (dx1 * sy - sx * dy1) / den
    a = p10[0] - p00[0] + g * p10[0]
    b = p01[0] - p00[0] + h * p01[0]
    d = p10[1] - p00[1] + g * p10[1]
    e = p01[1] - p00[1] + h * p01[1]
    return (a, b, p00[0], d, e, p00[1], g, h)


def hmap(H_, u, v):
    a, b, c, d, e, f, g, h = H_
    w = g * u + h * v + 1
    return ((a * u + b * v + c) / w, (d * u + e * v + f) / w)


def hinv(H_, x, y):
    """Inverse of the square->quad map (solve the 2x2 system)."""
    a, b, c, d, e, f, g, h = H_
    # x(gu+hv+1) = au+bv+c ; y(...) = du+ev+f
    m11, m12, r1 = a - g * x, b - h * x, x - c
    m21, m22, r2 = d - g * y, e - h * y, y - f
    det = m11 * m22 - m12 * m21
    return ((r1 * m22 - m12 * r2) / det, (m11 * r2 - m21 * r1) / det)


def snap(ws, work, name):
    path = at.snapshot(ws, "PM", os.path.join(work, name + ".ppm"))
    w, h, data = at.read_ppm(path)
    return lambda x, y: tuple(data[(y * w + x) * 3:(y * w + x) * 3 + 3])


def diff(a, b):
    return max(abs(x - y) for x, y in zip(a, b))


def slice_of(st):
    return st["displays"][0]["slices"][0]


def warp_px(st, i):
    s = slice_of(st)
    p, d = s["warp"]["points"][i], s["dst"]
    return (d["x"] + p["x"] * d["w"], d["y"] + p["y"] * d["h"])


def layout():
    return {"format": "obs-output-mapping", "version": 1, "restoreOutputs": False, "excludeFromCapture": True,
            "displays": [{"id": "pm", "name": "PM", "kind": "virtual", "width": W, "height": H,
                          "slices": [{"id": "s", "name": "Wall", "color": "#19D3F5",
                                      "src": R(0, 0, W, H), "dst": R(0, 0, W, H)}]}]}


def phase(ws, work):
    gpath = os.path.join(work, "gradient.png")
    if not os.path.exists(gpath):
        write_png(gpath, 3840, 1080, grad)
    scenes = [s["sceneName"] for s in ws.call("GetSceneList")["scenes"]]
    if "Gradient" not in scenes:
        ws.call("CreateScene", {"sceneName": "Gradient"})
        ws.call("CreateInput", {"sceneName": "Gradient", "inputName": "Gradient image",
                                "inputKind": "image_source", "inputSettings": {"file": gpath}})
    ws.call("SetCurrentSceneTransition", {"transitionName": "Cut"})
    ws.call("SetCurrentProgramScene", {"sceneName": "Gradient"})
    time.sleep(1.0)
    r = ws.vendor("SetState", {"state": layout()})
    check("Layout loaded", r.get("ok"), r.get("error", ""))

    px = snap(ws, work, "plain")
    bad = sum(1 for y in range(0, H, 7) for x in range(0, W, 7) if diff(px(x, y), grad(x, y)) > 1)
    check("Plain slice is pixel-exact", bad == 0, f"mismatches={bad}")

    # Identity warps (corner pin / mesh with untouched points) stay a plain, exact blit.
    for cols in (2, 5):
        ws.vendor("SetSliceWarp", {"sliceName": "Wall", "cols": cols, "rows": cols})
        px = snap(ws, work, f"identity{cols}")
        bad = sum(1 for y in range(0, H, 7) for x in range(0, W, 7) if diff(px(x, y), grad(x, y)) > 1)
        check(f"Untouched {'corner pin' if cols == 2 else 'mesh 5x5'} is still pixel-exact", bad == 0,
              f"mismatches={bad}")

    # Corner pin.
    q = [(0.10, 0.05), (0.90, 0.00), (0.00, 1.00), (1.00, 0.90)]
    r = ws.vendor("SetSliceWarp", {"sliceName": "Wall", "cols": 2, "rows": 2,
                                   "points": [{"x": x, "y": y} for x, y in q]})
    check("Corner pin set", r.get("ok"), r.get("error", ""))
    st = r["state"]
    check("Corner pin saved in state", slice_of(st).get("warp", {}).get("cols") == 2)
    Hq = square_to_quad(*[(x * W, y * H) for x, y in q])
    px = snap(ws, work, "cornerpin")
    worst, n = 0, 0
    for (u, v) in [(0.5, 0.5), (0.25, 0.25), (0.75, 0.3), (0.2, 0.8), (0.85, 0.85), (0.05, 0.5), (0.5, 0.05)]:
        ox, oy = hmap(Hq, u, v)
        x, y = int(ox), int(oy)
        # expected: the Program point under this output pixel's centre
        uu, vv = hinv(Hq, x + 0.5, y + 0.5)
        want = grad(uu * W - 0.5, vv * H - 0.5)
        worst = max(worst, diff(px(x, y), want))
        n += 1
    check("Corner pin: every sampled pixel shows the right Program point (perspective)", worst <= 3,
          f"worst colour error {worst} over {n} points")
    # True perspective, not a bilinear blend: the centre comes from where the diagonals cross.
    cx, cy = hmap(Hq, 0.5, 0.5)
    lin_cx = sum(x for x, _ in q) / 4 * W
    check("Corner pin is a true perspective (centre ≠ corner average)", abs(cx - lin_cx) > 5,
          f"centre x {cx:.1f} vs average {lin_cx:.1f}")
    check("Outside the warped shape is black", px(960, 5) == (0, 0, 0) and px(1900, 1070) == (0, 0, 0),
          f"{px(960, 5)} {px(1900, 1070)}")

    # Editor: drag the top-left point, then nudge with the arrow keys.
    ws.vendor("OpenEditor", {"width": 1400, "height": 860})
    time.sleep(1.0)
    ws.vendor("Select", {"sliceName": "Wall"})
    time.sleep(0.3)
    tl = warp_px(at.state(ws), 0)
    r = ws.vendor("EditorDrag", {"view": "output", "fromX": tl[0], "fromY": tl[1],
                                 "toX": tl[0] + 60, "toY": tl[1] + 40})
    st = r["state"]
    nt = warp_px(st, 0)
    check("Dragging a corner in Output Placement moves that point",
          abs(nt[0] - (tl[0] + 60)) <= 1 and abs(nt[1] - (tl[1] + 40)) <= 1,
          f"{tl} -> {tuple(round(v, 1) for v in nt)}")
    check("The dragged point becomes the selected point", st.get("selectedWarpPoint") == 0,
          str(st.get("selectedWarpPoint")))
    other = warp_px(st, 3)
    check("Other points stay put", abs(other[0] - W) < 0.01 and abs(other[1] - 0.9 * H) < 0.01, str(other))

    ws.vendor("SelectWarpPoint", {"sliceName": "Wall", "index": 3})
    before = warp_px(at.state(ws), 3)
    time.sleep(0.9)  # a separate undo step (rapid edits coalesce, like holding an arrow key)
    ws.vendor("EditorKey", {"view": "output", "key": KEY_RIGHT})
    ws.vendor("EditorKey", {"view": "output", "key": KEY_RIGHT})
    r = ws.vendor("EditorKey", {"view": "output", "key": KEY_DOWN, "shift": True})
    after = warp_px(r["state"], 3)
    check("Arrow keys nudge the selected point: 1 px, Shift = 10 px",
          abs(after[0] - (round(before[0]) + 2)) < 1e-6 and abs(after[1] - (round(before[1]) + 10)) < 1e-6,
          f"{before} -> {after}")
    check("Arrow keys moved only the point, not the slice", slice_of(r["state"])["dst"] == R(0, 0, W, H),
          str(slice_of(r["state"])["dst"]))
    r = ws.vendor("EditorKey", {"view": "output", "key": KEY_TAB})
    check("Tab steps to the next point (wraps 4 -> 1)", r["state"]["selectedWarpPoint"] == 0,
          str(r["state"]["selectedWarpPoint"]))
    r = ws.vendor("EditorKey", {"view": "output", "key": KEY_ESC})
    check("Esc lets go of the point, slice stays selected",
          r["state"]["selectedWarpPoint"] == -1 and r["state"]["selectedSlice"] == "s",
          f"{r['state']['selectedWarpPoint']} {r['state']['selectedSlice']}")
    time.sleep(0.9)
    r = ws.vendor("EditorKey", {"view": "output", "key": KEY_LEFT})
    check("With no point selected, arrows move the whole slice (warp moves along)",
          slice_of(r["state"])["dst"]["x"] == -1 and abs(warp_px(r["state"], 3)[0] - (after[0] - 1)) < 1e-6,
          f"{slice_of(r['state'])['dst']} {warp_px(r['state'], 3)}")
    ws.vendor("Undo")      # the slice move
    r = ws.vendor("Undo")  # the three point nudges (one step: they came in quick succession)
    p3 = warp_px(r["state"], 3)
    check("Undo restores the point", abs(p3[0] - before[0]) < 1e-6 and abs(p3[1] - before[1]) < 1e-6,
          f"{p3} vs {before}")

    # Moving the slice carries the warp.
    st = at.state(ws)
    p0 = warp_px(st, 0)
    sl = slice_of(st)
    d = dict(sl["dst"])
    d["x"] += 100
    d["y"] += 50
    doc = {k: st[k] for k in ("format", "version", "restoreOutputs", "excludeFromCapture", "displays")}
    doc["displays"][0]["slices"][0]["dst"] = d
    r = ws.vendor("SetState", {"state": doc})
    p0b = warp_px(r["state"], 0)
    check("Moving the slice moves its warp by the same amount",
          abs(p0b[0] - p0[0] - 100) < 1e-6 and abs(p0b[1] - p0[1] - 50) < 1e-6, f"{p0} -> {p0b}")
    ws.vendor("SetState", {"state": layout()})

    # Mesh warp: 3x3 with the centre point pulled; the output passes through it.
    pts = [{"x": c / 2, "y": r_ / 2} for r_ in range(3) for c in range(3)]
    pts[4] = {"x": 0.62, "y": 0.40}
    r = ws.vendor("SetSliceWarp", {"sliceName": "Wall", "cols": 3, "rows": 3, "points": pts})
    check("Mesh 3x3 set", r.get("ok") and slice_of(r["state"])["warp"]["cols"] == 3, r.get("error", ""))
    px = snap(ws, work, "mesh")
    cx, cy = int(0.62 * W), int(0.40 * H)
    check("Mesh: the pulled centre point shows the Program centre",
          diff(px(cx, cy), grad(W / 2, H / 2)) <= 4, f"{px(cx, cy)} vs {grad(W / 2, H / 2)}")
    check("Mesh: corners stay where they were", diff(px(3, 3), grad(3, 3)) <= 4 and
          diff(px(W - 4, H - 4), grad(W - 4, H - 4)) <= 4, f"{px(3, 3)} {px(W - 4, H - 4)}")
    r = ws.vendor("SetSliceWarp", {"sliceName": "Wall", "cols": 2, "rows": 2})
    w2 = slice_of(r["state"])["warp"]
    check("Switching mesh -> corner pin keeps the outer shape",
          w2["cols"] == 2 and all(abs(p["x"] - e[0]) < 1e-6 and abs(p["y"] - e[1]) < 1e-6
                                  for p, e in zip(w2["points"], [(0, 0), (1, 0), (0, 1), (1, 1)])),
          json.dumps(w2["points"]))
    ws.vendor("SetSliceWarp", {"sliceName": "Wall", "cols": 0, "rows": 0})

    # Soft edge: right 200 px. Linear-light S-curve: 0.5 at mid-ramp, 0 at the edge.
    r = ws.vendor("SetSliceFeather", {"sliceName": "Wall", "right": 200, "gamma": 1.0})
    check("Soft edge set", r.get("ok") and slice_of(r["state"]).get("feather", {}).get("right") == 200)
    px = snap(ws, work, "feather")
    y = 500
    mid_x = W - 100  # half-way into the ramp (pixel centre ~ t = 0.4975)
    t = (W - (mid_x + 0.5)) / 200.0
    f = 2 * t * t if t < 0.5 else 1 - 2 * (1 - t) ** 2
    want = tuple(round(lin_to_srgb(srgb_to_lin(c) * f)) for c in grad(mid_x, y))
    check("Soft edge: half-way into the ramp is half the light (linear)", diff(px(mid_x, y), want) <= 3,
          f"{px(mid_x, y)} vs {want}")
    check("Soft edge: the outermost pixel is (almost) black", max(px(W - 1, y)) <= 6, str(px(W - 1, y)))
    check("Soft edge: outside the ramp is untouched", diff(px(W - 300, y), grad(W - 300, y)) <= 1,
          f"{px(W - 300, y)} vs {grad(W - 300, y)}")
    check("Soft edge: other edges untouched", diff(px(2, y), grad(2, y)) <= 1, f"{px(2, y)}")

    # Warped test pattern follows the warp.
    ws.vendor("SetSliceWarp", {"sliceName": "Wall", "cols": 2, "rows": 2,
                               "points": [{"x": x, "y": y} for x, y in q]})
    ws.vendor("SetIdentify", {"displayName": "PM", "mode": "pattern"})
    time.sleep(0.3)
    px = snap(ws, work, "pattern_warped")
    ex, ey = hmap(Hq, 0.5, 0.0015)
    check("Test pattern is warped: its red top edge lies on the warped edge", px(int(ex), int(ey))[0] > 120,
          f"{px(int(ex), int(ey))} at {int(ex)},{int(ey)}")
    check("Test pattern is warped: the unwarped corner is black", px(3, 3) == (0, 0, 0), str(px(3, 3)))
    ws.vendor("SetIdentify", {"displayName": "PM", "mode": "off"})
    ws.vendor("SetSliceFeather", {"sliceName": "Wall", "right": 120, "left": 0, "top": 0, "bottom": 0,
                                  "gamma": 1.4})
    return at.state(ws)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--obs-app", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--port", type=int, default=at.DEFAULT_PORT)
    ap.add_argument("--plugin", default=at.DEFAULT_PLUGIN)
    args = ap.parse_args()
    os.makedirs(args.work, exist_ok=True)
    logfile = os.path.join(args.work, "obs-stdout.log")
    at.set_test_home(os.path.join(args.work, "obs-home"))
    at.install_plugin(args.plugin)
    at.reset_plugin_state()
    password = secrets.token_urlsafe(24)
    at.seed_config(args.port, password)

    proc = at.launch_obs(args.obs_app, logfile)
    try:
        ws = obsws.connect(port=args.port, password=password, retries=300)
        log(f"Connected to OBS {ws.call('GetVersion')['obsVersion']}")
        at.setup_obs(ws, args.work)
        before = phase(ws, args.work)
        ws.close()
    finally:
        log("\nQuitting OBS…")
        at.quit_obs(proc)

    log("Restarting OBS…")
    proc = at.launch_obs(args.obs_app, logfile)
    try:
        ws = obsws.connect(port=args.port, password=password, retries=300)
        st = at.state(ws)
        a, b = slice_of(st), slice_of(before)
        check("Warp persists across a restart", a.get("warp") == b.get("warp"), json.dumps(a.get("warp")))
        check("Soft edge persists across a restart", a.get("feather") == b.get("feather"),
              json.dumps(a.get("feather")))
        ws.close()
    finally:
        at.quit_obs(proc)

    passed = sum(1 for _, ok, _ in at.RESULTS if ok)
    log(f"\n{passed}/{len(at.RESULTS)} checks passed")
    with open(os.path.join(args.work, "results.json"), "w") as f:
        json.dump([{"check": n, "pass": ok, "detail": d} for n, ok, d in at.RESULTS], f, indent=2)
    sys.exit(0 if passed == len(at.RESULTS) else 1)


if __name__ == "__main__":
    main()
