# Release hardware checklist

A short, manual check of a release on real hardware, before announcing it.
About 20 minutes. Record each result in the table (and in
[VERIFICATION.md](../VERIFICATION.md)) so the release notes can say what
was checked. Fuller plans: [windows-test-plan.md](windows-test-plan.md) and
[hardware-test-checklist.md](hardware-test-checklist.md) (macOS).

**Release under test:** `v_____-beta` · **OS / build:** ________ ·
**OBS:** 33.0.1 · **GPU:** ________ · **Displays:** ________ · **Date:** ________

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | **Install the exact release.** Quit OBS. Download the asset from that release's page (not an Actions build). Windows: replace `C:\ProgramData\obs-studio\plugins\output-mapping`. macOS: run the `.pkg`. Start OBS, open *Options ▸ About Vexel*. | About shows the release's version; *Help ▸ Log Files ▸ View Current Log* has `[output-mapping] plugin loaded successfully (version X.Y.Z)` with the same number | ☐ |
| 2 | **Two slices to two physical displays.** Canvas 3840×1080, both displays at native 1920×1080, Windows "Extend". Add both displays, draw a slice over each half, Start Fullscreen on both. Turn on *Test Pattern All*. | Each display shows only its half; red + white edge lines visible on all four sides of each display; no taskbar/menu bar or cursor on the outputs. Turn the pattern off: Program halves meet at the seam with no doubled or missing column | ☐ |
| 3 | **Unplug and reconnect a display** while both outputs run. | OBS keeps running. The unplugged output stops and does not appear on another screen; status *Disconnected*. The other output keeps running. Plug it back into the **same** port: it shows *Connected* and the output resumes by itself | ☐ |
| 4 | **Resize the dock on Windows.** Drag the Vexel dock's edges quickly, make it very small and large, float it and dock it again a few times. | No flicker storm, no crash; thumbnails keep updating; outputs unaffected | ☐ |
| 5 | **Restart OBS with outputs running.** Quit OBS with both outputs live, then start it again. | Both outputs come back on the correct displays about 1.5 s after OBS loads; mappings unchanged. Then quit, unplug display 2, start OBS: display 2 shows *Disconnected* and nothing opens on another screen | ☐ |

If anything fails, save the log (*Help ▸ Log Files ▸ Upload Current Log
File*) and open an issue:
https://github.com/Jallison154/Vexel/issues

## Results still to record

As of 2026-11-16 these have **no recorded result** for any release:

- Windows, 0.6.4 or later: step 3 (the unplug crash fix from 0.6.4 has not
  been re-tested on the show PC).
- Windows, 0.7.0 or later: NDI to a real receiver on the network.
- Windows, 0.8.0: projection mapping (corner pin on a real projector, soft
  edges between two overlapping projectors). The warp shader has only run
  on macOS (OpenGL).
- Windows: step 4 has a fix from 0.5.1 tested only on macOS with Windows-style
  scrollbars forced.
- macOS: steps 2, 3 and 5 with external displays (only virtual windows and
  the built-in screen have been tested).
- Which Vexel version(s) were used at the live shows reported as solid.
