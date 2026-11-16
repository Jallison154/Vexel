/*
Output Mapping for OBS Studio
Copyright (C) 2026 Okami Designs

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// CPU-drawn identify overlays for an output (UI thread, QPainter):
//  - test pattern: a calibration image (edges, grid, rulers, corners,
//    centre, name/resolution) drawn at each slice's own size and position;
//    the whole output when it has no slices
//  - outlines: transparent overlay with slice outlines and labels

#pragma once

#include "model/mapping.hpp"

#include <QImage>

namespace om {

QImage makeTestPattern(const DisplayMapping &d); /* leaves warped / soft-edged slices to makeSlicePattern */
/* Pattern for one slice at its output size, before warping (the renderer
 * draws it through the slice's warp and soft edges). */
QImage makeSlicePattern(const Slice &s);
QImage makeOutlineOverlay(const DisplayMapping &d);

} // namespace om
