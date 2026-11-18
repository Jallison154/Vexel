/*
Output Mapping for OBS Studio
Copyright (C) 2026 Okami Designs

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "ui/preview-view.hpp"

#include "controller.hpp"
#include "render/compositor.hpp"

#include <obs.h>

#include <QApplication>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <cmath>
#include <unordered_set>

namespace om {

static constexpr uint32_t kBackground = 0xFF0C1322;
static constexpr uint32_t kCanvasBorder = 0x55FFFFFF;
static constexpr uint32_t kGuide = 0xFFFF3DCB;
static constexpr double kMinZoom = 0.02, kMaxZoom = 32.0;

static uint32_t argb(const QColor &c, int alpha = 255)
{
	return ((uint32_t)alpha << 24) | ((uint32_t)c.red() << 16) | ((uint32_t)c.green() << 8) | (uint32_t)c.blue();
}

static uint32_t withAlpha(uint32_t c, int alpha)
{
	return (c & 0x00FFFFFF) | ((uint32_t)alpha << 24);
}

PreviewView::PreviewView(Kind kind, QWidget *parent) : ObsDisplayWidget(parent), kind_(kind)
{
	setFocusPolicy(Qt::StrongFocus);
	setBackground(0x22130C); /* BGR of kBackground */
	setMinimumSize(240, 160);
	setCursor(kind_ == Kind::Input ? Qt::CrossCursor : Qt::ArrowCursor);
	grabGesture(Qt::PinchGesture);

	Controller *c = Controller::instance();
	connect(c, &Controller::documentChanged, this, [this](Change) { refresh(); });
	connect(c, &Controller::selectionChanged, this, [this]() {
		if (kind_ == Kind::Output && autoFit_)
			fitToView();
		refresh();
	});
	connect(c, &Controller::programSizeChanged, this, [this]() {
		if (autoFit_)
			fitToView();
		refresh();
	});
	refresh();
}

PreviewView::~PreviewView()
{
	destroyDisplay(); /* no more renderFrame calls after this */
	if (!labelCache_.empty()) {
		obs_enter_graphics();
		for (auto &kv : labelCache_)
			gs_texture_destroy(kv.second);
		obs_leave_graphics();
		labelCache_.clear();
	}
}

/* ---------------------------------------------------------------------- */
/* Model access                                                            */

void PreviewView::canvasSize(int &w, int &h) const
{
	Controller *c = Controller::instance();
	if (kind_ == Kind::Input) {
		c->programSize(w, h);
		return;
	}
	const DisplayMapping *d = c->doc().display(c->selectedDisplay());
	w = d ? d->width : 0;
	h = d ? d->height : 0;
}

std::vector<PreviewView::Item> PreviewView::items() const
{
	Controller *c = Controller::instance();
	const MappingDocument &doc = c->doc();
	const QString selD = c->selectedDisplay(), selS = c->selectedSlice();
	std::vector<Item> out; /* hit-test order: top-most first */

	auto add = [&](const DisplayMapping &d, const Slice &s) {
		Item it;
		it.sliceId = s.id;
		it.displayId = d.id;
		it.rect = kind_ == Kind::Input ? s.src : s.dst;
		it.color = argb(s.color);
		it.selected = s.id == selS;
		it.dim = (kind_ == Kind::Input && d.id != selD) || !s.enabled;
		it.locked = s.locked;
		if (kind_ == Kind::Output) {
			it.rot = s.rotation;
			if (s.warp.active()) {
				it.cols = s.warp.cols;
				it.rows = s.warp.rows;
				for (const PointD &n : s.warp.pts)
					it.points.push_back(warpToPixels(n, s.dst, s.rotation));
			}
			if ((s.warp.active() && !isIdentityWarp(s.warp)) || s.rotation != 0.0) {
				it.warped = true;
				for (const PointD &n : warpOutline(s.warp, 48))
					it.outline.push_back(warpToPixels(n, s.dst, s.rotation));
			}
		}
		out.push_back(it);
	};

	if (kind_ == Kind::Input) {
		/* Selected display's slices first, then all others (dimmed). */
		if (const DisplayMapping *d = doc.display(selD))
			for (const Slice &s : d->slices)
				add(*d, s);
		for (const DisplayMapping &d : doc.displays)
			if (d.id != selD)
				for (const Slice &s : d.slices)
					add(d, s);
	} else if (const DisplayMapping *d = doc.display(selD)) {
		for (const Slice &s : d->slices)
			add(*d, s);
	}

	/* Selected slice always hit-tests first. */
	std::stable_partition(out.begin(), out.end(), [](const Item &i) { return i.selected; });
	return out;
}

IRect PreviewView::sliceRect(const QString &sliceId) const
{
	const Slice *s = Controller::instance()->doc().slice(sliceId);
	if (!s)
		return {};
	return kind_ == Kind::Input ? s->src : s->dst;
}

void PreviewView::applyRect(const QString &sliceId, const IRect &r)
{
	if (kind_ == Kind::Input)
		Controller::instance()->setSliceSrc(sliceId, r); /* destination stays fixed */
	else
		Controller::instance()->setSliceDst(sliceId, r); /* source crop stays fixed */
}

SnapTargets PreviewView::snapTargets(const QString &exclude) const
{
	SnapTargets t;
	int w = 0, h = 0;
	canvasSize(w, h);
	t.xs = {0.0, (double)w, w / 2.0};
	t.ys = {0.0, (double)h, h / 2.0};
	for (const Item &it : items()) {
		if (it.sliceId == exclude)
			continue;
		/* Input: every slice's crop (any display); Output: same display. */
		t.xs.push_back(it.rect.x);
		t.xs.push_back(it.rect.right());
		t.ys.push_back(it.rect.y);
		t.ys.push_back(it.rect.bottom());
	}
	return t;
}

/* ---------------------------------------------------------------------- */
/* View transform                                                          */

QPointF PreviewView::canvasToWidget(const QPointF &c) const
{
	double dpr = devicePixelRatioF();
	return QPointF((c.x() * zoom_ + panX_) / dpr, (c.y() * zoom_ + panY_) / dpr);
}

QPointF PreviewView::widgetToCanvas(const QPointF &w) const
{
	double dpr = devicePixelRatioF();
	return QPointF((w.x() * dpr - panX_) / zoom_, (w.y() * dpr - panY_) / zoom_);
}

QPointF PreviewView::eventCanvasPos(const QPointF &widgetPos) const
{
	return widgetToCanvas(widgetPos);
}

void PreviewView::fitToView()
{
	int cw = 0, ch = 0;
	canvasSize(cw, ch);
	QSize px = pixelSize();
	if (cw <= 0 || ch <= 0 || px.width() <= 0 || px.height() <= 0) {
		zoom_ = 1;
		panX_ = panY_ = 0;
		autoFit_ = true;
		refresh();
		return;
	}
	/* Output Placement: room above the canvas for a full-size slice's rotation handle. */
	double margin = (kind_ == Kind::Output ? 34 : 18) * devicePixelRatioF();
	double z = std::min((px.width() - 2 * margin) / cw, (px.height() - 2 * margin) / ch);
	zoom_ = std::clamp(z, kMinZoom, kMaxZoom);
	panX_ = (px.width() - cw * zoom_) / 2.0;
	panY_ = (px.height() - ch * zoom_) / 2.0;
	autoFit_ = true;
	emit zoomChanged(zoom_);
	refresh();
}

void PreviewView::zoomActual()
{
	/* 100% = one canvas pixel per physical screen pixel. */
	QSize px = pixelSize();
	setZoomAt(1.0, QPointF(px.width() / 2.0, px.height() / 2.0));
}

void PreviewView::zoomBy(double factor)
{
	QSize px = pixelSize();
	setZoomAt(zoom_ * factor, QPointF(px.width() / 2.0, px.height() / 2.0));
}

void PreviewView::setZoomAt(double z, const QPointF &a)
{
	z = std::clamp(z, kMinZoom, kMaxZoom);
	double cx = (a.x() - panX_) / zoom_, cy = (a.y() - panY_) / zoom_;
	zoom_ = z;
	panX_ = a.x() - cx * zoom_;
	panY_ = a.y() - cy * zoom_;
	autoFit_ = false;
	clampPan();
	emit zoomChanged(zoom_);
	refresh();
}

void PreviewView::clampPan()
{
	/* Keep at least a sliver of the canvas visible. */
	int cw = 0, ch = 0;
	canvasSize(cw, ch);
	QSize px = pixelSize();
	double keep = 40 * devicePixelRatioF();
	panX_ = std::clamp(panX_, keep - cw * zoom_, px.width() - keep);
	panY_ = std::clamp(panY_, keep - ch * zoom_, px.height() - keep);
}

/* ---------------------------------------------------------------------- */
/* Overlay snapshot (UI thread)                                            */

QImage PreviewView::makeLabel(const QString &text, const QColor &color, bool selected) const
{
	double dpr = devicePixelRatioF();
	QFont f = QApplication::font();
	f.setPixelSize((int)std::lround(11 * dpr));
	f.setWeight(selected ? QFont::DemiBold : QFont::Medium);
	QFontMetrics fm(f);
	int padX = (int)(6 * dpr), padY = (int)(3 * dpr);
	int w = fm.horizontalAdvance(text) + 2 * padX;
	int h = fm.height() + 2 * padY;
	QImage img(w, h, QImage::Format_RGBA8888_Premultiplied);
	img.fill(Qt::transparent);
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);
	QPainterPath path;
	path.addRoundedRect(QRectF(0, 0, w, h), 3 * dpr, 3 * dpr);
	QColor bg = color;
	bg.setAlpha(selected ? 255 : 215);
	p.fillPath(path, bg);
	double lum = 0.299 * color.redF() + 0.587 * color.greenF() + 0.114 * color.blueF();
	p.setPen(lum > 0.55 ? QColor(18, 20, 24) : QColor(255, 255, 255));
	p.setFont(f);
	p.drawText(QRect(padX, padY, w - 2 * padX, h - 2 * padY), Qt::AlignVCenter | Qt::AlignLeft, text);
	p.end();
	return img;
}

void PreviewView::refresh()
{
	Controller *c = Controller::instance();
	if (!c)
		return;
	Overlay ov;
	canvasSize(ov.canvasW, ov.canvasH);
	ov.zoom = zoom_;
	ov.panX = panX_;
	ov.panY = panY_;
	ov.dpr = devicePixelRatioF();
	ov.displayId = c->selectedDisplay().toStdString();

	std::vector<Item> its = items();
	/* Draw bottom-most first: reverse hit order. */
	for (auto it = its.rbegin(); it != its.rend(); ++it) {
		const Slice *s = c->doc().slice(it->sliceId);
		if (!s)
			continue;
		OverlayItem oi;
		oi.rect = it->rect;
		oi.color = it->color;
		oi.selected = it->selected;
		oi.dim = it->dim;
		oi.locked = it->locked;
		oi.hovered = it->sliceId == hoverSlice_;
		oi.warped = it->warped;
		oi.outline = it->outline;
		oi.points = it->points;
		oi.cols = it->cols;
		oi.rows = it->rows;
		oi.selPoint = it->selected ? c->selectedWarpPoint() : -1;
		oi.rot = it->rot;
		oi.rotHandle = kind_ == Kind::Output && it->selected && !it->locked;
		QString text = QStringLiteral("%1%2  %3×%4")
				       .arg(it->locked ? QStringLiteral("🔒 ") : QString())
				       .arg(s->name)
				       .arg(it->rect.w)
				       .arg(it->rect.h);
		oi.labelKey = QStringLiteral("%1|%2|%3|%4")
				      .arg(text, s->color.name())
				      .arg(it->selected)
				      .arg(ov.dpr)
				      .toStdString();
		oi.label = makeLabel(text, s->color, it->selected);
		ov.items.push_back(std::move(oi));
	}

	ov.guideX = guideX_;
	ov.guideY = guideY_;
	ov.guideXPos = guideXPos_;
	ov.guideYPos = guideYPos_;
	ov.rubber = drag_ == Drag::Create && !rubber_.empty();
	ov.rubberRect = rubber_;

	QString center;
	if (kind_ == Kind::Output && c->doc().displays.empty())
		center = QStringLiteral("Add a display (＋ Display) to place slices");
	else if (kind_ == Kind::Output && ov.canvasW == 0)
		center = QStringLiteral("Select a display");
	else if (kind_ == Kind::Input && ov.canvasW == 0)
		center = QStringLiteral("Waiting for OBS Program…");
	else if (kind_ == Kind::Input && c->doc().displays.empty())
		center = QStringLiteral("Add a display, then drag on the Program image to draw a slice");
	else if (const DisplayMapping *sel = c->doc().display(c->selectedDisplay()); sel && sel->slices.empty())
		center = kind_ == Kind::Input
				 ? QStringLiteral("Drag on the image to draw a slice for “%1”").arg(sel->name)
				 : QStringLiteral("No slices on “%1” yet. Draw one in Input Selection").arg(sel->name);
	if (!center.isEmpty()) {
		ov.centerKey = (QStringLiteral("center|") + center + QString::number(ov.dpr)).toStdString();
		ov.centerLabel = makeLabel(center, QColor(0x2A, 0x2E, 0x37), false);
	}

	std::lock_guard<std::mutex> lock(overlayMutex_);
	overlay_ = std::move(ov);
}

/* ---------------------------------------------------------------------- */
/* Rendering (graphics thread)                                             */

void PreviewView::renderFrame(uint32_t cx, uint32_t cy)
{
	Overlay ov;
	{
		std::lock_guard<std::mutex> lock(overlayMutex_);
		ov = overlay_;
	}

	gs_viewport_push();
	gs_projection_push();
	gs_matrix_push();
	gs_matrix_identity();
	gfx::setScreenView(cx, cy);

	gfx::fillRect(0, 0, (float)cx, (float)cy, kBackground);

	if (ov.canvasW > 0 && ov.canvasH > 0) {
		double sx = ov.panX, sy = ov.panY, sw = ov.canvasW * ov.zoom, sh = ov.canvasH * ov.zoom;
		gfx::fillRect((float)sx, (float)sy, (float)sw, (float)sh, 0xFF000000);

		/* Clip video to the visible part of the canvas via viewport. */
		int vx0 = std::max(0, (int)std::floor(sx)), vy0 = std::max(0, (int)std::floor(sy));
		int vx1 = std::min((int)cx, (int)std::ceil(sx + sw)), vy1 = std::min((int)cy, (int)std::ceil(sy + sh));
		if (vx1 > vx0 && vy1 > vy0) {
			QRectF visible((vx0 - ov.panX) / ov.zoom, (vy0 - ov.panY) / ov.zoom, (vx1 - vx0) / ov.zoom,
				       (vy1 - vy0) / ov.zoom);
			gs_viewport_push();
			gs_projection_push();
			gfx::setCanvasView(visible, vx0, vy0, vx1 - vx0, vy1 - vy0);
			gfx::ProgramFrame pf;
			if (gfx::acquireProgram(pf)) {
				if (kind_ == Kind::Input) {
					gfx::drawProgram(pf, ov.zoom >= 2.0);
				} else {
					RenderDisplay rd;
					if (RenderStore::instance().getDisplay(ov.displayId, rd))
						gfx::drawDisplayLive(pf, rd); /* incl. test pattern */
				}
			}
			gs_projection_pop();
			gs_viewport_pop();
			gfx::setScreenView(cx, cy);
		}
	}

	drawOverlay(ov, cx, cy);

	gs_matrix_pop();
	gs_projection_pop();
	gs_viewport_pop();
}

void PreviewView::drawOverlay(const Overlay &ov, uint32_t cx, uint32_t cy)
{
	const float d = (float)ov.dpr;
	auto toScreen = [&](const IRect &r, float &x, float &y, float &w, float &h) {
		x = (float)(r.x * ov.zoom + ov.panX);
		y = (float)(r.y * ov.zoom + ov.panY);
		w = (float)(r.w * ov.zoom);
		h = (float)(r.h * ov.zoom);
	};

	std::unordered_set<std::string> used;
	auto label = [&](const std::string &key, const QImage &img) -> gs_texture_t * {
		if (img.isNull())
			return nullptr;
		used.insert(key);
		auto it = labelCache_.find(key);
		if (it != labelCache_.end())
			return it->second;
		gs_texture_t *t = gfx::createTextureFromImage(img);
		labelCache_[key] = t;
		return t;
	};

	if (ov.canvasW > 0 && ov.canvasH > 0) {
		float x, y, w, h;
		toScreen({0, 0, ov.canvasW, ov.canvasH}, x, y, w, h);
		gfx::strokeRect(x, y, w, h, 1.0f * d, kCanvasBorder);
	}

	auto scr = [&](const PointD &p) {
		return PointD{p.x * ov.zoom + ov.panX, p.y * ov.zoom + ov.panY};
	};
	for (const OverlayItem &it : ov.items) {
		float x, y, w, h;
		toScreen(it.rect, x, y, w, h);
		int strokeA = it.dim ? 175 : 255;
		int fillA = it.selected ? 46 : (it.hovered ? 34 : (it.dim ? 0 : 18));
		float t = (it.selected ? 2.0f : (it.hovered ? 1.5f : 1.0f)) * d;
		if (it.warped) {
			/* Projection-mapped: the real (warped) outline instead of the rect. */
			for (size_t i = 0; i < it.outline.size(); i++) {
				const PointD a = scr(it.outline[i]), b = scr(it.outline[(i + 1) % it.outline.size()]);
				gfx::drawLine((float)a.x, (float)a.y, (float)b.x, (float)b.y, t,
					      withAlpha(it.color, strokeA));
			}
			if (it.selected && !it.points.empty()) {
				/* faint (rotated) slice rectangle, for reference */
				const PointD c = rectCenter(it.rect);
				const PointD cs[4] = {{(double)it.rect.x, (double)it.rect.y},
						      {(double)it.rect.right(), (double)it.rect.y},
						      {(double)it.rect.right(), (double)it.rect.bottom()},
						      {(double)it.rect.x, (double)it.rect.bottom()}};
				for (int i = 0; i < 4; i++) {
					const PointD a = scr(rotateAbout(cs[i], c, it.rot));
					const PointD b = scr(rotateAbout(cs[(i + 1) % 4], c, it.rot));
					gfx::drawLine((float)a.x, (float)a.y, (float)b.x, (float)b.y, 1.0f * d,
						      withAlpha(it.color, 70));
				}
			}
		} else {
			if (fillA)
				gfx::fillRect(x, y, w, h, withAlpha(it.color, fillA));
			gfx::strokeRect(x, y, w, h, t, withAlpha(it.color, strokeA));
		}
		if (it.selected && !it.points.empty()) {
			/* Mesh lines between control points, then the points. */
			if (it.cols > 2 || it.rows > 2) {
				for (int r = 0; r < it.rows; r++)
					for (int cc = 0; cc < it.cols; cc++) {
						const PointD a = scr(it.points[(size_t)(r * it.cols + cc)]);
						if (cc + 1 < it.cols) {
							const PointD b = scr(it.points[(size_t)(r * it.cols + cc + 1)]);
							gfx::drawLine((float)a.x, (float)a.y, (float)b.x, (float)b.y,
								      1.0f * d, withAlpha(it.color, 150));
						}
						if (r + 1 < it.rows) {
							const PointD b =
								scr(it.points[(size_t)((r + 1) * it.cols + cc)]);
							gfx::drawLine((float)a.x, (float)a.y, (float)b.x, (float)b.y,
								      1.0f * d, withAlpha(it.color, 150));
						}
					}
			}
			for (size_t i = 0; i < it.points.size(); i++) {
				const PointD p = scr(it.points[i]);
				const bool sel = (int)i == it.selPoint;
				const float hs = (sel ? 12.0f : 9.0f) * d;
				gfx::fillRect((float)p.x - hs / 2, (float)p.y - hs / 2, hs, hs,
					      sel ? withAlpha(it.color, 255) : 0xFFFFFFFF);
				gfx::strokeRect((float)p.x - hs / 2, (float)p.y - hs / 2, hs, hs,
						(sel ? 2.0f : 1.0f) * d, sel ? 0xFFFFFFFF : withAlpha(it.color, 255));
			}
		}

		gs_texture_t *tex = label(it.labelKey, it.label);
		if (tex && !it.dim) {
			float lw = (float)it.label.width(), lh = (float)it.label.height();
			float lx = x, ly = y - lh - 2 * d;
			if (ly < 0)
				ly = y + 2 * d; /* inside if no room above */
			if (it.rotHandle && ly < y) {
				/* Don't sit on the rotation handle above the top edge: go inside. */
				const PointD hp = scr(rotateHandlePos(it.rect, it.rot, ov.zoom, ov.dpr));
				const float pad = 8.0f * d;
				if (hp.x + pad > lx && hp.x - pad < lx + lw && hp.y + pad > ly && hp.y - pad < ly + lh)
					ly = y + 2 * d;
			}
			gfx::drawTexture(tex, std::floor(lx), std::floor(ly), lw, lh);
		}

		if (it.rotHandle) {
			/* Rotation handle: a dot above the top edge, joined to it. */
			const PointD c = rectCenter(it.rect);
			const PointD top = scr(rotateAbout({c.x, (double)it.rect.y}, c, it.rot));
			const PointD hp = scr(rotateHandlePos(it.rect, it.rot, ov.zoom, ov.dpr));
			gfx::drawLine((float)top.x, (float)top.y, (float)hp.x, (float)hp.y, 1.0f * d,
				      withAlpha(it.color, 200));
			const float rs = 10.0f * d;
			gfx::fillRect((float)hp.x - rs / 2, (float)hp.y - rs / 2, rs, rs, withAlpha(it.color, 255));
			gfx::strokeRect((float)hp.x - rs / 2, (float)hp.y - rs / 2, rs, rs, 1.5f * d, 0xFFFFFFFF);
		}

		if (it.selected && !it.locked && it.points.empty()) {
			float hs = 7.0f * d;
			for (Handle hd : kResizeHandles) {
				PointD p = rotateAbout(handlePos(it.rect, hd), rectCenter(it.rect), it.rot);
				float px = (float)(p.x * ov.zoom + ov.panX), py = (float)(p.y * ov.zoom + ov.panY);
				gfx::fillRect(px - hs / 2, py - hs / 2, hs, hs, 0xFFFFFFFF);
				gfx::strokeRect(px - hs / 2, py - hs / 2, hs, hs, 1.0f * d, withAlpha(it.color, 255));
			}
		}
	}

	if (ov.guideX) {
		float gx = (float)(ov.guideXPos * ov.zoom + ov.panX);
		gfx::drawLine(gx, 0, gx, (float)cy, 1.0f * d, kGuide);
	}
	if (ov.guideY) {
		float gy = (float)(ov.guideYPos * ov.zoom + ov.panY);
		gfx::drawLine(0, gy, (float)cx, gy, 1.0f * d, kGuide);
	}
	if (ov.rubber) {
		float x, y, w, h;
		toScreen(ov.rubberRect, x, y, w, h);
		gfx::fillRect(x, y, w, h, 0x30FFFFFF);
		gfx::strokeRect(x, y, w, h, 1.0f * d, 0xFFFFFFFF);
	}
	if (!ov.centerLabel.isNull()) {
		if (gs_texture_t *tex = label(ov.centerKey, ov.centerLabel)) {
			float lw = (float)ov.centerLabel.width(), lh = (float)ov.centerLabel.height();
			gfx::drawTexture(tex, std::floor((cx - lw) / 2), std::floor((cy - lh) / 2), lw, lh);
		}
	}

	/* Evict labels no longer shown. */
	if (labelCache_.size() > used.size() + 32) {
		for (auto it = labelCache_.begin(); it != labelCache_.end();) {
			if (!used.count(it->first)) {
				gs_texture_destroy(it->second);
				it = labelCache_.erase(it);
			} else {
				++it;
			}
		}
	}
}

/* ---------------------------------------------------------------------- */
/* Interaction                                                             */

static Qt::CursorShape cursorFor(Handle h)
{
	switch (h) {
	case Handle::TopLeft:
	case Handle::BottomRight:
		return Qt::SizeFDiagCursor;
	case Handle::TopRight:
	case Handle::BottomLeft:
		return Qt::SizeBDiagCursor;
	case Handle::Top:
	case Handle::Bottom:
		return Qt::SizeVerCursor;
	case Handle::Left:
	case Handle::Right:
		return Qt::SizeHorCursor;
	case Handle::Move:
		return Qt::SizeAllCursor;
	default:
		return Qt::ArrowCursor;
	}
}

int PreviewView::hitPoint(const Item &it, const QPointF &p) const
{
	if (kind_ != Kind::Output || !it.selected || it.locked || it.points.empty())
		return -1;
	const double tol = 9.0 * devicePixelRatioF() / zoom_;
	int best = -1;
	double bestD = tol * tol;
	for (size_t i = 0; i < it.points.size(); i++) {
		const double dx = it.points[i].x - p.x(), dy = it.points[i].y - p.y();
		if (dx * dx + dy * dy <= bestD) {
			bestD = dx * dx + dy * dy;
			best = (int)i;
		}
	}
	return best;
}

/* The rotation handle sits this many screen pixels above the slice's top edge. */
static constexpr double kRotateHandleGap = 22.0;

PointD PreviewView::rotateHandlePos(const IRect &r, double rot, double zoom, double dpr) const
{
	const PointD c = rectCenter(r);
	return rotateAbout({c.x, r.y - kRotateHandleGap * dpr / zoom}, c, rot);
}

bool PreviewView::hitRotate(const Item &it, const QPointF &p) const
{
	if (kind_ != Kind::Output || !it.selected || it.locked)
		return false;
	const PointD h = rotateHandlePos(it.rect, it.rot, zoom_, devicePixelRatioF());
	const double tol = 9.0 * devicePixelRatioF() / zoom_;
	return std::hypot(h.x - p.x(), h.y - p.y()) <= tol;
}

/* Mouse position in the slice's own (unrotated) frame, for its resize handles. */
static QPointF unrotated(const QPointF &p, const IRect &r, double rot)
{
	const PointD q = rotateAbout({p.x(), p.y()}, rectCenter(r), -rot);
	return QPointF(q.x, q.y);
}

void PreviewView::updateHover(const QPointF &p)
{
	Controller *c = Controller::instance();
	double tol = 6.0 * devicePixelRatioF() / zoom_;
	QString hover;
	Handle h = Handle::None;
	bool overLocked = false;
	for (const Item &it : items()) {
		if (hitPoint(it, p) >= 0) {
			hover = it.sliceId;
			h = Handle::None;
			setCursor(Qt::CrossCursor);
			if (hover != hoverSlice_) {
				hoverSlice_ = hover;
				refresh();
			}
			return;
		}
		if (hitRotate(it, p)) {
			setCursor(Qt::PointingHandCursor);
			return;
		}
		const QPointF lp = unrotated(p, it.rect, it.rot);
		Handle hh = (it.selected && !it.locked && it.points.empty())
				    ? hitTest(it.rect, lp.x(), lp.y(), tol)
				    : (it.contains(p.x(), p.y()) ? Handle::Move : Handle::None);
		if (hh != Handle::None) {
			hover = it.sliceId;
			h = hh;
			overLocked = it.locked;
			break;
		}
	}
	if (spaceDown_)
		setCursor(Qt::OpenHandCursor);
	else if (overLocked)
		setCursor(Qt::ForbiddenCursor);
	else if (h != Handle::None)
		setCursor(cursorFor(h));
	else
		setCursor(kind_ == Kind::Input && !c->doc().displays.empty() ? Qt::CrossCursor : Qt::ArrowCursor);

	if (hover != hoverSlice_) {
		hoverSlice_ = hover;
		refresh();
	}
}

void PreviewView::mousePressEvent(QMouseEvent *e)
{
	setFocus(Qt::MouseFocusReason);
	Controller *c = Controller::instance();
	const QPointF p = eventCanvasPos(e->position());
	dragStartCanvas_ = p;
	dragStartDevice_ = e->position() * devicePixelRatioF();

	if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && spaceDown_)) {
		drag_ = Drag::Pan;
		dragStartPanX_ = panX_;
		dragStartPanY_ = panY_;
		setCursor(Qt::ClosedHandCursor);
		return;
	}
	if (e->button() != Qt::LeftButton)
		return;

	double tol = 6.0 * devicePixelRatioF() / zoom_;
	for (const Item &it : items()) {
		/* A warp point of the selected slice wins over everything. */
		if (const int pi = hitPoint(it, p); pi >= 0) {
			c->selectWarpPoint(pi);
			drag_ = Drag::Point;
			dragSlice_ = it.sliceId;
			dragPoint_ = pi;
			dragStartPoint_ = it.points[(size_t)pi];
			return;
		}
		if (hitRotate(it, p)) {
			drag_ = Drag::Rotate;
			dragSlice_ = it.sliceId;
			dragStartRect_ = it.rect;
			dragStartRot_ = it.rot;
			const PointD c = rectCenter(it.rect);
			dragStartAngle_ = std::atan2(p.y() - c.y, p.x() - c.x);
			return;
		}
		const QPointF lp = unrotated(p, it.rect, it.rot);
		Handle h = (it.selected && !it.locked && it.points.empty())
				   ? hitTest(it.rect, lp.x(), lp.y(), tol)
				   : (it.contains(p.x(), p.y()) ? Handle::Move : Handle::None);
		if (h == Handle::None)
			continue;
		if (!it.selected)
			c->select(it.displayId, it.sliceId);
		if (it.locked) {
			emit blockedByLock(it.sliceId);
			return; /* locked: selectable, never moved */
		}
		drag_ = h == Handle::Move ? Drag::Move : Drag::Resize;
		dragHandle_ = h;
		dragSlice_ = it.sliceId;
		dragStartRect_ = it.rect;
		dragStartRot_ = it.rot;
		return;
	}

	if (kind_ == Kind::Input) {
		if (c->doc().displays.empty()) {
			emit needDisplay();
			return;
		}
		if (c->selectedDisplay().isEmpty())
			c->select(c->doc().displays.front().id, QString());
		drag_ = Drag::Create;
		rubber_ = {};
	} else {
		c->select(c->selectedDisplay(), QString());
	}
}

void PreviewView::mouseMoveEvent(QMouseEvent *e)
{
	const QPointF p = eventCanvasPos(e->position());
	int cw = 0, ch = 0;
	canvasSize(cw, ch);
	emit hoverChanged(p, p.x() >= 0 && p.y() >= 0 && p.x() < cw && p.y() < ch);

	const bool snap = snapping_ && !(e->modifiers() & Qt::AltModifier);
	const double thr = 8.0 * devicePixelRatioF() / zoom_;

	switch (drag_) {
	case Drag::None:
		updateHover(p);
		return;
	case Drag::Pan: {
		QPointF d = e->position() * devicePixelRatioF() - dragStartDevice_;
		panX_ = dragStartPanX_ + d.x();
		panY_ = dragStartPanY_ + d.y();
		autoFit_ = false;
		clampPan();
		refresh();
		return;
	}
	case Drag::Create: {
		double x0 = std::clamp(dragStartCanvas_.x(), 0.0, (double)cw);
		double y0 = std::clamp(dragStartCanvas_.y(), 0.0, (double)ch);
		double x1 = std::clamp(p.x(), 0.0, (double)cw), y1 = std::clamp(p.y(), 0.0, (double)ch);
		guideX_ = guideY_ = false;
		if (snap) {
			SnapTargets t = snapTargets(QString());
			SnapResult sx = snapAxis({x1}, t.xs, thr), sy = snapAxis({y1}, t.ys, thr);
			if (sx.snapped) {
				x1 += sx.delta;
				guideX_ = true;
				guideXPos_ = sx.line;
			}
			if (sy.snapped) {
				y1 += sy.delta;
				guideY_ = true;
				guideYPos_ = sy.line;
			}
			SnapResult s0x = snapAxis({x0}, t.xs, thr), s0y = snapAxis({y0}, t.ys, thr);
			if (s0x.snapped)
				x0 += s0x.delta;
			if (s0y.snapped)
				y0 += s0y.delta;
		}
		rubber_ = rectFromPoints(x0, y0, x1, y1);
		refresh();
		return;
	}
	case Drag::Point: {
		/* Tenth-of-a-pixel precision; arrow keys then step whole pixels. */
		const double nx = std::round((dragStartPoint_.x + p.x() - dragStartCanvas_.x()) * 10.0) / 10.0;
		const double ny = std::round((dragStartPoint_.y + p.y() - dragStartCanvas_.y()) * 10.0) / 10.0;
		Controller::instance()->setWarpPoint(dragSlice_, dragPoint_, {nx, ny});
		refresh();
		return;
	}
	case Drag::Rotate: {
		const PointD c = rectCenter(dragStartRect_);
		const double a = std::atan2(p.y() - c.y, p.x() - c.x);
		double deg = dragStartRot_ + (a - dragStartAngle_) * 180.0 / 3.14159265358979323846;
		if (e->modifiers() & Qt::ShiftModifier) {
			deg = std::round(deg / 15.0) * 15.0; /* Shift: 15° steps */
		} else {
			deg = std::round(deg * 10.0) / 10.0;
			const double q = std::round(deg / 90.0) * 90.0;
			if (snap && std::fabs(deg - q) <= 2.0)
				deg = q; /* snap to straight */
		}
		Controller::instance()->setSliceRotation(dragSlice_, deg);
		refresh();
		return;
	}
	case Drag::Move:
	case Drag::Resize: {
		double dx = p.x() - dragStartCanvas_.x(), dy = p.y() - dragStartCanvas_.y();
		/* Rotated slices: no edge snapping (their edges aren't the rect's),
		 * and resizing works along the slice's own axes. */
		const bool rotated = dragStartRot_ != 0.0;
		const bool snapHere = snap && !rotated;
		SnapTargets t;
		if (snapHere)
			t = snapTargets(dragSlice_);
		DragResult r;
		if (drag_ == Drag::Move) {
			r = applyMove(dragStartRect_, dx, dy, snapHere ? &t : nullptr, thr);
		} else {
			bool lock = aspectLock_ != bool(e->modifiers() & Qt::ShiftModifier);
			const PointD ld = rotateAbout({dx, dy}, {0, 0}, -dragStartRot_);
			r = applyResize(dragStartRect_, dragHandle_, ld.x, ld.y, lock, snapHere ? &t : nullptr, thr, 1);
			if (rotated) {
				/* Keep the opposite edge / corner still on screen: shift by
				 * (I - R)(c0 - c1), with R the slice's rotation. */
				const PointD c0 = rectCenter(dragStartRect_), c1 = rectCenter(r.rect);
				const PointD d0{c0.x - c1.x, c0.y - c1.y};
				const PointD rd = rotateAbout(d0, {0, 0}, dragStartRot_);
				r.rect.x += (int)std::lround(d0.x - rd.x);
				r.rect.y += (int)std::lround(d0.y - rd.y);
			}
		}
		guideX_ = r.snappedX;
		guideY_ = r.snappedY;
		guideXPos_ = r.guideX;
		guideYPos_ = r.guideY;
		applyRect(dragSlice_, r.rect);
		refresh();
		return;
	}
	}
}

void PreviewView::mouseReleaseEvent(QMouseEvent *e)
{
	if (drag_ == Drag::Create) {
		Controller *c = Controller::instance();
		if (rubber_.w >= 2 && rubber_.h >= 2)
			c->createSlice(c->selectedDisplay(), rubber_);
		else
			c->select(c->selectedDisplay(), QString());
	}
	drag_ = Drag::None;
	rubber_ = {};
	guideX_ = guideY_ = false;
	updateHover(eventCanvasPos(e->position()));
	refresh();
}

void PreviewView::mouseDoubleClickEvent(QMouseEvent *e)
{
	const QPointF p = eventCanvasPos(e->position());
	for (const Item &it : items())
		if (it.contains(p.x(), p.y()))
			return;
	fitToView();
}

void PreviewView::wheelEvent(QWheelEvent *e)
{
	const QPointF anchor = e->position() * devicePixelRatioF();
	const bool trackpadScroll = !e->pixelDelta().isNull() && !(e->modifiers() & Qt::ControlModifier);
	if (trackpadScroll) {
		panX_ += e->pixelDelta().x() * devicePixelRatioF();
		panY_ += e->pixelDelta().y() * devicePixelRatioF();
		autoFit_ = false;
		clampPan();
		refresh();
	} else {
		double steps = e->angleDelta().y() / 120.0;
		setZoomAt(zoom_ * std::pow(1.2, steps), anchor);
	}
	e->accept();
}

bool PreviewView::event(QEvent *e)
{
	if (e->type() == QEvent::KeyPress) {
		auto *k = static_cast<QKeyEvent *>(e);
		if (k->key() == Qt::Key_Tab || k->key() == Qt::Key_Backtab) {
			const bool back = k->key() == Qt::Key_Backtab || (k->modifiers() & Qt::ShiftModifier);
			Controller *c = Controller::instance();
			const Slice *s = c->doc().slice(c->selectedSlice());
			if (kind_ == Kind::Output && s && s->warp.active() && !s->locked) {
				/* Projection mapping: Tab steps through the warp points. */
				const int n = s->warp.count(), cur = c->selectedWarpPoint();
				c->selectWarpPoint(cur < 0 ? (back ? n - 1 : 0) : (cur + (back ? n - 1 : 1)) % n);
				return true;
			}
			c->cycleSlice(back ? -1 : 1);
			return true;
		}
	}
	if (e->type() == QEvent::NativeGesture) {
		auto *g = static_cast<QNativeGestureEvent *>(e);
		if (g->gestureType() == Qt::ZoomNativeGesture) {
			setZoomAt(zoom_ * (1.0 + g->value()), g->position() * devicePixelRatioF());
			return true;
		}
		if (g->gestureType() == Qt::SmartZoomNativeGesture) {
			fitToView();
			return true;
		}
	}
	return ObsDisplayWidget::event(e);
}

void PreviewView::nudge(int dx, int dy)
{
	Controller *c = Controller::instance();
	QString id = c->selectedSlice();
	if (id.isEmpty())
		return;
	if (c->isSliceLocked(id)) {
		emit blockedByLock(id);
		return;
	}
	if (kind_ == Kind::Output && c->selectedWarpPoint() >= 0) {
		/* A selected warp point moves on its own. */
		c->nudgeWarpPoint(id, c->selectedWarpPoint(), dx, dy);
		return;
	}
	IRect r = sliceRect(id);
	r.x += dx;
	r.y += dy;
	applyRect(id, r);
}

void PreviewView::keyPressEvent(QKeyEvent *e)
{
	Controller *c = Controller::instance();
	const int step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
	switch (e->key()) {
	case Qt::Key_Left:
		nudge(-step, 0);
		return;
	case Qt::Key_Right:
		nudge(step, 0);
		return;
	case Qt::Key_Up:
		nudge(0, -step);
		return;
	case Qt::Key_Down:
		nudge(0, step);
		return;
	case Qt::Key_Delete:
	case Qt::Key_Backspace:
		if (c->isSliceLocked(c->selectedSlice()))
			emit blockedByLock(c->selectedSlice());
		else if (!c->selectedSlice().isEmpty())
			c->deleteSlice(c->selectedSlice());
		return;
	case Qt::Key_BracketRight: /* bring forward */
	case Qt::Key_BracketLeft:  /* send backward */
		if (const DisplayMapping *d = c->doc().displayForSlice(c->selectedSlice())) {
			const QString id = c->selectedSlice();
			c->moveSlice(id, d->id, d->sliceIndex(id) + (e->key() == Qt::Key_BracketRight ? -1 : 1));
		}
		return;
	case Qt::Key_H:
		if (!c->selectedSlice().isEmpty())
			c->toggleSliceVisible(c->selectedSlice());
		return;
	case Qt::Key_Escape:
		if (drag_ == Drag::None && rubber_.empty()) {
			if (c->selectedWarpPoint() >= 0)
				c->selectWarpPoint(-1); /* first Esc: let go of the point */
			else
				c->select(c->selectedDisplay(), QString()); /* deselect */
			return;
		}
		if (drag_ == Drag::Point)
			c->setWarpPoint(dragSlice_, dragPoint_, dragStartPoint_);
		if (drag_ == Drag::Rotate)
			c->setSliceRotation(dragSlice_, dragStartRot_);
		if (drag_ == Drag::Move || drag_ == Drag::Resize)
			applyRect(dragSlice_, dragStartRect_);
		drag_ = Drag::None;
		rubber_ = {};
		guideX_ = guideY_ = false;
		refresh();
		return;
	case Qt::Key_Space:
		if (!e->isAutoRepeat()) {
			spaceDown_ = true;
			setCursor(Qt::OpenHandCursor);
		}
		return;
	case Qt::Key_F:
		fitToView();
		return;
	case Qt::Key_1:
	case Qt::Key_0:
		zoomActual();
		return;
	case Qt::Key_Plus:
	case Qt::Key_Equal:
		zoomBy(1.25);
		return;
	case Qt::Key_Minus:
		zoomBy(0.8);
		return;
	default:
		ObsDisplayWidget::keyPressEvent(e);
	}
}

void PreviewView::keyReleaseEvent(QKeyEvent *e)
{
	if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
		spaceDown_ = false;
		setCursor(kind_ == Kind::Input ? Qt::CrossCursor : Qt::ArrowCursor);
		return;
	}
	ObsDisplayWidget::keyReleaseEvent(e);
}

void PreviewView::resizeEvent(QResizeEvent *e)
{
	ObsDisplayWidget::resizeEvent(e);
	if (autoFit_)
		fitToView();
	else
		refresh();
}

void PreviewView::leaveEvent(QEvent *e)
{
	ObsDisplayWidget::leaveEvent(e);
	if (!hoverSlice_.isEmpty()) {
		hoverSlice_.clear();
		refresh();
	}
	emit hoverChanged(QPointF(), false);
}

QImage PreviewView::capture()
{
	QSize px = pixelSize();
	QImage img = gfx::captureOffscreen((uint32_t)px.width(), (uint32_t)px.height(),
					   [this](uint32_t cx, uint32_t cy) { renderFrame(cx, cy); });
	img.setDevicePixelRatio(devicePixelRatioF());
	return img;
}

void PreviewView::simulateDrag(const QPointF &fromCanvas, const QPointF &toCanvas, Qt::KeyboardModifiers mods,
			       int steps)
{
	auto send = [&](QEvent::Type type, const QPointF &canvas, Qt::MouseButton button, Qt::MouseButtons buttons) {
		QPointF local = canvasToWidget(canvas);
		QMouseEvent ev(type, local, mapToGlobal(local), button, buttons, mods);
		QCoreApplication::sendEvent(this, &ev);
	};
	send(QEvent::MouseMove, fromCanvas, Qt::NoButton, Qt::NoButton);
	send(QEvent::MouseButtonPress, fromCanvas, Qt::LeftButton, Qt::LeftButton);
	for (int i = 1; i <= steps; i++) {
		double t = (double)i / steps;
		QPointF pt = fromCanvas + (toCanvas - fromCanvas) * t;
		send(QEvent::MouseMove, pt, Qt::NoButton, Qt::LeftButton);
	}
	send(QEvent::MouseButtonRelease, toCanvas, Qt::LeftButton, Qt::NoButton);
}

} // namespace om
