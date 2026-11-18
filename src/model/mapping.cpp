/*
Output Mapping for OBS Studio
Copyright (C) 2026 Okami Designs

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "model/mapping.hpp"

#include <cmath>

#include <QJsonArray>
#include <QUuid>

namespace om {

static const QColor kPalette[] = {
	QColor(0xFF, 0x9F, 0x1C), /* amber   */
	QColor(0x2E, 0xC4, 0xB6), /* teal    */
	QColor(0xE7, 0x1D, 0x73), /* magenta */
	QColor(0x7B, 0x8C, 0xFF), /* periwinkle */
	QColor(0xA7, 0xE0, 0x3C), /* lime    */
	QColor(0xFF, 0x5C, 0x4D), /* coral   */
	QColor(0x3D, 0xA5, 0xFF), /* azure   */
	QColor(0xF7, 0xD3, 0x3B), /* yellow  */
	QColor(0xC0, 0x7B, 0xFF), /* violet  */
	QColor(0x4D, 0xE0, 0x8A), /* mint    */
};

QColor sliceColorForIndex(int i)
{
	const int n = (int)(sizeof(kPalette) / sizeof(kPalette[0]));
	return kPalette[((i % n) + n) % n];
}

QString newId()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

int DisplayMapping::sliceIndex(const QString &sliceId) const
{
	for (int i = 0; i < (int)slices.size(); i++)
		if (slices[i].id == sliceId)
			return i;
	return -1;
}

int MappingDocument::displayIndex(const QString &displayId) const
{
	for (int i = 0; i < (int)displays.size(); i++)
		if (displays[i].id == displayId)
			return i;
	return -1;
}

DisplayMapping *MappingDocument::display(const QString &displayId)
{
	int i = displayIndex(displayId);
	return i >= 0 ? &displays[i] : nullptr;
}

const DisplayMapping *MappingDocument::display(const QString &displayId) const
{
	int i = displayIndex(displayId);
	return i >= 0 ? &displays[i] : nullptr;
}

DisplayMapping *MappingDocument::displayForSlice(const QString &sliceId)
{
	for (auto &d : displays)
		if (d.sliceIndex(sliceId) >= 0)
			return &d;
	return nullptr;
}

const DisplayMapping *MappingDocument::displayForSlice(const QString &sliceId) const
{
	return const_cast<MappingDocument *>(this)->displayForSlice(sliceId);
}

Slice *MappingDocument::slice(const QString &sliceId)
{
	for (auto &d : displays) {
		int i = d.sliceIndex(sliceId);
		if (i >= 0)
			return &d.slices[i];
	}
	return nullptr;
}

const Slice *MappingDocument::slice(const QString &sliceId) const
{
	return const_cast<MappingDocument *>(this)->slice(sliceId);
}

/* ---------------------------------------------------------------------- */
/* JSON                                                                   */

/* Rects are objects, not number arrays: obs_data (used by obs-websocket
 * vendor requests) cannot represent arrays of scalars. */
static QJsonObject rectToJson(const IRect &r)
{
	return QJsonObject{{"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h}};
}

static IRect rectFromJson(const QJsonValue &v)
{
	if (v.isObject()) {
		QJsonObject o = v.toObject();
		return {o.value("x").toInt(), o.value("y").toInt(), o.value("w").toInt(), o.value("h").toInt()};
	}
	QJsonArray a = v.toArray(); /* legacy [x, y, w, h] */
	if (a.size() != 4)
		return {};
	return {a[0].toInt(), a[1].toInt(), a[2].toInt(), a[3].toInt()};
}

QJsonObject warpToJson(const Warp &w)
{
	QJsonArray pts;
	for (const PointD &p : w.pts)
		pts.append(QJsonObject{{"x", p.x}, {"y", p.y}}); /* objects: obs_data has no scalar arrays */
	return QJsonObject{{"cols", w.cols}, {"rows", w.rows}, {"points", pts}};
}

Warp warpFromJson(const QJsonValue &v)
{
	Warp w;
	const QJsonObject o = v.toObject();
	w.cols = o.value("cols").toInt(0);
	w.rows = o.value("rows").toInt(0);
	for (const QJsonValue pv : o.value("points").toArray()) {
		const QJsonObject po = pv.toObject();
		const double x = po.value("x").toDouble(), y = po.value("y").toDouble();
		w.pts.push_back({std::isfinite(x) ? std::clamp(x, -10.0, 11.0) : 0.0,
				 std::isfinite(y) ? std::clamp(y, -10.0, 11.0) : 0.0});
	}
	if (!w.active())
		return Warp{}; /* malformed: no warp rather than garbage */
	return w;
}

QJsonObject featherToJson(const Feather &f)
{
	return QJsonObject{{"left", f.left},
			   {"right", f.right},
			   {"top", f.top},
			   {"bottom", f.bottom},
			   {"gamma", f.gamma}};
}

Feather featherFromJson(const QJsonValue &v)
{
	Feather f;
	const QJsonObject o = v.toObject();
	f.left = std::clamp(o.value("left").toInt(0), 0, 8192);
	f.right = std::clamp(o.value("right").toInt(0), 0, 8192);
	f.top = std::clamp(o.value("top").toInt(0), 0, 8192);
	f.bottom = std::clamp(o.value("bottom").toInt(0), 0, 8192);
	const double g = o.value("gamma").toDouble(1.0);
	f.gamma = std::isfinite(g) ? std::clamp(g, 0.2, 5.0) : 1.0;
	return f;
}

QJsonObject MappingDocument::toJson() const
{
	QJsonArray ds;
	for (const auto &d : displays) {
		QJsonArray ss;
		for (const auto &s : d.slices) {
			QJsonObject so{
				{"id", s.id},
				{"name", s.name},
				{"color", s.color.name()},
				{"src", rectToJson(s.src)},
				{"dst", rectToJson(s.dst)},
				{"enabled", s.enabled},
				{"locked", s.locked},
			};
			if (s.warp.active())
				so["warp"] = warpToJson(s.warp);
			if (s.feather.active())
				so["feather"] = featherToJson(s.feather);
			if (s.rotation != 0.0)
				so["rotation"] = s.rotation;
			ss.append(so);
		}
		QJsonObject ident{
			{"uuid", QString::fromStdString(d.identity.uuid)},
			{"vendor", (qint64)d.identity.vendor},
			{"model", (qint64)d.identity.model},
			{"serial", (qint64)d.identity.serial},
			{"name", QString::fromStdString(d.identity.name)},
		};
		ds.append(QJsonObject{
			{"id", d.id},
			{"name", d.name},
			{"kind", d.kind == DisplayKind::Virtual ? "virtual"
				 : d.kind == DisplayKind::Ndi   ? "ndi"
								: "physical"},
			{"identity", ident},
			{"width", d.width},
			{"height", d.height},
			{"outputWanted", d.outputWanted},
			{"delayMs", d.delayMs},
			{"slices", ss},
		});
	}
	return QJsonObject{
		{"format", "obs-output-mapping"},           {"version", 1},   {"restoreOutputs", restoreOutputs},
		{"excludeFromCapture", excludeFromCapture}, {"displays", ds},
	};
}

MappingDocument MappingDocument::fromJson(const QJsonObject &obj)
{
	MappingDocument doc;
	doc.restoreOutputs = obj.value("restoreOutputs").toBool(true);
	doc.excludeFromCapture = obj.value("excludeFromCapture").toBool(true);
	int colorIdx = 0;
	for (const QJsonValue dv : obj.value("displays").toArray()) {
		QJsonObject o = dv.toObject();
		DisplayMapping d;
		d.id = o.value("id").toString();
		if (d.id.isEmpty())
			d.id = newId();
		d.name = o.value("name").toString("Display");
		const QString kind = o.value("kind").toString();
		d.kind = kind == "virtual" ? DisplayKind::Virtual
			 : kind == "ndi"   ? DisplayKind::Ndi
					   : DisplayKind::Physical;
		QJsonObject id = o.value("identity").toObject();
		d.identity.uuid = id.value("uuid").toString().toStdString();
		d.identity.vendor = (uint32_t)id.value("vendor").toInteger();
		d.identity.model = (uint32_t)id.value("model").toInteger();
		d.identity.serial = (uint32_t)id.value("serial").toInteger();
		d.identity.name = id.value("name").toString().toStdString();
		d.width = std::max(1, o.value("width").toInt(1920));
		d.height = std::max(1, o.value("height").toInt(1080));
		d.outputWanted = o.value("outputWanted").toBool(false);
		d.delayMs = std::clamp(o.value("delayMs").toInt(0), 0, kMaxDelayMs);
		for (const QJsonValue sv : o.value("slices").toArray()) {
			QJsonObject so = sv.toObject();
			Slice s;
			s.id = so.value("id").toString();
			if (s.id.isEmpty())
				s.id = newId();
			s.name = so.value("name").toString("Slice");
			QColor c(so.value("color").toString());
			s.color = c.isValid() ? c : sliceColorForIndex(colorIdx);
			colorIdx++;
			s.src = rectFromJson(so.value("src"));
			s.dst = rectFromJson(so.value("dst"));
			s.enabled = so.value("enabled").toBool(true);
			s.locked = so.value("locked").toBool(false);
			s.warp = warpFromJson(so.value("warp"));
			s.feather = featherFromJson(so.value("feather"));
			s.rotation = normalizeAngle(so.value("rotation").toDouble(0.0));
			d.slices.push_back(s);
		}
		doc.displays.push_back(std::move(d));
	}
	return doc;
}

/* ---------------------------------------------------------------------- */
/* Render snapshot                                                         */

RenderStore &RenderStore::instance()
{
	static RenderStore store;
	return store;
}

void RenderStore::publish(std::shared_ptr<const RenderState> s)
{
	std::lock_guard<std::mutex> lock(mutex_);
	state_ = std::move(s);
}

std::shared_ptr<const RenderState> RenderStore::get() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return state_;
}

bool RenderStore::getDisplay(const std::string &id, RenderDisplay &out) const
{
	auto s = get();
	auto it = s->displays.find(id);
	if (it == s->displays.end())
		return false;
	out = it->second;
	return true;
}

std::shared_ptr<const RenderState> buildRenderState(const MappingDocument &doc)
{
	auto st = std::make_shared<RenderState>();
	for (const auto &d : doc.displays) {
		RenderDisplay rd;
		rd.id = d.id.toStdString();
		rd.width = d.width;
		rd.height = d.height;
		rd.delayMs = d.delayMs;
		/* Sidebar index 0 is on top -> draw it last. */
		for (auto it = d.slices.rbegin(); it != d.slices.rend(); ++it) {
			if (!it->enabled || it->src.empty() || it->dst.empty())
				continue;
			RenderSlice rs;
			rs.src = it->src;
			rs.dst = it->dst;
			rs.id = it->id.toStdString();
			rs.mapped = sliceIsMapped(*it);
			if (rs.mapped) {
				rs.tris = std::make_shared<const std::vector<float>>(
					warpTriangles(it->warp, it->dst, it->rotation));
				rs.feather[0] = it->dst.w > 0 ? (float)it->feather.left / it->dst.w : 0.0f;
				rs.feather[1] = it->dst.w > 0 ? (float)it->feather.right / it->dst.w : 0.0f;
				rs.feather[2] = it->dst.h > 0 ? (float)it->feather.top / it->dst.h : 0.0f;
				rs.feather[3] = it->dst.h > 0 ? (float)it->feather.bottom / it->dst.h : 0.0f;
				rs.gamma = (float)it->feather.gamma;
			}
			rd.slices.push_back(std::move(rs));
		}
		st->displays[d.id.toStdString()] = std::move(rd);
	}
	return st;
}

} // namespace om
