// Standalone unit tests for the pure geometry / display matching core.
// Build & run:  c++ -std=c++17 -I src tests/test_core.cpp -o /tmp/om_tests && /tmp/om_tests

#include "core/geometry.hpp"
#include "core/display-match.hpp"
#include "core/warp.hpp"

#include <cstdio>
#include <cstdlib>

using namespace om;

static int failures = 0;
#define CHECK(cond)                                                              \
	do {                                                                     \
		if (!(cond)) {                                                   \
			std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                              \
		}                                                                \
	} while (0)
#define CHECK_RECT(r, X, Y, W, H) CHECK((r) == (IRect{X, Y, W, H}))

static void test_two_display_example()
{
	/* 3840x1080 program, draw over each half, two 1920x1080 displays. */
	IRect left = rectFromPoints(0, 0, 1920, 1080);
	IRect right = rectFromPoints(3840, 1080, 1920, 0); /* dragged backwards */
	CHECK_RECT(left, 0, 0, 1920, 1080);
	CHECK_RECT(right, 1920, 0, 1920, 1080);
	CHECK_RECT(defaultDestination(left, 1920, 1080), 0, 0, 1920, 1080);
	CHECK_RECT(defaultDestination(right, 1920, 1080), 0, 0, 1920, 1080);

	/* 1:1 mapping clips nothing and keeps scale exactly 1. */
	ClippedSlice c;
	CHECK(clipSliceToCanvas(right, {0, 0, 1920, 1080}, 3840, 1080, c));
	CHECK_RECT(c.src, 1920, 0, 1920, 1080);
	CHECK(c.dx == 0 && c.dy == 0 && c.dw == 1920 && c.dh == 1080);
}

static void test_defaults_and_fit()
{
	CHECK_RECT(defaultDestination({0, 0, 3840, 1080}, 1920, 1080), 0, 0, 1920, 540);
	CHECK_RECT(defaultDestination({10, 10, 400, 300}, 1920, 1080), 0, 0, 400, 300);
	CHECK_RECT(fitToDisplay({0, 0, 1920, 1080}, 1920, 1080), 0, 0, 1920, 1080);
	CHECK_RECT(fitToDisplay({0, 0, 1000, 1000}, 1920, 1080), 420, 0, 1080, 1080);
	CHECK_RECT(fitToDisplay({0, 0, 3840, 1080}, 1920, 1080), 0, 270, 1920, 540);
}

static void test_clip()
{
	ClippedSlice c;
	/* Source hangs 100px off the right edge of a 1920 canvas: the clipped
	 * part is dropped and the destination shrinks to match (scale 2). */
	CHECK(clipSliceToCanvas({1820, 0, 200, 100}, {0, 0, 400, 200}, 1920, 1080, c));
	CHECK_RECT(c.src, 1820, 0, 100, 100);
	CHECK(c.dx == 0 && c.dw == 200 && c.dh == 200);
	/* Entirely outside. */
	CHECK(!clipSliceToCanvas({5000, 0, 10, 10}, {0, 0, 10, 10}, 1920, 1080, c));
	/* Negative origin. */
	CHECK(clipSliceToCanvas({-50, -50, 100, 100}, {100, 100, 100, 100}, 1920, 1080, c));
	CHECK_RECT(c.src, 0, 0, 50, 50);
	CHECK(c.dx == 150 && c.dy == 150 && c.dw == 50 && c.dh == 50);
}

static void test_hit_and_handles()
{
	IRect r{100, 100, 200, 100};
	CHECK(hitTest(r, 100, 100, 5) == Handle::TopLeft);
	CHECK(hitTest(r, 300, 200, 5) == Handle::BottomRight);
	CHECK(hitTest(r, 200, 100, 5) == Handle::Top);
	CHECK(hitTest(r, 150, 150, 5) == Handle::Move);
	CHECK(hitTest(r, 301, 130, 5) == Handle::Right);
	CHECK(hitTest(r, 50, 50, 5) == Handle::None);
}

static void test_move_and_snap()
{
	IRect r{100, 100, 200, 100};
	DragResult m = applyMove(r, 15.4, -20.6, nullptr, 0);
	CHECK_RECT(m.rect, 115, 79, 200, 100);

	SnapTargets t;
	t.xs = {0, 1920};
	t.ys = {0, 1080};
	/* Left edge within 6px of 0 snaps to 0. */
	m = applyMove(r, -96, 0, &t, 8);
	CHECK(m.snappedX && m.rect.x == 0 && m.guideX == 0);
	/* Right edge near 1920 snaps. */
	m = applyMove({1700, 0, 200, 100}, 15, 0, &t, 8);
	CHECK(m.snappedX && m.rect.right() == 1920);
	/* Far away: no snap. */
	m = applyMove(r, 500, 0, &t, 8);
	CHECK(!m.snappedX && m.rect.x == 600);
}

static void test_resize()
{
	IRect r{100, 100, 200, 100};
	DragResult d = applyResize(r, Handle::BottomRight, 50, 20, false, nullptr, 0);
	CHECK_RECT(d.rect, 100, 100, 250, 120);
	d = applyResize(r, Handle::TopLeft, -10, -10, false, nullptr, 0);
	CHECK_RECT(d.rect, 90, 90, 210, 110);
	/* Never flips past min size. */
	d = applyResize(r, Handle::Right, -500, 0, false, nullptr, 0, 4);
	CHECK_RECT(d.rect, 100, 100, 4, 100);
	/* Aspect lock 2:1 from right edge. */
	d = applyResize(r, Handle::Right, 100, 0, true, nullptr, 0);
	CHECK(d.rect.w == 300 && d.rect.h == 150 && d.rect.x == 100);
	/* Aspect lock from corner. */
	d = applyResize(r, Handle::BottomRight, 200, 0, true, nullptr, 0);
	CHECK_RECT(d.rect, 100, 100, 400, 200);
	/* Snap a resized edge. */
	SnapTargets t;
	t.xs = {1920};
	d = applyResize({1700, 0, 200, 100}, Handle::Right, 17, 0, false, &t, 8);
	CHECK(d.snappedX && d.rect.right() == 1920);
	/* Numeric size edit with lock. */
	CHECK_RECT(applySizeEdit({0, 0, 1920, 1080}, 960, 1080, true, true), 0, 0, 960, 540);
	CHECK_RECT(applySizeEdit({0, 0, 1920, 1080}, 1920, 540, false, true), 0, 0, 960, 540);
	CHECK_RECT(applySizeEdit({0, 0, 1920, 1080}, 960, 1080, true, false), 0, 0, 960, 1080);
}

static void test_matching()
{
	DisplayIdentity a{"UUID-A", 0x10ac, 0xa0, 1111, "DELL"};
	DisplayIdentity b{"UUID-B", 0x10ac, 0xa0, 2222, "DELL"};
	DisplayIdentity tv{"UUID-TV", 0x4c2d, 0x10, 0, "Samsung TV"};
	DisplayIdentity tv2{"UUID-TV2", 0x4c2d, 0x10, 0, "Samsung TV"};
	DisplayIdentity builtin{"UUID-BI", 0x610, 0xa, 0, "Built-in"};

	/* exact */
	MatchResult m = matchDisplay(a, {builtin, b, a});
	CHECK(m.status == MatchStatus::Connected && m.index == 2);

	/* UUID changed (different port) but serial proves same unit */
	DisplayIdentity aMoved = a;
	aMoved.uuid = "UUID-A2";
	m = matchDisplay(a, {builtin, aMoved, b});
	CHECK(m.status == MatchStatus::ConnectedSerial && m.index == 1);

	/* Twin of the same model with different serial is NOT the display */
	m = matchDisplay(a, {builtin, b});
	CHECK(m.status == MatchStatus::Disconnected);

	/* TV without serial, UUID gone, one same-model TV present: ambiguous */
	DisplayIdentity tvMoved = tv;
	tvMoved.uuid = "UUID-TV-NEW";
	m = matchDisplay(tv, {builtin, tvMoved});
	CHECK(m.status == MatchStatus::Ambiguous && m.candidates.size() == 1);

	/* Exact UUID still wins even with identical twins present */
	m = matchDisplay(tv, {tv2, tv});
	CHECK(m.status == MatchStatus::Connected && m.index == 1);

	/* Nothing plausible */
	m = matchDisplay(tv, {builtin});
	CHECK(m.status == MatchStatus::Disconnected && m.index == -1);

	/* Duplicate UUIDs reported by OS: refuse */
	m = matchDisplay(a, {a, a});
	CHECK(m.status == MatchStatus::Ambiguous);

	/* No hardware identity (virtual/indirect display): name match is only a
	 * candidate, never "connected" -- even if it is the only display. */
	DisplayIdentity virt{"", 0, 0, 0, "Virtual Display"};
	m = matchDisplay(virt, {builtin, DisplayIdentity{"", 0, 0, 0, "Virtual Display"}});
	CHECK(m.status == MatchStatus::Ambiguous && m.index == -1 && m.candidates.size() == 1);
	m = matchDisplay(virt, {builtin});
	CHECK(m.status == MatchStatus::Disconnected);

	/* Unassigned */
	m = matchDisplay(DisplayIdentity{}, {a});
	CHECK(m.status == MatchStatus::Unassigned);
}

static bool near(double a, double b, double eps = 1e-6)
{
	return std::fabs(a - b) <= eps;
}

static void test_rotation()
{
	CHECK(near(normalizeAngle(190), -170) && near(normalizeAngle(-180), 180) && near(normalizeAngle(540), 180));
	CHECK(normalizeAngle(360) == 0.0 && normalizeAngle(std::nan("")) == 0.0);
	const IRect r{100, 50, 400, 200}; /* centre 300, 150 */
	/* 90° clockwise (y down): the top-left corner goes to the top-right of the turned shape. */
	PointD tl = warpToPixels({0, 0}, r, 90);
	CHECK(near(tl.x, 400) && near(tl.y, -50));
	PointD c = warpToPixels({0.5, 0.5}, r, 37);
	CHECK(near(c.x, 300) && near(c.y, 150)); /* rotates about the centre */
	/* pixelsToWarp undoes warpToPixels at any angle. */
	for (double a : {-135.0, -12.5, 0.0, 33.0, 90.0, 180.0}) {
		PointD n = pixelsToWarp(warpToPixels({0.2, 0.9}, r, a), r, a);
		CHECK(near(n.x, 0.2) && near(n.y, 0.9));
	}
	/* Rotated plain slice: a single quad whose corners are the rotated rect. */
	std::vector<float> t = warpTriangles(Warp{}, r, 90);
	CHECK(t.size() == 24);
	CHECK(near(t[0], 400, 1e-3) && near(t[1], -50, 1e-3) && t[2] == 0.0f && t[3] == 0.0f);
	/* Rotating a warp rotates its outline the same way. */
	Warp cp = identityWarp(2, 2);
	cp.pts[3] = {1.1, 1.2};
	const PointD o = warpToPixels(evalWarp(cp, 1, 1), r, 30);
	const PointD e = rotateAbout(warpToPixels({1.1, 1.2}, r), rectCenter(r), 30);
	CHECK(near(o.x, e.x) && near(o.y, e.y));
}

static void test_warp()
{
	/* Identity grids map every point to itself (corner pin and mesh). */
	for (int n : {2, 3, 5, 9}) {
		Warp w = identityWarp(n, n);
		CHECK(w.active() && isIdentityWarp(w));
		for (double u : {0.0, 0.13, 0.5, 0.77, 1.0})
			for (double v : {0.0, 0.29, 0.5, 1.0}) {
				PointD p = evalWarp(w, u, v);
				CHECK(near(p.x, u) && near(p.y, v));
			}
	}
	CHECK(!Warp{}.active());
	CHECK(identityWarp(1, 20).cols == 2 && identityWarp(1, 20).rows == kMaxWarpGrid);

	/* Corner pin passes exactly through its four corners and is a true
	 * perspective: straight lines stay straight, the centre is where the
	 * diagonals cross (not the average of the corners). */
	Warp cp = identityWarp(2, 2);
	cp.pts = {{0.1, 0.0}, {0.9, 0.2}, {0.0, 1.0}, {1.0, 0.8}};
	CHECK(cp.cornerPin() && !isIdentityWarp(cp));
	PointD c00 = evalWarp(cp, 0, 0), c11 = evalWarp(cp, 1, 1), c10 = evalWarp(cp, 1, 0);
	CHECK(near(c00.x, 0.1) && near(c00.y, 0.0) && near(c11.x, 1.0) && near(c11.y, 0.8) && near(c10.x, 0.9));
	PointD a = evalWarp(cp, 0.25, 0), b = evalWarp(cp, 0.75, 0);
	PointD m = evalWarp(cp, 0.5, 0); /* top edge stays a straight line */
	const double cross = (b.x - a.x) * (m.y - a.y) - (b.y - a.y) * (m.x - a.x);
	CHECK(near(cross, 0.0, 1e-9));
	/* centre == intersection of the diagonals */
	auto inter = [](PointD p1, PointD p2, PointD p3, PointD p4) {
		double d = (p1.x - p2.x) * (p3.y - p4.y) - (p1.y - p2.y) * (p3.x - p4.x);
		double t = ((p1.x - p3.x) * (p3.y - p4.y) - (p1.y - p3.y) * (p3.x - p4.x)) / d;
		return PointD{p1.x + t * (p2.x - p1.x), p1.y + t * (p2.y - p1.y)};
	};
	PointD ctr = evalWarp(cp, 0.5, 0.5), want = inter(cp.pts[0], cp.pts[3], cp.pts[1], cp.pts[2]);
	CHECK(near(ctr.x, want.x, 1e-9) && near(ctr.y, want.y, 1e-9));

	/* Mesh passes through every control point and keeps a moved point. */
	Warp mesh = identityWarp(4, 3);
	mesh.pts[5] = {0.40, 0.62}; /* col 1, row 1 */
	for (int r = 0; r < mesh.rows; r++)
		for (int c = 0; c < mesh.cols; c++) {
			PointD p = evalWarp(mesh, (double)c / (mesh.cols - 1), (double)r / (mesh.rows - 1));
			CHECK(near(p.x, mesh.at(c, r).x, 1e-9) && near(p.y, mesh.at(c, r).y, 1e-9));
		}
	/* Linear (affine) layouts are reproduced exactly between points too. */
	Warp lin = identityWarp(5, 5);
	for (PointD &p : lin.pts)
		p = {0.2 + 0.5 * p.x + 0.1 * p.y, 0.1 + 0.7 * p.y};
	PointD q = evalWarp(lin, 0.37, 0.61);
	CHECK(near(q.x, 0.2 + 0.5 * 0.37 + 0.1 * 0.61, 1e-9) && near(q.y, 0.1 + 0.7 * 0.61, 1e-9));

	/* Resampling keeps the shape: corner pin -> 5x5 mesh -> corner pin. */
	Warp up = resampleWarp(cp, 5, 5);
	CHECK(up.cols == 5 && up.rows == 5);
	PointD u1 = evalWarp(up, 0, 0), u2 = evalWarp(up, 1, 1), u3 = evalWarp(up, 0.5, 0.5);
	CHECK(near(u1.x, 0.1) && near(u2.y, 0.8) && near(u3.x, ctr.x, 1e-9));
	Warp back = resampleWarp(up, 2, 2);
	for (int i = 0; i < 4; i++)
		CHECK(near(back.pts[(size_t)i].x, cp.pts[(size_t)i].x, 1e-9) &&
		      near(back.pts[(size_t)i].y, cp.pts[(size_t)i].y, 1e-9));

	/* Pixels <-> normalised follow the output rect (warp moves with it). */
	IRect r{100, 50, 400, 200};
	PointD px = warpToPixels({0.25, 0.5}, r), nb = pixelsToWarp(px, r);
	CHECK(near(px.x, 200) && near(px.y, 150) && near(nb.x, 0.25) && near(nb.y, 0.5));

	/* Outline + hit test, triangles cover the slice with u,v in 0..1. */
	std::vector<PointD> ol = warpOutline(cp);
	CHECK(ol.size() == 4 && pointInPolygon(ol, 0.5, 0.5) && !pointInPolygon(ol, 0.02, 0.5));
	std::vector<float> tri = warpTriangles(identityWarp(2, 2), {0, 0, 100, 50});
	CHECK(tri.size() % 24 == 0 && !tri.empty());
	float minx = 1e9f, maxx = -1e9f, maxu = 0;
	for (size_t i = 0; i < tri.size(); i += 4) {
		minx = std::min(minx, tri[i]);
		maxx = std::max(maxx, tri[i]);
		maxu = std::max(maxu, tri[i + 2]);
	}
	CHECK(near(minx, 0) && near(maxx, 100) && near(maxu, 1));

	Feather f;
	CHECK(!f.active());
	f.right = 120;
	CHECK(f.active());
}

int main()
{
	test_warp();
	test_rotation();
	test_two_display_example();
	test_defaults_and_fit();
	test_clip();
	test_hit_and_handles();
	test_move_and_snap();
	test_resize();
	test_matching();
	if (failures) {
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("All core tests passed\n");
	return 0;
}
