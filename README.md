<p align="center"><img src="docs/images/vexel-banner.png" alt="Vexel" width="880"></p>

<p align="center"><a href="https://buymeacoffee.com/okamidesigns"><img src="https://img.shields.io/badge/Buy%20me%20a%20coffee-okamidesigns-FFDD00?logo=buymeacoffee&logoColor=black" alt="Buy me a coffee"></a></p>

# Vexel

**Vexel** is a third-party plugin for [OBS Studio](https://obsproject.com)
by Okami Designs, for mapping OBS's Program onto multiple displays,
projectors and NDI outputs. It is not made or endorsed by the OBS Project.
It was previously called "Output Mapping"; existing installs upgrade in
place (see [Upgrading](#upgrading-from-output-mapping)).

Draw **slices** on the live Program canvas, assign each slice to a display
(HDMI output, projector or NDI source), and independently adjust **which
part of Program it grabs** (Input Selection) and **where it lands on the
display** (Output Placement), including corner pin, mesh warp and soft
edges for projection mapping. Every mapped output follows Program live,
including transitions, and runs independently of streaming and recording.

**[Download](https://github.com/Jallison154/Vexel/releases)** ·
**[Install on Windows](#install-on-windows)** ·
**[Install on macOS](#install-on-macos)** ·
**[Report a problem](https://github.com/Jallison154/Vexel/issues)** ·
**[What's been tested](VERIFICATION.md)**

![Vexel editor: Input Selection (Program canvas, top) and Output Placement (one display, bottom)](docs/images/editor.png)

```
Program canvas 3840×1080
┌───────────────────┬───────────────────┐
│     Slice 1       │      Slice 2      │   Input Selection: source crops (Program px)
└─────────┬─────────┴─────────┬─────────┘
          ▼                   ▼
   ┌─────────────┐     ┌─────────────┐
   │   HDMI-1    │     │   HDMI-2    │      Output Placement: destinations (display px)
   │ 1920×1080   │     │ 1920×1080   │
   └─────────────┘     └─────────────┘
```

## Status

All releases so far are **betas** (GitHub pre-releases). Built for
**OBS Studio 33.0.1**; other OBS versions are untested.

| Platform | Release download | Builds in CI | Tested in OBS |
|---|---|---|---|
| **Windows 10 (2004+) / 11, x64** | `.zip` | Yes (MSVC, warnings as errors) | Used at live shows by the developer on one Windows 11 PC with physical outputs (LED wall and HDMI displays); see [VERIFICATION.md](VERIFICATION.md#windows-show-pc). No automated tests run on Windows. |
| **macOS 13+, universal** (Apple Silicon and Intel) | `.pkg` | Yes | Automated end-to-end tests on one Apple Silicon Mac (macOS 27.0.1), using **virtual displays opened as windows** plus one fullscreen output on the built-in screen. **Not tested with external displays**, on Intel Macs, or on macOS versions other than 27. |
| Linux (Ubuntu 24.04) | none | Yes | Not tested. Uses a generic Qt display layer. |

Exactly what was tested, on which hardware and version, and what still
needs real hardware: [VERIFICATION.md](VERIFICATION.md).

**Which version has what:** projection mapping (corner pin, mesh warp,
soft edges) needs **0.8.0 or later**; NDI outputs need **0.7.0 or later**.
The release notes on [Releases](https://github.com/Jallison154/Vexel/releases) list
what's new in each version.

## Features

- **Projection mapping** (per slice, in the slice panel under *Projection
  mapping*):
  - **Corner pin:** drag the slice's 4 corners in Output Placement to fit any
    4-sided surface (a tilted screen, a building face, a projector at an
    angle). True perspective, not a stretch.
  - **Mesh warp** (3×3 up to 9×9 points): bend the slice for curved or
    uneven surfaces. The picture passes smoothly through every point.
    Switching between corner pin and mesh sizes keeps the current shape.
  - **Soft edges** for projector blending: a width in output pixels for each
    side (left, right, top, bottom) and an *Edge gamma*. At 1.00 two
    overlapping edges add up to even brightness.
  - **Editing:** click a point (or drag it) in Output Placement. **Arrow keys
    move the selected point 1 px, Shift+arrows 10 px**, Tab steps to the
    next point, Esc lets go of it (arrows then move the whole slice again).
    The selected point's exact X / Y can also be typed in. *Reset* puts the
    points back on the rectangle. All of it is undoable.
  - The warp is stored relative to the slice's output rectangle, so moving
    or resizing the slice carries it along.
  - Works everywhere the slice goes: fullscreen and windowed outputs, NDI,
    delay, dock previews and snapshots. The **test pattern and slice
    outlines follow the warp**, so you can line the grid up on the real
    surface. An untouched corner pin or mesh is still a pixel-exact 1:1
    copy.
  - Requires Vexel 0.8.0 or later. Masks (cutting a slice to a custom
    outline) are not implemented.

- **NDI outputs** (*＋ Display ▸ NDI Output…*, or pick *NDI (network
  source)* under *Output to*): a display with a resolution you choose that
  goes out over the network as an NDI source instead of to a screen.
  Receivers (NDI Studio Monitor, vMix, another OBS, LED processors that
  take NDI) see it as `COMPUTER (name)`. It is composited exactly like a
  fullscreen output: slices, per-output delay with crossfade, test pattern
  and slice outlines all apply, 1:1 slices are pixel-exact, at the OBS frame
  rate. Video only. **▶ Start NDI** / **■ Stop NDI** in the editor and dock;
  the dock shows the frames per second sent and how many receivers are
  connected. A running NDI output comes back after an OBS restart, and
  renaming it re-announces the source under its new name.
  - Needs the free **NDI Runtime** on the computer (installed with
    [NDI Tools](https://ndi.video/tools/)); Vexel loads it from where NDI
    installs it and ships nothing NDI itself. Without it, NDI displays say
    *NDI Runtime not installed* and everything else works as before.
  - Rendering and sending never hold up OBS: frames are read back from the
    GPU two frames late (no stall) and sent from a separate thread; if the
    network falls behind, the newest frame wins.

- **Vexel dock** in the main OBS window (OBS ▸ *Docks ▸
  Vexel*; shown automatically the first time): a confidence monitor with a
  live thumbnail of exactly what each display receives (including delay and
  test patterns), its status (LIVE / WINDOW / READY / OFFLINE / CHECK /
  VIRTUAL), the frames per second each running output actually draws (or a
  red **⚠ NOT RENDERING** if it stops), ⏱ delay and ⚠ test-pattern markers, per-display **Fullscreen**,
  **Window** and **Clear Pattern** buttons, plus **Start All**, **Stop All**
  (asks first when live), **Test Pattern All** / **Clear All Patterns**
  (asks first when live) and **Editor…**. "Live previews" pauses the
  thumbnails if you need to save GPU; outputs are unaffected. Thumbnails
  update at the OBS frame rate and contain no native video windows, so
  resizing, docking or floating the dock can't disturb OBS or the outputs.

  ![Vexel dock](docs/images/dock.png)

- **One editor window** (Tools ▸ *Vexel Output Mapping…*): a display and slice
  sidebar, plus two synchronized live previews side by side.
  - **Input Selection** shows the live Program canvas. Click and drag to draw a
    slice. Drag or resize a slice to change which Program pixels it grabs;
    its destination stays fixed.
  - **Output Placement** shows the selected display at its real aspect ratio,
    with its name and resolution, and renders the actual composed video.
    Drag or resize slices there to move them on the display; their source
    crop stays fixed. It is drawn by the same GPU code as the real output.
  - Each slice has the same outline colour in both previews and the sidebar.
    Selection is synced across all three.
  - Sidebar: rename (double-click), duplicate (⌘D), delete, reorder by drag
    or Bring Forward / Send Backward, and reassign to another display (drag,
    context menu, or the *Display* field). **Top of the list is drawn on
    top.**
  - Numeric **X / Y / W / H** fields for the source (Program px) and the
    destination (physical display px), kept in sync with the drag handles.
  - Zoom (mouse wheel, Ctrl/⌘+scroll, pinch, + / −), pan (trackpad
    scroll, Space-drag, middle-drag), Fit (F or double-click), 100% (1).
    Snapping to canvas edges, the centre and other slices (hold Alt/⌥ to
    bypass). Arrow-key nudge (Shift ×10). Optional aspect-ratio lock (hold
    Shift while resizing to toggle it temporarily).
  - **Layout ▾**: show the two previews side by side or stacked (top /
    bottom). *Auto* (default) stacks them when the Program canvas is wider
    than 2:1, which on 3840×1080 more than doubles each preview's size. The
    choice is remembered.
  - **Keyboard commands** (Ctrl on Windows, ⌘ on Mac; full list under
    *Options ▸ Keyboard Shortcuts*): **Copy / Cut / Paste** slices
    (pasting on the same display offsets the copy; on another display it
    keeps the crop), **Paste Position Only** (Ctrl+Shift+V) and **Paste
    Crop Only** (Ctrl+Alt+V) to line slices up exactly, **Tab / Shift+Tab**
    next / previous slice, **] / [** bring forward / send backward, **H**
    show / hide, **F2** rename, **Esc** deselect. Copied slices go to the
    system clipboard, so they paste across presets and OBS sessions.
  - **Identify on the real output** (display panel ▸ *Identify*):
    - **Test Pattern** replaces each slice on the output with its own
      calibration image, drawn at that slice's exact size and position: red
      outermost pixel row and white second row on every edge (if either is
      missing on the screen, that edge is cropped), 100 px grid with pixel
      rulers counted from the slice's corner, corner markers in the slice's
      colour, centre cross, slice name, size and position. Areas no slice
      covers stay black. An output with no slices shows one full-screen
      pattern.
    - **Slice Outlines** draws each slice's outline and name over the live
      video, so you can line it up on the actual screen.
    - Output Placement shows the test pattern too, with the editor's
      draggable outlines on top, so you can fit slices against it.
    - If the display is live, it asks first ("this will take over what the
      audience sees"). A red **ON OUTPUT** badge shows in the sidebar and
      header while it's on. *Test Pattern All* (dock, or *Options ▸ Test
      Pattern on All Displays*) puts it on every display at once, and
      *Clear All Patterns* turns every one off. Identify is never saved: OBS always starts with it
      off. It ignores the output delay so you see changes instantly.
  - **Lock slices** that are set up correctly: tick **🔒 Locked** in the
    slice panel, press Ctrl/⌘+L, or right-click it (or *Lock All Slices* on
    a display). A locked slice can still be selected and inspected, but it
    can't be dragged, resized, nudged, typed into, fit, set to 1:1, deleted
    or moved to another display; reordering it in the list is still allowed.
    The lock is saved with mappings and presets.
  - **Delay** (per display, 0–1000 ms): delays that output's video, e.g. to
    line up displays with different processing latency or to match delayed
    audio. Rounded to whole frames; the editor shows the frame count and GPU
    memory used (1080p ≈ 8 MB per frame). Frames are buffered on the GPU.
    **Changing the delay live never flashes**: the output keeps playing at
    normal speed while the new delay buffers, then crossfades (0.5 s) from
    the old-delay picture to the new one. Turning delay on fades from live,
    and turning it off fades back to live.
    Previews stay live while you edit; the output and snapshots are delayed.
    Audio isn't delayed (use OBS's per-source Sync Offset).
  - **Presets ▾**: save the whole layout (displays, slices, positions)
    under a name and recall it with one click. Recall is live-safe: running
    outputs stay up if the preset still uses their display, the others stop,
    nothing new goes fullscreen by itself, and Undo brings back the previous
    layout. **Import / Export Layout File…** writes a portable `.json` you
    can back up or move between machines. Presets live in the plugin's
    `layouts` folder (*Presets ▸ Open Presets Folder*). "•" next to the
    preset name means you changed something since saving or recalling it.
  - **Undo / Redo** (Ctrl+Z / Ctrl+Y, ⌘Z / ⇧⌘Z) for every edit; a drag or
    a burst of nudges is one step.
  - The *new slices → Display* chip shows where a newly drawn slice will
    go. The sidebar shows each display's state at a glance (READY, LIVE,
    WINDOW, OFFLINE, CHECK, VIRTUAL), and each slice shows 1:1 or scaled.
  - An empty state walks you through adding the first display.
  - **Fit** (to display) and **1:1** (pixel for pixel) actions.
  - Outlines, labels, handles and guides are drawn only in the editor. The
    outputs carry only the composed video.
- **Outputs**
  - Independent **Start / Stop Fullscreen** per display. Several fullscreen
    outputs can run at once, with any number of slices composited per
    display.
  - **Open Window**: a windowed output for testing, or for *virtual* displays
    (a fixed resolution with no monitor attached).
  - **Save Snapshot…**: renders exactly what the display receives to a PNG.
  - **Stop All asks first** when fullscreen outputs are live, so one stray
    click can't black out a show.
  - Hotkeys (Settings ▸ Hotkeys): *Start all fullscreen outputs*, *Stop all
    outputs*, *Open editor*. Also Tools ▸ *Vexel: Stop All
    Outputs*.
  - **Esc** stops a windowed output. It stops a fullscreen output only when
    that output covers the screen OBS is on, so you can never get stuck
    behind it. A stray key on a show output on another display does nothing.
  - **Options ▸ Hide outputs from screen capture** (on by default): outputs
    are excluded from Display/Window Capture (Windows
    `WDA_EXCLUDEFROMCAPTURE`, macOS `NSWindowSharingNone`), so Program can
    never contain its own output.
- **Display identity and safety**
  - Displays are identified by the macOS display UUID plus EDID
    vendor/model/serial. A display counts as connected only when it is
    positively identified:
    - **Connected**: exact UUID match.
    - **Connected (re-identified by serial)**: the UUID changed (for example,
      another port), but a unique non-zero EDID serial proves it is the same
      unit.
    - **Ambiguous**: a same-model display is present, but nothing proves it
      is the same one (no serial, or a twin unit). Fullscreen is blocked
      until you pick the display under *Output to*.
    - **Disconnected**: nothing plausible is attached. Fullscreen is
      blocked.
  - If a display is unplugged while live, its output closes immediately,
    before macOS can move the window onto another screen. When *that exact
    display* comes back, the output resumes.
  - Outputs that were running when OBS quit are restored at launch, but only
    on exactly identified displays.
  - A **watchdog** checks every 250 ms (and immediately on Windows
    `WM_DISPLAYCHANGE`) that each fullscreen output still sits exactly on
    its own display. If the OS moved or resized it, it is put back; if the
    display is gone or its identity changed, the output is stopped.
  - On Windows, identity is the DisplayConfig monitor device path (one per
    monitor and connector) plus EDID vendor/product/serial read from the
    registry. Placement uses the monitor's exact physical rect, so 1:1 holds
    at any Windows scaling (125 %, 150 %, 175 %…). If a config file can't
    be read it is set aside rather than overwritten, and a `.bak` of the
    last good file is kept.
- **Persistence**: `mappings.json` in OBS's plugin settings folder, global
  and independent of scene collections. Hotkeys are stored in
  `hotkeys.json` next to it.
  - Windows: `%APPDATA%\obs-studio\plugin_config\output-mapping\`
  - macOS: `~/Library/Application Support/obs-studio/plugin_config/output-mapping/`
- **Remote control**: obs-websocket vendor `output-mapping`. See
  [Remote control](#remote-control-obs-websocket).

## How it works (OBS APIs)

| Need | OBS 33 API used | Notes |
|---|---|---|
| Live Program frame | `obs_get_main_texture()` (public, `obs.h`) | The main canvas render texture: Program after transitions. libobs renders it every frame whether or not you stream or record, so outputs are independent of both. No desktop capture, and nothing is fed back into Program. |
| Colour handling | Same technique selection as libobs' `obs_render_main_texture()` (`Draw` / `DrawTonemap` / `DrawMultiply`, sRGB-aware sampling) | Outputs look identical to OBS's own projectors. |
| Separate per-display rendering | `obs_display_create()` + `obs_display_add_draw_callback()` on a native Qt widget (`gs_window.view` = `NSView*`) | The same mechanism OBS projectors use. Each output and each editor preview is its own swapchain, rendered on the OBS graphics thread. |
| GPU crop and composite | `gs_draw_sprite_subregion()` with per-slice translate/scale matrices, the default effect, point sampling for exact 1:1 copies and linear for scaled ones | Sources are clipped to the canvas; the clipped part renders black rather than being clamped or stretched. |
| Clipping in the editor | `gs_set_viewport()` limited to the visible canvas, plus a matching `gs_ortho()` | Scissor rects are avoided because their Y origin differs between the GL and D3D/Metal back-ends. |
| Snapshots | `obs_add_main_rendered_callback()` → `gs_texrender` → `gs_stagesurface` | Runs the same `renderDisplay()` as the outputs, inside a real frame. |
| UI | `obs_frontend_add_tools_menu_qaction`, `obs_frontend_get_main_window`, frontend hotkeys, frontend events (`FINISHED_LOADING`, `EXIT`) | |

### Where the public API falls short, and what this plugin does instead

1. **No public Qt display widget.** OBS's `OBSQTDisplay` (and `OBSProjector`)
   live in the frontend and aren't exported to plugins. The plugin carries
   a small equivalent, `src/ui/obs-display-widget.cpp`: a native `QWidget`
   that creates an `obs_display` once exposed and resizes and destroys it
   with the window surface.
2. **No API for "fullscreen on display X" or for display identity.** OBS
   projectors use `QWidget::showFullScreen()` on a screen index, which (a)
   on macOS creates a separate Space per display and fails when *Displays
   have separate Spaces* is off, and (b) uses a screen index that changes
   when displays are re-plugged. The plugin instead:
   - opens a borderless window at the display's exact frame, at
     `NSStatusWindowLevel` (above the menu bar and Dock), joined to all
     Spaces, with no shadow and without activating
     (`src/platform/platform-macos.mm`). This works the same with or
     without separate Spaces;
   - on Windows, uses a `WS_POPUP`, topmost, tool-window HWND that never
     activates, placed with `SetWindowPos` at the monitor's exact physical
     rect (`platform-windows.cpp`), so taskbar, DPI scaling and Windows'
     own fullscreen heuristics don't change its size;
   - identifies displays by OS identity plus EDID vendor/model/serial
     (Windows: DisplayConfig device path and registry EDID; macOS:
     `CGDisplayCreateUUIDFromDisplayID`), and refuses anything ambiguous;
   - holds a display-sleep assertion while any fullscreen output runs.
3. **No public "Program texture" outside the render loop.** Snapshots use
   `obs_add_main_rendered_callback` so they run inside a frame on the
   graphics thread.

Everything else uses public libobs and obs-frontend-api calls. No private
symbols or frontend internals are touched.

## Install on Windows

No build needed. Requires Windows 10 (version 2004 or later) or 11, 64-bit,
and OBS Studio 33.

1. Quit OBS.
2. Go to [**Releases**](https://github.com/Jallison154/Vexel/releases) and download
   **`output-mapping-<version>-windows-x64.zip`** from the newest release
   (under *Assets*).
3. Right-click the zip ▸ **Extract All…**. You get a folder named `output-mapping`.
4. In File Explorer, paste `C:\ProgramData\obs-studio\plugins` into the
   address bar and press Enter. Create the `plugins` folder if it doesn't
   exist (`ProgramData` is hidden, but typing the path still works).
5. Move the `output-mapping` folder in there, so this file exists:
   `C:\ProgramData\obs-studio\plugins\output-mapping\bin\64bit\output-mapping.dll`
6. Start OBS and open **Tools ▸ Vexel Output Mapping…**.

To uninstall, delete that `output-mapping` folder. Every push to `main`
also builds a fresh zip on GitHub Actions (*Actions* tab ▸ latest run ▸
*Artifacts*; requires being signed in to GitHub).

## Install on macOS

No build needed. Requires macOS 13 or later and OBS Studio 33. The installer is universal
(Apple Silicon and Intel).

1. Quit OBS.
2. From [**Releases**](https://github.com/Jallison154/Vexel/releases),
   download **`output-mapping-<version>-macos-universal.pkg`**.
3. Double-click it. Because the installer isn't signed with an Apple
   Developer ID yet, macOS first says it can't be opened: click **Done**,
   then open **System Settings ▸ Privacy & Security**, scroll down to the
   message about `output-mapping…pkg` and click **Open Anyway**. (Only
   needed once per downloaded installer.)
4. Click through the installer. It installs only for your user, into
   `~/Library/Application Support/obs-studio/plugins/`, with no admin
   password needed.
5. Start OBS and open **Tools ▸ Vexel Output Mapping…**.

To uninstall, delete `~/Library/Application Support/obs-studio/plugins/output-mapping.plugin`.

For NDI outputs, also install [NDI Tools](https://ndi.video/tools/)
(includes the free NDI Runtime) and restart OBS.

## Support

Found a bug or have a question? [Open an issue on GitHub](https://github.com/Jallison154/Vexel/issues).
Please include your Vexel version (*Options ▸ About Vexel*), your OS, and
the OBS log of the session (*Help ▸ Log Files ▸ Upload Current Log File*
in OBS).

## Building from source

The first configure downloads the OBS 33.0.1 sources and the matching
prebuilt obs-deps and Qt 6 listed in `buildspec.json`, then builds libobs
and obs-frontend-api into `.deps/`. This is the standard
[obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)
flow.

### Windows

Requirements: Windows 10/11 x64, Visual Studio 2022 with *Desktop
development with C++*, Windows SDK 10.0.20348 or newer, CMake 3.28+,
Git.

```powershell
git clone https://github.com/Jallison154/Vexel.git
cd Vexel
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
cmake --install build_x64 --prefix release --config RelWithDebInfo
```

The last command creates `release\output-mapping\` in your clone. Install
it exactly as in step 5 above.

### macOS

Requirements: macOS 13+, Xcode 16+ (verified: Xcode 27.1), CMake 3.28+.

> **Don't put the build directory in an iCloud-synced folder** (Desktop or
> Documents with iCloud Drive). The File Provider adds
> `com.apple.FinderInfo` to `.plugin` bundles and `codesign` fails. Point
> `-B` somewhere else.

```bash
cmake --preset macos -B ~/Library/Caches/obs-output-mapping/build_macos
```

```bash
cmake --build ~/Library/Caches/obs-output-mapping/build_macos --config RelWithDebInfo
```

```bash
mkdir -p ~/Library/Application\ Support/obs-studio/plugins && ditto ~/Library/Caches/obs-output-mapping/build_macos/RelWithDebInfo/output-mapping.plugin ~/Library/Application\ Support/obs-studio/plugins/output-mapping.plugin
```

### Tests

- **Show-readiness soak test** (`tools/soak_test.py`): runs OBS at 60 fps
  with three outputs for a set time while changing scenes with fades every
  3 s, delay every 7 s, presets every 13 s, test patterns every 17 s and
  nudging slices. It checks every output pixel-exact after each change and
  tracks OBS memory, render time and skipped frames:

  Both test tools run OBS in an **isolated** home (own settings, plugins
  and websocket port 4466, `--multi`), so they never touch your real OBS or
  its settings, even while it is running.

  ```bash
  python3 tools/soak_test.py --obs-app /Applications/OBS.app --work /tmp/omsoak --minutes 30
  ```

- Core unit tests (geometry, display identity rules):
  `output-mapping-core-tests` (built with the plugin).
- End-to-end acceptance test against a real OBS. It uses obs-websocket
  with a random password, creates an "Output Mapping Test" scene
  collection, and restarts OBS once:

  ```bash
  python3 tools/acceptance_test.py --obs-app /Applications/OBS.app --work /tmp/omtest
  ```

  The harness currently launches OBS the macOS way; on Windows, run the
  steps in [docs/windows-test-plan.md](docs/windows-test-plan.md).
- Projection mapping end-to-end test (`tools/warp_test.py`): a smooth
  gradient as Program, so every warped pixel can be checked against the
  Program point it should come from (Python homography). Covers corner pin
  perspective, mesh, editor point drag, arrow-key nudges, Tab, Esc, undo,
  soft edges, the warped test pattern and persistence:

  ```bash
  python3 tools/warp_test.py --obs-app /Applications/OBS.app --work /tmp/omwarp
  ```
- NDI end-to-end test (`tools/ndi_test.py`): runs OBS with a stand-in NDI
  library (`tools/ndi_stub.cpp`) that records every frame Vexel would send,
  and checks format, frame rate, pixel-exact content, delay, test pattern,
  rename, stop, resume after restart and a missing runtime. No NDI install
  or network needed:

  ```bash
  clang++ -std=c++17 -dynamiclib -arch arm64 -arch x86_64 -o /tmp/libndi_stub.dylib tools/ndi_stub.cpp
  python3 tools/ndi_test.py --obs-app /Applications/OBS.app --work /tmp/omndi --stub /tmp/libndi_stub.dylib
  ```

## Walkthrough: 3840×1080 Program onto two 1920×1080 displays

1. **Settings ▸ Video**: set *Base (Canvas) Resolution* to `3840x1080`.
   Build your scenes on that canvas.
2. Connect both HDMI displays and set each to its native 1920×1080
   (Windows: *Settings ▸ System ▸ Display*, choose "Extend these displays";
   macOS: *System Settings ▸ Displays*). Avoid "scaled" modes; the editor warns if a
   display is not pixel-exact.
3. Open **Tools ▸ Vexel Output Mapping…**.
4. **＋ Display**: pick the first HDMI display, then **＋ Display** again
   for the second. Each appears in the sidebar with a green *Connected* dot
   and its resolution. Rename them by selecting one and editing *Name* (for
   example `Left` / `Right`).
5. Select **Left**. In **Input Selection**, drag from the top-left corner to
   the middle of the bottom edge. Snapping pulls the edges to `0,0` and
   `1920,1080`. A new slice appears, outlined in its colour, with the
   destination `0,0 1920×1080`: 1:1 on Left.
6. Select **Right** and drag over the right half (`1920,0` → `3840,1080`).
   It becomes Right's slice, also at 1:1. Check the numbers in the inspector:
   - Slice 1: Source `0 0 1920 1080`, Dest `0 0 1920 1080`
   - Slice 2: Source `1920 0 1920 1080`, Dest `0 0 1920 1080`
7. Click **▶ Start Fullscreen** for each display, or **▶ Start All** in
   the toolbar. Once a slice is right, tick **🔒 Locked** so it can't be
   knocked out of place. Each display now shows its half of Program.
   Switch scenes in OBS and both outputs follow, including transitions.
8. Fine-tuning:
   - In **Input Selection**, drag a slice to grab a different area. Its
     destination doesn't move.
   - In **Output Placement**, drag a slice to move it on the display. Its
     crop doesn't change.
   - Use arrow keys to nudge by 1 px (⇧ for 10), the X/Y/W/H fields for
     exact values, **Fit to Display** to fill the display, and **1:1
     Pixels** to restore exact pixel scale.
   - To put several slices on one display, draw more slices while that
     display is selected (or drag them onto it in the sidebar). Order them
     in the list; the top one is drawn on top.
9. Quit OBS whenever you like. Mappings and running outputs are restored on
   the next launch, but only on the same physical displays. If one is
   missing, it shows as **Disconnected** (red dot) and nothing is opened on
   another screen. Reassign it under **Output to** if it was replaced.

Without the hardware, use **＋ Display ▸ Virtual Display…** (`1920x1080`)
and **Open Window** to preview each output in a window, or use **Save
Snapshot…** to check pixels.

## Remote control (obs-websocket)

Vendor name `output-mapping`, called via `CallVendorRequest`:

| Request | Data | Effect |
|---|---|---|
| `GetState` | – | Mapping, per-display status and output state, connected screens, Program size |
| `SetState` | `{state}` | Replace the whole mapping (same JSON as `mappings.json`; rects are `{"x","y","w","h"}` objects) |
| `EditorCapture` | `{path}` | Save a screenshot of the editor; the live previews are rendered through their own GPU path |
| `StartFullscreen` / `OpenWindow` / `StopOutput` | `{displayId}` or `{displayName}` | Per-display output control (identity safety checks apply) |
| `StartNdi` | `{displayId}` or `{displayName}` | Start sending an NDI display (stop it with `StopOutput`) |
| `RenameDisplay` | `{displayName` or `displayId, name}` | Rename a display (a running NDI source is re-announced) |
| `StartAll` / `StopAll` | – | All exactly identified displays and NDI displays / all outputs |
| `Undo` / `Redo` | – | Same as the editor buttons |
| `ListPresets` / `SavePreset` / `LoadPreset` / `DeletePreset` | `{name}` | Layout presets, e.g. recall from a Stream Deck |
| `SetDelay` | `{displayName, delayMs}` | Per-output video delay (0 = live); changes crossfade (0.5 s) |
| `SetIdentify` | `{displayName, mode: pattern / outlines / off}` or `{mode: clear}` | Test pattern / slice outlines on an output |
| `SetSliceLocked` | `{sliceName` or `sliceId, locked}` | Lock or unlock a slice |
| `SetSliceWarp` | `{sliceName, cols, rows[, points: [{x, y}]]}` | Corner pin (2×2) or mesh (up to 9×9); points relative to the output rect; `cols: 0` = off; without points the shape is kept |
| `SetWarpPoint` | `{sliceName, index, x, y}` or `{…, dx, dy}` | Move or nudge one warp point (output pixels) |
| `SelectWarpPoint` | `{sliceName, index}` | Select a point (‑1 = none), as when clicking it in the editor |
| `SetSliceFeather` | `{sliceName, left, right, top, bottom, gamma}` | Soft edges (output pixels) |
| `Snapshot` | `{displayName, path}` | Render exactly what the display receives to `.png` / `.ppm` |
| `OpenEditor` / `CloseEditor` / `Select` | … | Editor control |
| `EditorDrag` / `EditorKey` | … | Automation only: needs `OUTPUT_MAPPING_AUTOMATION=1`. Feeds synthetic mouse and keyboard events to the real editor handlers. |

## Source layout

```
src/core/            geometry.hpp (drag/resize/snap/clip math), display-match.hpp (identity rules), warp.hpp (corner pin / mesh / soft edge); pure C++, unit tested
src/model/           mapping document, JSON, immutable render snapshot shared with the graphics thread
src/render/          GPU compositor (slices, overlays, snapshots, NDI frame readback)
src/output/          NDI sender (loads the NDI Runtime at run time, sends from a worker thread)
src/platform/        display enumeration and fullscreen windows (Windows, macOS .mm, generic Qt fallback)
src/ui/              obs_display widget, output window, interactive preview, editor, theme
src/controller.*     selection, mutations, display status, outputs, persistence
src/plugin-main.cpp  module entry, Tools menu, hotkeys, obs-websocket vendor
tests/               core unit tests
tools/               acceptance test, NDI test + stand-in NDI library, websocket client, test patterns
```

## Verification

See [VERIFICATION.md](VERIFICATION.md) for exactly what was verified on
which setup, and what still needs testing with real hardware.

## Upgrading from "Output Mapping"

Vexel is the same plugin under a new name. The plugin ID and install
folder (`output-mapping`), the download file names
(`output-mapping-<version>-…`), saved mappings and presets, hotkeys and
the obs-websocket vendor name (`output-mapping`) are unchanged, so
updating works exactly as before (replace the `output-mapping` folder, or
run the new `.pkg`) and nothing needs to be set up again.

The GitHub repository was also renamed, from `obs-output-mapping` to
`Vexel`. Old links (including release downloads) redirect to the new
address.

## License

The editor's footer and *Options ▸ About Vexel* show the version
and copyright. OBS's own Plugin Manager also lists the plugin under its name
and version (from `data/manifest.json`; keep its `version` in step with
`buildspec.json`).

Copyright © 2026 Okami Designs. Released under the GNU General Public License v2.0 or later (see [LICENSE](LICENSE)), as required for plugins linking libobs.
