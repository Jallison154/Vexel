/*
Output Mapping for OBS Studio
Copyright (C) 2026 Okami Designs

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Projection mapping math: corner pin, mesh warp and soft-edge settings for
// a slice. Pure C++ (no OBS / Qt) so the GPU renderer, the test pattern,
// the outlines and the editor handles all use exactly the same shape, and
// it can be unit tested standalone (see tests/).
//
// A warp is a grid of control points stored RELATIVE to the slice's output
// rectangle (0..1 across it; points may go outside), so moving or resizing
// the slice carries its warp along.
//   2 x 2 grid  -> corner pin with true perspective (homography)
//   larger grid -> mesh warp, smooth (Catmull-Rom) through every point

#pragma once

#include "core/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace om {

static constexpr int kMaxWarpGrid = 9;

struct Warp {
	int cols = 0, rows = 0;  /* 0 = no warp */
	std::vector<PointD> pts; /* row-major, top row first; normalised to the output rect */

	bool active() const
	{
		return cols >= 2 && rows >= 2 && cols <= kMaxWarpGrid && rows <= kMaxWarpGrid &&
		       pts.size() == (size_t)cols * (size_t)rows;
	}
	bool cornerPin() const { return active() && cols == 2 && rows == 2; }
	int count() const { return active() ? cols * rows : 0; }
	const PointD &at(int c, int r) const { return pts[(size_t)r * (size_t)cols + (size_t)c]; }
	bool operator==(const Warp &o) const
	{
		if (cols != o.cols || rows != o.rows || pts.size() != o.pts.size())
			return false;
		for (size_t i = 0; i < pts.size(); i++)
			if (pts[i].x != o.pts[i].x || pts[i].y != o.pts[i].y)
				return false;
		return true;
	}
	bool operator!=(const Warp &o) const { return !(*this == o); }
};

/* Soft edges (projector blending), in output pixels of the slice's rect. */
struct Feather {
	int left = 0, right = 0, top = 0, bottom = 0;
	double gamma = 1.0; /* blend curve exponent; 1.0 = sums to constant light */

	bool active() const { return left > 0 || right > 0 || top > 0 || bottom > 0; }
	bool operator==(const Feather &o) const
	{
		return left == o.left && right == o.right && top == o.top && bottom == o.bottom && gamma == o.gamma;
	}
	bool operator!=(const Feather &o) const { return !(*this == o); }
};

/* Even grid: the unwarped rectangle. */
inline Warp identityWarp(int cols, int rows)
{
	Warp w;
	w.cols = std::clamp(cols, 2, kMaxWarpGrid);
	w.rows = std::clamp(rows, 2, kMaxWarpGrid);
	w.pts.reserve((size_t)w.cols * (size_t)w.rows);
	for (int r = 0; r < w.rows; r++)
		for (int c = 0; c < w.cols; c++)
			w.pts.push_back({(double)c / (w.cols - 1), (double)r / (w.rows - 1)});
	return w;
}

inline bool isIdentityWarp(const Warp &w, double eps = 1e-9)
{
	if (!w.active())
		return true;
	for (int r = 0; r < w.rows; r++)
		for (int c = 0; c < w.cols; c++) {
			const PointD &p = w.at(c, r);
			if (std::fabs(p.x - (double)c / (w.cols - 1)) > eps ||
			    std::fabs(p.y - (double)r / (w.rows - 1)) > eps)
				return false;
		}
	return true;
}

/* Projective map of the unit square onto quad p00 (0,0), p10 (1,0),
 * p01 (0,1), p11 (1,1) (Heckbert's square-to-quad). */
struct Homography {
	double a = 1, b = 0, c = 0, d = 0, e = 1, f = 0, g = 0, h = 0;

	PointD map(double u, double v) const
	{
		const double w = g * u + h * v + 1.0;
		if (std::fabs(w) < 1e-12)
			return {a * u + b * v + c, d * u + e * v + f};
		return {(a * u + b * v + c) / w, (d * u + e * v + f) / w};
	}
};

inline Homography squareToQuad(const PointD &p00, const PointD &p10, const PointD &p01, const PointD &p11)
{
	Homography H;
	const double sx = p00.x - p10.x + p11.x - p01.x;
	const double sy = p00.y - p10.y + p11.y - p01.y;
	if (std::fabs(sx) < 1e-12 && std::fabs(sy) < 1e-12) {
		/* Parallelogram: affine. */
		H.a = p10.x - p00.x;
		H.b = p01.x - p00.x;
		H.c = p00.x;
		H.d = p10.y - p00.y;
		H.e = p01.y - p00.y;
		H.f = p00.y;
		return H;
	}
	const double dx1 = p10.x - p11.x, dx2 = p01.x - p11.x;
	const double dy1 = p10.y - p11.y, dy2 = p01.y - p11.y;
	const double den = dx1 * dy2 - dx2 * dy1;
	if (std::fabs(den) < 1e-12)
		return squareToQuad(p00, p10, p01, {p10.x + p01.x - p00.x, p10.y + p01.y - p00.y}); /* degenerate */
	H.g = (sx * dy2 - dx2 * sy) / den;
	H.h = (dx1 * sy - sx * dy1) / den;
	H.a = p10.x - p00.x + H.g * p10.x;
	H.b = p01.x - p00.x + H.h * p01.x;
	H.c = p00.x;
	H.d = p10.y - p00.y + H.g * p10.y;
	H.e = p01.y - p00.y + H.h * p01.y;
	H.f = p00.y;
	return H;
}

inline double catmullRom(double p0, double p1, double p2, double p3, double t)
{
	const double t2 = t * t, t3 = t2 * t;
	return 0.5 * ((2.0 * p1) + (-p0 + p2) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
		      (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

/* Position (normalised to the output rect) of slice point (u, v) in 0..1. */
inline PointD evalWarp(const Warp &w, double u, double v)
{
	if (!w.active())
		return {u, v};
	if (w.cornerPin())
		return squareToQuad(w.at(0, 0), w.at(1, 0), w.at(0, 1), w.at(1, 1)).map(u, v);

	/* Grid point, linearly extrapolated one step past the edges so the
	 * spline stays natural at the border. */
	auto at = [&](int c, int r) {
		return w.at(c, r);
	};
	auto extR = [&](int c, int r) -> PointD { /* c in range */
						  if (r < 0) {
							  const PointD a = at(c, 0), b = at(c, 1);
							  return {2 * a.x - b.x, 2 * a.y - b.y};
						  }
						  if (r >= w.rows) {
							  const PointD a = at(c, w.rows - 1), b = at(c, w.rows - 2);
							  return {2 * a.x - b.x, 2 * a.y - b.y};
						  }
						  return at(c, r);
	};
	auto P = [&](int c, int r) -> PointD {
		if (c < 0) {
			const PointD a = extR(0, r), b = extR(1, r);
			return {2 * a.x - b.x, 2 * a.y - b.y};
		}
		if (c >= w.cols) {
			const PointD a = extR(w.cols - 1, r), b = extR(w.cols - 2, r);
			return {2 * a.x - b.x, 2 * a.y - b.y};
		}
		return extR(c, r);
	};
	const double gu = std::clamp(u, 0.0, 1.0) * (w.cols - 1), gv = std::clamp(v, 0.0, 1.0) * (w.rows - 1);
	const int ci = std::min((int)std::floor(gu), w.cols - 2), ri = std::min((int)std::floor(gv), w.rows - 2);
	const double tu = gu - ci, tv = gv - ri;
	double rx[4], ry[4];
	for (int k = 0; k < 4; k++) {
		const int r = ri - 1 + k;
		const PointD a = P(ci - 1, r), b = P(ci, r), c = P(ci + 1, r), d = P(ci + 2, r);
		rx[k] = catmullRom(a.x, b.x, c.x, d.x, tu);
		ry[k] = catmullRom(a.y, b.y, c.y, d.y, tu);
	}
	return {catmullRom(rx[0], rx[1], rx[2], rx[3], tv), catmullRom(ry[0], ry[1], ry[2], ry[3], tv)};
}

/* Same shape on a different grid (switching corner pin <-> mesh sizes keeps
 * the current shape as closely as the new grid allows). */
inline Warp resampleWarp(const Warp &w, int cols, int rows)
{
	Warp out = identityWarp(cols, rows);
	if (!w.active())
		return out;
	for (int r = 0; r < out.rows; r++)
		for (int c = 0; c < out.cols; c++)
			out.pts[(size_t)r * out.cols + c] =
				evalWarp(w, (double)c / (out.cols - 1), (double)r / (out.rows - 1));
	return out;
}

/* Tessellation density: segments across the whole slice in u and v. */
inline int warpSegments(const Warp &w, int along)
{
	if (!w.active())
		return 1;
	if (w.cornerPin())
		return 32; /* perspective is exact at the vertices; 32 keeps interpolation error invisible */
	return std::clamp((along - 1) * 12, 12, 96);
}

/* Outline of the warped slice, normalised, clockwise from the top-left. */
inline std::vector<PointD> warpOutline(const Warp &w, int perEdge = 32)
{
	std::vector<PointD> out;
	if (!w.active()) {
		out = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
		return out;
	}
	const int n = w.cornerPin() ? 1 : std::max(2, perEdge);
	for (int i = 0; i < n; i++)
		out.push_back(evalWarp(w, (double)i / n, 0));
	for (int i = 0; i < n; i++)
		out.push_back(evalWarp(w, 1, (double)i / n));
	for (int i = 0; i < n; i++)
		out.push_back(evalWarp(w, 1.0 - (double)i / n, 1));
	for (int i = 0; i < n; i++)
		out.push_back(evalWarp(w, 0, 1.0 - (double)i / n));
	return out;
}

/* Normalised (relative to rect) <-> output pixels. */
inline PointD warpToPixels(const PointD &n, const IRect &r)
{
	return {r.x + n.x * r.w, r.y + n.y * r.h};
}
inline PointD pixelsToWarp(const PointD &p, const IRect &r)
{
	return {r.w > 0 ? (p.x - r.x) / r.w : 0.0, r.h > 0 ? (p.y - r.y) / r.h : 0.0};
}

inline bool pointInPolygon(const std::vector<PointD> &poly, double x, double y)
{
	bool in = false;
	for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
		const PointD &a = poly[i], &b = poly[j];
		if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)
			in = !in;
	}
	return in;
}

/* Triangle list for the GPU: per vertex x, y (output px) and u, v (0..1
 * across the slice). Covers the slice with segU x segV quads. */
inline std::vector<float> warpTriangles(const Warp &w, const IRect &dst)
{
	const int su = w.active() ? warpSegments(w, w.cols) : 1;
	const int sv = w.active() ? warpSegments(w, w.rows) : 1;
	std::vector<PointD> grid((size_t)(su + 1) * (size_t)(sv + 1));
	for (int j = 0; j <= sv; j++)
		for (int i = 0; i <= su; i++)
			grid[(size_t)j * (su + 1) + i] = warpToPixels(evalWarp(w, (double)i / su, (double)j / sv), dst);
	std::vector<float> out;
	out.reserve((size_t)su * sv * 6 * 4);
	auto put = [&](int i, int j) {
		const PointD &p = grid[(size_t)j * (su + 1) + i];
		out.push_back((float)p.x);
		out.push_back((float)p.y);
		out.push_back((float)i / su);
		out.push_back((float)j / sv);
	};
	for (int j = 0; j < sv; j++)
		for (int i = 0; i < su; i++) {
			put(i, j);
			put(i + 1, j);
			put(i, j + 1);
			put(i + 1, j);
			put(i + 1, j + 1);
			put(i, j + 1);
		}
	return out;
}

} // namespace om
