# Verification record

What has actually been tested, on what, and how. Anything not listed here
is **untested**. Last updated 2026-11-16, for Vexel **0.8.0**.

## Summary

| Platform | Builds in CI (GitHub Actions) | Tested in OBS | Displays used |
|---|---|---|---|
| Windows x64 | Yes, every push (`windows-2022`, MSVC, warnings as errors) | Developer's show PC, live shows (manual) | **Physical**: LED wall + HDMI displays |
| macOS universal | Yes, every push | Automated end-to-end tests on one Mac | **Virtual windows**, plus one fullscreen output on the built-in screen; no external displays |
| Linux (Ubuntu 24.04) | Yes, every push | **Not tested** | – |

No automated test has run on Windows; no physical-display test has run on
macOS. OBS **33.0.1** is the only OBS version tested anywhere.

## Environments

### Mac (automated tests)

| | |
|---|---|
| Machine | Apple MacBook Air, Apple M3 (`Mac15,12`), **built-in display only** (Liquid Retina, 2560×1664) |
| macOS | 27.0.1 (build 26A434) |
| OBS Studio | **33.0.1** official macOS Apple Silicon build (obs-websocket 5.7.6, Qt 6.11.1), OpenGL renderer (default) |
| Plugin build | Xcode 27.1, CMake 4.5.0, against OBS 33.0.1 sources plus obs-deps / Qt 2026-10-23; universal binary, ad-hoc signed |

The automated tests run OBS in an isolated home (own settings and plugins,
websocket port 4466) so they never touch a normal OBS install.

### Windows show PC

| | |
|---|---|
| Machine | Laptop, Intel Core Ultra 9 275HX, NVIDIA GeForce RTX 5070 Ti Laptop GPU (OBS renders on it with D3D11) plus Intel integrated graphics |
| Windows | Windows 11 25H2 (10.0 build 26200) |
| OBS Studio | **33.0.1** (64-bit), Qt 6.11.1 |
| Outputs | Three **physical** fullscreen outputs: `LED WALL` 2688×1152 on an HDMI 2.0 output, and two 1920×1080 outputs (`SL POSTERS`, `SR POSTERS`) through ROLAND VIDEO and, in a later session, Blackmagic devices |

Evidence:

- **Developer report (2026-11-16):** used the Windows build for multiple
  live shows and it has been solid. *Which Vexel version(s) were used at
  those shows is not yet recorded.*
- **OBS logs from this PC** (supplied by the developer) for 2026-11-13
  (plugin 0.5.2) and 2026-11-14 (plugin 0.6.2) show:
  - all three outputs restored automatically at launch on the right
    displays, each with its video surface created at the display's
    native size;
  - live delay changes;
  - one hot-unplug of all three displays that OBS survived, with all three
    outputs resuming by themselves when the displays came back (0.6.2);
  - one crash when two displays dropped (0.6.2). The cause was found and
    fixed in **0.6.4** (see *Bugs*, item 6). The fix has **not yet been
    re-tested** on this PC.
- Nothing newer than 0.6.2 has a recorded Windows result. In particular
  **NDI outputs (0.7.0+) and projection mapping (0.8.0) have not been run
  on Windows.**

## Automated (Mac)

Current results, 2026-11-16, on commit `c232b02` (the 0.8.0 code; the next
commit, `862a52f`, only changes the version number):

| Test | Result | What it covers |
|---|---|---|
| `output-mapping-core-tests` | **All pass** | Geometry (draw, move, resize, aspect lock, snapping, numeric edits, fit, 1:1, clipping), display identity matching (exact UUID, serial re-identification, twin units, missing serials, duplicate UUIDs), warp maths (corner pin perspective, mesh through its points, resampling, outlines) |
| `tools/acceptance_test.py` | **72/72** | The six-step acceptance test below, plus delay, presets, locking, clipboard, identify, restart |
| `tools/warp_test.py` | **33/33** | Projection mapping: corner pin geometry checked against an independently computed perspective, mesh, editor point drag, arrow-key nudges, Tab, Esc, undo, soft-edge brightness curve, warped test pattern, persistence |
| `tools/ndi_test.py` | **32/32** | NDI outputs against a **stand-in NDI library** that records every frame: format, frame rate, pixel-exact content, scaling, delay, test pattern, rename, stop, resume after restart, missing runtime. **No real NDI Runtime or network was used.** |

An earlier full log (74 checks, 2026-11-13, version 0.5.2) is in
[docs/results/acceptance-run.txt](docs/results/acceptance-run.txt).

The acceptance test runs the six steps of the requested acceptance test.
Since only one physical display was available, the two 1920×1080 outputs
are **virtual displays opened as windows**. Pixels are verified with
**Snapshot**, which renders through the same `gfx::renderDisplay()` call
the fullscreen outputs use, on the GPU, inside a real OBS frame. The
Program source is a 3840×1080 pattern in which every pixel encodes its own
coordinates (a 1 px shift changes the value by 67).

| Step | What was checked | Result |
|---|---|---|
| 1 | Program canvas set to 3840×1080; two 1920×1080 outputs | ✅ |
| 2 | Slices drawn over each half **by dragging in Input Selection** (synthetic mouse events through the real editor handlers); edges snap to 0/1920/3840/1080; each lands on its own display at 1:1 | ✅ |
| 2 | HDMI-1 = Program x 0–1919 and HDMI-2 = Program x 1920–3839, **every pixel, max difference 0**; seam columns are exactly x=1919 and x=1920 | ✅ |
| 3 | Dragging Slice 1 in Input Selection moves its crop by (+100,+50); destination unchanged; output pixel-exact, with rows beyond the canvas black | ✅ |
| 4 | Dragging Slice 2 in Output Placement moves it to (200,100); crop unchanged; output pixel-exact with a black margin. Arrow-key nudge (+3, ⇧+10) | ✅ |
| 5 | Fade to Scene B: **mid-transition** both outputs show a blend of A and B; afterwards they are pixel-exact Scene B. Stopping HDMI-1 leaves HDMI-2 running and following Program | ✅ |
| 6 | OBS quit and relaunched: both slices' source and destination persist exactly; display list persists | ✅ |
| 6 | A saved display that isn't connected shows **Disconnected**. A same-model display without a proven identity shows **Ambiguous**. Fullscreen is refused on both before and after the restart, and their pending restore is held back instead of going to another screen | ✅ |
| extra | **Real borderless fullscreen** output opened on the built-in display and kept in place by the placement watchdog, restored automatically after an OBS restart (exact UUID match), and its restore flag cleared when the user stops it | ✅ |
| extra | Delete a slice with the Delete key, Undo restores it exactly, Redo removes it again | ✅ |

The screenshots in `docs/images/` are made by `tools/readme_shots.py`,
which runs an isolated OBS. Editor and dock images come from the plugin's
`EditorCapture` / `DockCapture`. Those composite each live preview,
rendered through its own GPU draw path, into a Qt grab of the window, so
macOS Screen Recording permission isn't needed. Output images come from
`Snapshot`, which renders exactly what the display receives.

## Show-readiness soak test (Mac, version 0.5.0)

Run once, on 2026-11-12, on **version 0.5.0**; **not re-run** for later
versions. `tools/soak_test.py`, 30 minutes at **60 fps** with three outputs (2×1920×1080,
1×1280×720 with scaled slices), nothing else running on the machine. Full
report: [docs/results/soak-report.md](docs/results/soak-report.md).

| Check | Result |
|---|---|
| Crashes / disconnects | none; OBS quit cleanly |
| Pixel-exact output checks after every change | **661 / 661 exact** |
| Workload | 594 scene changes (fades), 256 delay changes, 163 slice nudges, 138 preset recalls, 105 test-pattern toggles |
| Memory growth after warm-up | none (−96 MB; macOS reclaimed memory) |
| Skipped render frames | 32 of 104,397 (0.031 %) |
| Frame render time | mean 4.95 ms, worst 30 s sample 8.57 ms (budget 16.7 ms) |

**Verdict at the time: SHOW READY** on this Mac (version 0.5.0). An earlier 10-minute run passed every
check except one render-time sample taken while the plugin was being
compiled on the same machine (8.5 ms; skipped frames 0.012 %); the criterion
was then set to mean < 8 ms and worst sample < 12 ms, and the clean 30-minute
run above was made with nothing else running. Run the same soak on the show
PC (Windows) before an event: `python3 tools/soak_test.py … --minutes 30`.

## Bugs found by this verification and fixed

0. **(0.5.1, reported on Windows.)** Resizing the Output Mapping dock could
   make it flicker and crash OBS. The dock thumbnails were native video
   windows (each with its own swapchain) inside a scroll area, and resized
   themselves from their own resize handler, which can oscillate with a
   Windows scrollbar; rapid or degenerate D3D11 swapchain resizes can leave
   no render target. Fixed by (a) drawing dock thumbnails as plain widgets
   from GPU-rendered images (~10 fps, double-buffered readback, no native
   windows), (b) height-for-width layout with an always-visible scrollbar,
   and (c) coalescing and validating every remaining swapchain resize
   (editor previews, outputs: one resize after the size settles, never
   below 2×2 px). Dock stress test (macOS, Windows-style scrollbars forced):
   16 sizes down to 30×40, a 60-step rapid drag and 10 dock/float cycles:
   0 swapchain resizes from the dock, 1 layout pass per tile, OBS responsive.

1. **Restore recursion → crash on launch.** Resuming a saved fullscreen
   output re-scanned the screens, which resumed again, and so on until the
   stack overflowed. Fixed: rescanning never resumes, plus a re-entrancy
   guard.
2. **Quitting OBS cleared the restore flag.** Qt closes every window at
   quit, and the fullscreen output treated that like a user stop. Fixed:
   borderless outputs treat external closes as an app quit and keep the
   flag.
3. **Rects lost over obs-websocket.** `obs_data` can't carry arrays of plain
   numbers. Rects are now `{x,y,w,h}` objects; the legacy array form still
   loads.
4. The editor opened partly off-screen, and the sidebar inspector was
   clipped. Fixed.
5. (Found in review, Windows.) Falling back to GDI names (`\\.\DISPLAYn`)
   as an identity could have matched a different monitor after Windows
   renumbered them. Those displays now never get an exact identity; they
   show as ambiguous until you confirm them for the session.
6. **(0.6.4, from the Windows show PC's log.)** OBS could crash when a
   display disappeared. Stopping an output deleted its window while Windows
   was still delivering the display-change message to it (and inside Qt's
   *screen removed* handling). Outputs now stop rendering and hide at once,
   and the window is deleted afterwards. Not yet re-tested on that PC.

## Windows: what still needs a recorded result

The developer's show use (above) covers the core: physical fullscreen
outputs, restore at launch and live delay. Still to record, ideally on the
release being reviewed: see [docs/release-hardware-checklist.md](docs/release-hardware-checklist.md)
for the short list, and [docs/windows-test-plan.md](docs/windows-test-plan.md)
for the full one.

## macOS: not verified here, needs real hardware

See [docs/hardware-test-checklist.md](docs/hardware-test-checklist.md). In short:

- Two **simultaneous fullscreen** outputs on **two external HDMI displays**,
  and the 1:1 seam as it appears on the actual panels and signal. Only
  windowed outputs and one built-in fullscreen output were tested.
- Behaviour with *Displays have separate Spaces* turned off.
- **Hot-unplug and replug** while live: the code closes the output on
  `QGuiApplication::screenRemoved` before macOS can move the window, and
  resumes on exact reconnection. Neither path could be exercised without a
  second display.
- EDID serial re-identification when a display moves to another port. This
  is covered by unit tests only.
- Scaled ("looks like") modes on an external display, the Metal renderer,
  Intel Macs (the x86_64 slice is built but not run), and Linux.
- Projection mapping on a real projector (alignment by eye on a surface,
  soft-edge blending between two overlapping projectors).
- NDI with the real NDI Runtime and a receiver on the network.

## Environment notes

- OBS 33.0.1 sometimes takes 30–60 s to quit, because its main window
  waits for its own *What's New* network thread. This is unrelated to the
  plugin. The harness waits up to 90 s before force-killing.
- Building inside an iCloud-synced folder breaks `codesign`; see the
  README.
