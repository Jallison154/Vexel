/*
Output Mapping for OBS Studio
Copyright (C) 2026 Okami Designs

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "render/identify-image.hpp"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>

namespace om {

static QFont uiFont(int px, bool bold = false)
{
	QFont f;
	f.setPixelSize(std::max(8, px));
	f.setBold(bold);
	return f;
}

static void drawLabel(QPainter &p, int x, int y, const QString &text, const QColor &bg, int px)
{
	QFont f = uiFont(px, true);
	QFontMetrics fm(f);
	const int pad = std::max(4, px / 3);
	QRect r(x, y, fm.horizontalAdvance(text) + 2 * pad, fm.height() + pad);
	p.setPen(Qt::NoPen);
	p.setBrush(bg);
	p.drawRoundedRect(r, pad, pad);
	const double lum = 0.299 * bg.redF() + 0.587 * bg.greenF() + 0.114 * bg.blueF();
	p.setPen(lum > 0.55 ? QColor(15, 15, 18) : QColor(255, 255, 255));
	p.setFont(f);
	p.drawText(r, Qt::AlignCenter, text);
}

static void drawSliceOutlines(QPainter &p, const DisplayMapping &d, bool dashed, int unit)
{
	/* Bottom-most first so labels of upper slices stay readable. */
	for (auto it = d.slices.rbegin(); it != d.slices.rend(); ++it) {
		if (!it->enabled)
			continue;
		const IRect &r = it->dst;
		QPen pen(it->color, std::max(2, unit / 4));
		pen.setStyle(dashed ? Qt::DashLine : Qt::SolidLine);
		pen.setJoinStyle(Qt::MiterJoin);
		p.setPen(pen);
		QColor fill = it->color;
		fill.setAlpha(dashed ? 0 : 28);
		p.setBrush(fill);
		if (it->warp.active() && !isIdentityWarp(it->warp)) {
			/* The real (warped) shape on the output. */
			QPolygonF poly;
			for (const PointD &n : warpOutline(it->warp, 48)) {
				const PointD px = warpToPixels(n, r);
				poly << QPointF(px.x, px.y);
			}
			p.setRenderHint(QPainter::Antialiasing, true);
			p.drawPolygon(poly);
			p.setRenderHint(QPainter::Antialiasing, false);
		} else {
			p.drawRect(QRect(r.x, r.y, r.w - 1, r.h - 1));
		}
		drawLabel(p, r.x + unit / 2, r.y + unit / 2,
			  QStringLiteral("%1  %2×%3").arg(it->name).arg(r.w).arg(r.h), it->color, unit);
	}
}

/* One calibration pattern filling `area` (in image pixels): grid and rulers
 * in area-local coordinates, corner markers, centre circle, name and size,
 * and a red + white edge border on the area's outermost pixels. */
static void paintPattern(QPainter &p, const QRect &area, const QString &name, const QString &sub, const QColor &accent,
			 bool compact)
{
	const int w = area.width(), h = area.height();
	if (w < 4 || h < 4)
		return;
	p.save();
	p.setClipRect(area);
	p.translate(area.topLeft());
	const int unit = std::max(6, std::min(w, h) / 40); /* ~27 px at 1080p */
	p.setRenderHint(QPainter::Antialiasing, false);
	p.fillRect(0, 0, w, h, QColor(14, 15, 18));

	/* Grid: every 100 px, brighter every 500 px. */
	for (int x = 100; x < w; x += 100) {
		p.setPen(QColor(x % 500 == 0 ? 90 : 48, x % 500 == 0 ? 92 : 50, x % 500 == 0 ? 100 : 58));
		p.drawLine(x, 0, x, h - 1);
	}
	for (int y = 100; y < h; y += 100) {
		p.setPen(QColor(y % 500 == 0 ? 90 : 48, y % 500 == 0 ? 92 : 50, y % 500 == 0 ? 100 : 58));
		p.drawLine(0, y, w - 1, y);
	}

	/* Rulers (pixel coordinates within the area) along the top and left. */
	p.setPen(QColor(150, 155, 165));
	p.setFont(uiFont(unit / 2 + 4));
	const int clear = unit * 5; /* keep numbers away from the corner markers */
	for (int x = 100; x < w - clear; x += 100)
		if (x > clear)
			p.drawText(x + 4, unit, QString::number(x));
	for (int y = 100; y < h - clear; y += 100)
		if (y > clear)
			p.drawText(6, y - 4, QString::number(y));

	/* Centre: crosshair and circle. */
	const int cx = w / 2, cy = h / 2, rad = std::min(w, h) / 6;
	p.setPen(QPen(QColor(255, 255, 255), 1));
	p.drawLine(cx - rad - unit, cy, cx + rad + unit, cy);
	p.drawLine(cx, cy - rad - unit, cx, cy + rad + unit);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.drawEllipse(QPoint(cx, cy), rad, rad);
	p.setRenderHint(QPainter::Antialiasing, false);

	/* Corner markers (L shapes). */
	const int len = unit * 3, th = std::max(2, unit / 5), in = unit / 2;
	p.setPen(Qt::NoPen);
	p.setBrush(accent);
	const QPoint corners[4] = {{in, in}, {w - in, in}, {in, h - in}, {w - in, h - in}};
	for (int i = 0; i < 4; i++) {
		const int sx = (i % 2) ? -1 : 1, sy = (i / 2) ? -1 : 1;
		QPoint c = corners[i];
		p.drawRect(QRect(sx > 0 ? c.x() : c.x() - len, sy > 0 ? c.y() : c.y() - th, len, th));
		p.drawRect(QRect(sx > 0 ? c.x() : c.x() - th, sy > 0 ? c.y() : c.y() - len, th, len));
	}

	/* Exact-edge border: outermost pixel red, next pixel white. If either is
	 * missing on the screen, that edge is cropped. */
	auto frame = [&](int inset, const QColor &col) {
		p.fillRect(inset, inset, w - 2 * inset, 1, col);
		p.fillRect(inset, h - 1 - inset, w - 2 * inset, 1, col);
		p.fillRect(inset, inset, 1, h - 2 * inset, col);
		p.fillRect(w - 1 - inset, inset, 1, h - 2 * inset, col);
	};
	frame(0, QColor(255, 40, 40));
	frame(1, QColor(255, 255, 255));

	/* Identity text, above and below the centre circle. */
	p.setRenderHint(QPainter::TextAntialiasing, true);
	p.setPen(QColor(255, 255, 255));
	p.setFont(uiFont(unit * 3, true));
	p.drawText(QRect(0, cy - rad - unit * 5, w, unit * 4), Qt::AlignCenter, name);
	p.setFont(uiFont(unit * 2, false));
	p.drawText(QRect(0, cy + rad + unit, w, unit * 3), Qt::AlignCenter, sub);
	if (!compact) {
		p.setPen(accent);
		p.setFont(uiFont(unit, true));
		p.drawText(QRect(0, cy + rad + unit * 4, w, unit * 2), Qt::AlignCenter,
			   QStringLiteral("VEXEL TEST PATTERN · red + white edge lines must be visible"));
	}
	p.restore();
}

QImage makeTestPattern(const DisplayMapping &d)
{
	const int w = std::max(16, d.width), h = std::max(16, d.height);
	const int unit = std::max(10, std::min(w, h) / 40);
	QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
	img.fill(QColor(0, 0, 0));
	QPainter p(&img);

	bool any = false;
	for (const Slice &s : d.slices)
		any = any || (s.enabled && s.dst.w > 0 && s.dst.h > 0);
	/* Warped / soft-edged slices get their own pattern (makeSlicePattern),
	 * drawn through the warp on the GPU; leave their area to them. */
	if (!any) {
		/* No slices yet: one pattern for the whole output. */
		paintPattern(p, QRect(0, 0, w, h), d.name, QStringLiteral("%1 × %2").arg(d.width).arg(d.height),
			     QColor(255, 196, 66), false);
		p.end();
		return img;
	}

	/* Area no slice covers: black, with the output's name in a corner so an
	 * uncovered screen is still identifiable. */
	p.setRenderHint(QPainter::TextAntialiasing, true);
	p.setPen(QColor(110, 115, 125));
	p.setFont(uiFont(unit, true));
	p.drawText(QRect(unit, unit, w - 2 * unit, h - 2 * unit), Qt::AlignRight | Qt::AlignBottom,
		   QStringLiteral("%1 · %2 × %3 · no slice here").arg(d.name).arg(d.width).arg(d.height));

	/* One pattern per slice, at the slice's exact output rect. Bottom-most
	 * first so overlaps stack the same way the video does. */
	for (auto it = d.slices.rbegin(); it != d.slices.rend(); ++it) {
		if (!it->enabled || it->dst.w <= 0 || it->dst.h <= 0 || sliceIsMapped(*it))
			continue;
		const IRect &r = it->dst;
		const bool compact = std::min(r.w, r.h) < 400;
		paintPattern(p, QRect(r.x, r.y, r.w, r.h), it->name,
			     QStringLiteral("%1 × %2 @ %3, %4").arg(r.w).arg(r.h).arg(r.x).arg(r.y), it->color,
			     compact);
	}
	p.end();
	return img;
}

QImage makeSlicePattern(const Slice &s)
{
	const int w = std::clamp(s.dst.w, 16, 8192), h = std::clamp(s.dst.h, 16, 8192);
	QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
	img.fill(QColor(0, 0, 0));
	QPainter p(&img);
	QString sub = QStringLiteral("%1 × %2").arg(s.dst.w).arg(s.dst.h);
	if (s.warp.active() && !isIdentityWarp(s.warp))
		sub += s.warp.cornerPin() ? QStringLiteral(" · corner pin")
					  : QStringLiteral(" · mesh %1×%2").arg(s.warp.cols).arg(s.warp.rows);
	paintPattern(p, QRect(0, 0, w, h), s.name, sub, s.color, std::min(w, h) < 400);
	p.end();
	return img;
}

QImage makeOutlineOverlay(const DisplayMapping &d)
{
	const int w = std::max(16, d.width), h = std::max(16, d.height);
	const int unit = std::max(10, std::min(w, h) / 40);
	QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::transparent);
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing, false);
	p.setRenderHint(QPainter::TextAntialiasing, true);

	/* Display edges. */
	const int th = std::max(2, unit / 6);
	const QColor edge(255, 196, 66);
	p.fillRect(0, 0, w, th, edge);
	p.fillRect(0, h - th, w, th, edge);
	p.fillRect(0, 0, th, h, edge);
	p.fillRect(w - th, 0, th, h, edge);

	drawSliceOutlines(p, d, false, unit);
	drawLabel(p, w - unit * 16, h - unit * 2,
		  QStringLiteral("%1 · %2×%3 · OUTLINES").arg(d.name).arg(d.width).arg(d.height), edge, unit * 3 / 4);
	p.end();
	return img;
}

} // namespace om
