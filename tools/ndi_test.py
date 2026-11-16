#!/usr/bin/env python3
"""End-to-end test of NDI displays, against a stand-in NDI library.

Runs a real, isolated OBS (same harness as acceptance_test.py) with Vexel
pointed at tools/ndi_stub.cpp instead of the NDI Runtime. The stub records
every frame Vexel hands to NDI, so this checks what would go on the network:

  - NDI displays start, send BGRX progressive frames at the OBS frame rate
    and the display's own resolution, pixel-exact for 1:1 slices
  - scaled slices match the output renderer (Snapshot)
  - test pattern, delay and rename (re-announced under the new name)
  - stop, and resume after an OBS restart
  - without an NDI Runtime: a clear error, nothing else affected

Usage:
  clang++ -std=c++17 -dynamiclib -arch arm64 -arch x86_64 -o /tmp/libndi_stub.dylib tools/ndi_stub.cpp
  python3 tools/ndi_test.py --obs-app ~/Library/Caches/obs-output-mapping/OBS.app \\
      --work /tmp/ndi --stub /tmp/libndi_stub.dylib
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
from testpattern import pattern_pixel  # noqa: E402

R, check, log, disp = at.R, at.check, at.log, at.disp
BGRX = ord("B") | ord("G") << 8 | ord("R") << 16 | ord("X") << 24


def safe(name):
    return "".join(c if c.isalnum() or c in "-_" else "_" for c in name)


def stub_info(stub_dir, name):
    try:
        with open(os.path.join(stub_dir, safe(name) + ".json")) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def stub_frame(stub_dir, name, w, h):
    """Latest dumped frame as a function (x, y) -> (r, g, b)."""
    path = os.path.join(stub_dir, safe(name) + ".bgra")
    with open(path, "rb") as f:
        data = f.read()
    if len(data) != w * h * 4:
        raise RuntimeError(f"{path}: {len(data)} bytes, expected {w * h * 4}")
    return lambda x, y: (data[(y * w + x) * 4 + 2], data[(y * w + x) * 4 + 1], data[(y * w + x) * 4])


def wait_frames(stub_dir, name, more, timeout=6.0):
    start = stub_info(stub_dir, name).get("frames", 0)
    t0 = time.time()
    while time.time() - t0 < timeout:
        if stub_info(stub_dir, name).get("frames", 0) >= start + more:
            return True
        time.sleep(0.1)
    return False


def mismatches(px, expected, w, h, step=3, tol=2):
    bad = 0
    for y in range(0, h, step):
        for x in range(0, w, step):
            if max(abs(a - b) for a, b in zip(px(x, y), expected(x, y))) > tol:
                bad += 1
    return bad


def layout():
    return {"format": "obs-output-mapping", "version": 1, "restoreOutputs": True, "excludeFromCapture": True,
            "displays": [
                {"id": "ndi-a", "name": "Vexel NDI A", "kind": "ndi", "width": 1920, "height": 1080,
                 "slices": [{"id": "sa", "name": "Right half", "color": "#19D3F5",
                             "src": R(1920, 0, 1920, 1080), "dst": R(0, 0, 1920, 1080)}]},
                {"id": "ndi-b", "name": "Vexel NDI B", "kind": "ndi", "width": 960, "height": 540,
                 "slices": [{"id": "sb", "name": "Scaled", "color": "#8A5CFF",
                             "src": R(0, 0, 3840, 1080), "dst": R(0, 0, 960, 270)}]},
            ]}


def phase_send(ws, work, stub_dir):
    r = ws.vendor("SetState", {"state": layout()})
    check("Layout with two NDI displays loaded", r.get("ok"), r.get("error", ""))
    st = at.state(ws)
    check("NDI displays report kind 'ndi'", all(d["kind"] == "ndi" for d in st["displays"]))

    r = ws.vendor("StartNdi", {"displayName": "Vexel NDI A"})
    check("Start NDI A", r.get("ok"), r.get("error", ""))
    r = ws.vendor("StartNdi", {"displayName": "Vexel NDI B"})
    check("Start NDI B", r.get("ok"), r.get("error", ""))
    st = r["state"]
    check("State reports the NDI runtime", st.get("ndiRuntime") == "NDI stub (test)", st.get("ndiRuntime"))
    check("Both displays report output 'ndi'",
          all(d["output"] == "ndi" for d in st["displays"]), str([d["output"] for d in st["displays"]]))

    check("NDI A is sending frames", wait_frames(stub_dir, "Vexel NDI A", 30))
    t0, f0 = time.time(), stub_info(stub_dir, "Vexel NDI A").get("frames", 0)
    time.sleep(3.0)
    f1 = stub_info(stub_dir, "Vexel NDI A").get("frames", 0)
    rate = (f1 - f0) / (time.time() - t0)
    check("NDI A sends at the OBS frame rate (30 fps)", 28.0 <= rate <= 31.0, f"{rate:.1f} fps")
    a = stub_info(stub_dir, "Vexel NDI A")
    check("NDI A frames are 1920x1080 BGRX, progressive, 30/1 fps",
          a.get("width") == 1920 and a.get("height") == 1080 and a.get("fourcc") == BGRX
          and a.get("format") == 1 and a.get("fpsN") == 30 and a.get("fpsD") == 1
          and a.get("stride") == 1920 * 4, str(a))
    b = stub_info(stub_dir, "Vexel NDI B")
    check("NDI B frames are its own resolution (960x540)", b.get("width") == 960 and b.get("height") == 540, str(b))

    st = at.state(ws)
    da = disp(st, "Vexel NDI A")
    check("Dock health: NDI A ~30 fps", 28 <= da["outputFps"] <= 31, str(da["outputFps"]))
    check("Receivers reported", da["ndiConnections"] == 2, str(da["ndiConnections"]))

    px = stub_frame(stub_dir, "Vexel NDI A", 1920, 1080)
    bad = mismatches(px, lambda x, y: pattern_pixel(1920 + x, y, at.TAG_A), 1920, 1080)
    check("NDI A frame == Program right half, pixel-exact", bad == 0, f"mismatches={bad}")

    snap = at.snapshot(ws, "Vexel NDI B", os.path.join(work, "ndi_b_snapshot.ppm"))
    w, h, data = at.read_ppm(snap)
    sp = lambda x, y: tuple(data[(y * w + x) * 3:(y * w + x) * 3 + 3])
    pb = stub_frame(stub_dir, "Vexel NDI B", 960, 540)
    bad = mismatches(pb, sp, 960, 540, step=2, tol=3)
    check("NDI B (scaled slice) matches the output renderer", bad == 0, f"mismatches={bad}")
    check("NDI B area below the slice is black", pb(480, 400) == (0, 0, 0), str(pb(480, 400)))

    # Test pattern on an NDI display.
    ws.vendor("SetIdentify", {"displayName": "Vexel NDI A", "mode": "pattern"})
    time.sleep(0.2)
    wait_frames(stub_dir, "Vexel NDI A", 15)
    px = stub_frame(stub_dir, "Vexel NDI A", 1920, 1080)
    check("Test pattern goes out over NDI (red edge, white inside)",
          px(0, 500) == (255, 40, 40) and px(1, 500) == (255, 255, 255), f"{px(0, 500)} {px(1, 500)}")
    ws.vendor("SetIdentify", {"displayName": "Vexel NDI A", "mode": "off"})

    # Delay: the NDI frame stays on Scene A after a cut, then follows.
    r = ws.vendor("SetDelay", {"displayName": "Vexel NDI A", "delayMs": 500})
    check("Set 500 ms delay on NDI A", r.get("ok"), r.get("error", ""))
    time.sleep(1.5)  # buffer fills, crossfade completes
    ws.call("SetCurrentSceneTransition", {"transitionName": "Cut"})
    ws.call("SetCurrentProgramScene", {"sceneName": "Scene B"})
    time.sleep(0.15)
    wait_frames(stub_dir, "Vexel NDI A", 3)
    px = stub_frame(stub_dir, "Vexel NDI A", 1920, 1080)
    check("Delayed NDI A still shows Scene A just after the cut",
          px(100, 100) == pattern_pixel(2020, 100, at.TAG_A), str(px(100, 100)))
    time.sleep(1.2)
    wait_frames(stub_dir, "Vexel NDI A", 12)
    px = stub_frame(stub_dir, "Vexel NDI A", 1920, 1080)
    bad = mismatches(px, lambda x, y: pattern_pixel(1920 + x, y, at.TAG_B), 1920, 1080, step=5)
    check("Delayed NDI A shows Scene B after the delay, pixel-exact", bad == 0, f"mismatches={bad}")
    ws.vendor("SetDelay", {"displayName": "Vexel NDI A", "delayMs": 0})
    ws.call("SetCurrentProgramScene", {"sceneName": "Scene A"})

    # Rename: the source is re-announced under its new name.
    r = ws.vendor("RenameDisplay", {"displayName": "Vexel NDI B", "name": "Vexel NDI B2"})
    db = disp(r["state"], "Vexel NDI B2")
    check("NDI B renamed and still sending", db["output"] == "ndi", db["output"])
    check("Renamed NDI B is re-announced under its new name", wait_frames(stub_dir, "Vexel NDI B2", 20))
    check("The old NDI B source name is closed", stub_info(stub_dir, "Vexel NDI B").get("destroyed") is True)

    r = ws.vendor("StopOutput", {"displayName": "Vexel NDI B2"})
    time.sleep(0.5)
    check("Stop NDI B: state 'none'", disp(r["state"], "Vexel NDI B2")["output"] == "none")
    check("Stop NDI B: the NDI source is closed", stub_info(stub_dir, "Vexel NDI B2").get("destroyed") is True)
    check("NDI A keeps sending after B stops", wait_frames(stub_dir, "Vexel NDI A", 20))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--obs-app", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--stub", required=True, help="built tools/ndi_stub.cpp library")
    ap.add_argument("--port", type=int, default=at.DEFAULT_PORT)
    ap.add_argument("--plugin", default=at.DEFAULT_PLUGIN)
    args = ap.parse_args()
    os.makedirs(args.work, exist_ok=True)
    stub_dir = os.path.join(args.work, "ndi-stub")
    os.makedirs(stub_dir, exist_ok=True)
    for f in os.listdir(stub_dir):
        os.remove(os.path.join(stub_dir, f))
    logfile = os.path.join(args.work, "obs-stdout.log")

    at.set_test_home(os.path.join(args.work, "obs-home"))
    at.install_plugin(args.plugin)
    at.reset_plugin_state()
    password = secrets.token_urlsafe(24)
    at.seed_config(args.port, password)

    os.environ["OUTPUT_MAPPING_NDI_LIB"] = os.path.abspath(args.stub)
    os.environ["NDI_STUB_DIR"] = stub_dir
    proc = at.launch_obs(args.obs_app, logfile)
    try:
        ws = obsws.connect(port=args.port, password=password, retries=300)
        log(f"Connected to OBS {ws.call('GetVersion')['obsVersion']}")
        at.setup_obs(ws, args.work)
        phase_send(ws, args.work, stub_dir)
        ws.close()
    finally:
        log("\nQuitting OBS…")
        at.quit_obs(proc)

    a = stub_info(stub_dir, "Vexel NDI A")
    check("Quitting OBS closes the NDI sources cleanly", a.get("destroyed") is True, str(a))

    log("Restarting OBS (NDI A was running: it must resume)…")
    os.remove(os.path.join(stub_dir, "Vexel_NDI_A.json"))
    proc = at.launch_obs(args.obs_app, logfile)
    try:
        ws = obsws.connect(port=args.port, password=password, retries=300)
        t0 = time.time()
        while time.time() - t0 < 15 and not stub_info(stub_dir, "Vexel NDI A").get("frames"):
            time.sleep(0.5)
        check("NDI A resumes by itself after a restart", stub_info(stub_dir, "Vexel NDI A").get("frames", 0) > 0)
        st = at.state(ws)
        check("NDI B (stopped) stays stopped after a restart",
              disp(st, "Vexel NDI B2")["output"] == "none", disp(st, "Vexel NDI B2")["output"])
        ws.close()
    finally:
        at.quit_obs(proc)

    log("Restarting OBS without an NDI Runtime…")
    os.environ["OUTPUT_MAPPING_NDI_LIB"] = os.path.join(args.work, "no-such-libndi.dylib")
    proc = at.launch_obs(args.obs_app, logfile)
    try:
        ws = obsws.connect(port=args.port, password=password, retries=300)
        time.sleep(3)
        r = ws.vendor("StartNdi", {"displayName": "Vexel NDI B2"})
        check("No runtime: Start NDI refused with a clear message",
              not r.get("ok") and "NDI Runtime not installed" in r.get("error", ""), r.get("error", ""))
        st = at.state(ws)
        check("No runtime: status says so", "NDI Runtime not installed" in disp(st, "Vexel NDI A")["statusText"],
              disp(st, "Vexel NDI A")["statusText"])
        check("No runtime: OBS keeps running normally", ws.call("GetVersion").get("obsVersion"))
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
