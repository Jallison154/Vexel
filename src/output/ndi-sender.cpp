/*
Output Mapping for OBS Studio
Copyright (C) 2026 Okami Designs

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "output/ndi-sender.hpp"

#include <obs-module.h>

#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QStringList>

#include <chrono>
#include <cstring>

namespace om::ndi {

/* ---------------------------------------------------------------------- */
/* The few NDI Runtime entry points used, declared from NDI's public C ABI */
/* (stable across NDI 5 and 6).                                            */

namespace abi {

struct SendCreate {
	const char *p_ndi_name;
	const char *p_groups;
	bool clock_video;
	bool clock_audio;
};

struct VideoFrameV2 {
	int xres, yres;
	int FourCC;
	int frame_rate_N, frame_rate_D;
	float picture_aspect_ratio;
	int frame_format_type;
	int64_t timecode;
	uint8_t *p_data;
	int line_stride_in_bytes;
	const char *p_metadata;
	int64_t timestamp;
};

constexpr int fourcc(char a, char b, char c, char d)
{
	return (int)((uint32_t)(uint8_t)a | ((uint32_t)(uint8_t)b << 8) | ((uint32_t)(uint8_t)c << 16) |
		     ((uint32_t)(uint8_t)d << 24));
}
constexpr int kFourCC_BGRX = fourcc('B', 'G', 'R', 'X');
constexpr int kProgressive = 1;
constexpr int64_t kTimecodeSynthesize = INT64_MAX;

using initialize_t = bool (*)();
using destroy_t = void (*)();
using version_t = const char *(*)();
using send_create_t = void *(*)(const SendCreate *);
using send_destroy_t = void (*)(void *);
using send_video_v2_t = void (*)(void *, const VideoFrameV2 *);
using send_get_no_connections_t = int (*)(void *, uint32_t);

} // namespace abi

struct Runtime {
	QLibrary lib;
	bool loaded = false;
	bool initialised = false;
	QString version, error;
	abi::initialize_t initialize = nullptr;
	abi::destroy_t destroy = nullptr;
	abi::version_t version_fn = nullptr;
	abi::send_create_t send_create = nullptr;
	abi::send_destroy_t send_destroy = nullptr;
	abi::send_video_v2_t send_video_v2 = nullptr;
	abi::send_get_no_connections_t send_get_no_connections = nullptr;
};

static Runtime &rt()
{
	static Runtime r;
	return r;
}

static QStringList candidates()
{
#if defined(_WIN32)
	const QString file = QStringLiteral("Processing.NDI.Lib.x64.dll");
#elif defined(__APPLE__)
	const QString file = QStringLiteral("libndi.dylib");
#else
	const QString file = QStringLiteral("libndi.so.6");
#endif
	QStringList out;
	/* Testing override: an exact library path. */
	const QString exact = qEnvironmentVariable("OUTPUT_MAPPING_NDI_LIB");
	if (!exact.isEmpty())
		out << exact;
	/* Set by NDI's runtime installers. */
	for (const char *env : {"NDI_RUNTIME_DIR_V6", "NDI_RUNTIME_DIR_V5"}) {
		const QString dir = qEnvironmentVariable(env);
		if (!dir.isEmpty())
			out << QDir(dir).filePath(file);
	}
#if defined(__APPLE__)
	out << QStringLiteral("/usr/local/lib/libndi.dylib")
	    << QStringLiteral("/Library/NDI SDK for Apple/lib/macOS/libndi.dylib");
#elif !defined(_WIN32)
	out << QStringLiteral("/usr/lib/libndi.so.6") << QStringLiteral("/usr/local/lib/libndi.so.6")
	    << QStringLiteral("libndi.so.6") << QStringLiteral("libndi.so.5");
#endif
	return out;
}

bool ensureRuntime(QString *error)
{
	Runtime &r = rt();
	if (r.initialised)
		return true;
	if (!r.loaded) {
		QStringList tried;
		for (const QString &path : candidates()) {
			const bool bare = !path.contains('/') && !path.contains('\\');
			if (!bare && !QFileInfo::exists(path)) {
				tried << path;
				continue;
			}
			r.lib.setFileName(path);
			if (!r.lib.load()) {
				tried << QStringLiteral("%1 (%2)").arg(path, r.lib.errorString());
				continue;
			}
			r.initialize = reinterpret_cast<abi::initialize_t>(r.lib.resolve("NDIlib_initialize"));
			r.destroy = reinterpret_cast<abi::destroy_t>(r.lib.resolve("NDIlib_destroy"));
			r.version_fn = reinterpret_cast<abi::version_t>(r.lib.resolve("NDIlib_version"));
			r.send_create = reinterpret_cast<abi::send_create_t>(r.lib.resolve("NDIlib_send_create"));
			r.send_destroy = reinterpret_cast<abi::send_destroy_t>(r.lib.resolve("NDIlib_send_destroy"));
			r.send_video_v2 =
				reinterpret_cast<abi::send_video_v2_t>(r.lib.resolve("NDIlib_send_send_video_v2"));
			r.send_get_no_connections = reinterpret_cast<abi::send_get_no_connections_t>(
				r.lib.resolve("NDIlib_send_get_no_connections"));
			if (!r.initialize || !r.destroy || !r.send_create || !r.send_destroy || !r.send_video_v2) {
				tried << QStringLiteral("%1 (not an NDI runtime)").arg(path);
				r.lib.unload();
				continue;
			}
			r.loaded = true;
			blog(LOG_INFO, "[output-mapping] NDI runtime loaded from %s", qPrintable(path));
			break;
		}
		if (!r.loaded) {
			r.error = QStringLiteral(
				"NDI Runtime not installed. Install NDI Tools (ndi.video) and restart OBS.");
			blog(LOG_WARNING, "[output-mapping] NDI runtime not found; tried: %s",
			     qPrintable(tried.join(QStringLiteral("; "))));
			if (error)
				*error = r.error;
			return false;
		}
	}
	if (!r.initialize()) {
		r.error = QStringLiteral("NDI could not start on this computer (unsupported CPU?).");
		blog(LOG_ERROR, "[output-mapping] NDIlib_initialize failed");
		if (error)
			*error = r.error;
		return false;
	}
	r.initialised = true;
	r.version = r.version_fn ? QString::fromUtf8(r.version_fn()) : QString();
	blog(LOG_INFO, "[output-mapping] NDI runtime ready (%s)", qPrintable(r.version));
	return true;
}

bool runtimeAvailable()
{
	return rt().initialised;
}

QString runtimeVersion()
{
	return rt().version;
}

void shutdownRuntime()
{
	Runtime &r = rt();
	if (r.initialised) {
		r.destroy();
		r.initialised = false;
	}
	/* The library itself stays loaded: NDI keeps background threads that
	 * must never outlive their code. */
}

/* ---------------------------------------------------------------------- */
/* Sender                                                                  */

std::shared_ptr<Sender> Sender::create(const QString &name, int fpsNum, int fpsDen, QString *error)
{
	if (!ensureRuntime(error))
		return nullptr;
	Runtime &r = rt();
	std::shared_ptr<Sender> s(new Sender());
	s->name_ = name.toStdString();
	s->fpsNum_ = fpsNum > 0 ? fpsNum : 30;
	s->fpsDen_ = fpsDen > 0 ? fpsDen : 1;
	abi::SendCreate desc = {};
	desc.p_ndi_name = s->name_.c_str();
	desc.p_groups = nullptr;
	desc.clock_video = false; /* OBS paces the frames */
	desc.clock_audio = false;
	s->instance_ = r.send_create(&desc);
	if (!s->instance_) {
		if (error)
			*error = QStringLiteral("NDI could not create the source '%1' (name in use?).").arg(name);
		blog(LOG_ERROR, "[output-mapping] NDIlib_send_create failed for '%s'", s->name_.c_str());
		return nullptr;
	}
	s->worker_ = std::thread([raw = s.get()]() { raw->run(); });
	blog(LOG_INFO, "[output-mapping] NDI source '%s' created (%d/%d fps)", s->name_.c_str(), s->fpsNum_,
	     s->fpsDen_);
	return s;
}

Sender::~Sender()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stop_ = true;
	}
	cv_.notify_all();
	if (worker_.joinable())
		worker_.join();
	if (instance_ && rt().send_destroy)
		rt().send_destroy(instance_);
	blog(LOG_INFO, "[output-mapping] NDI source '%s' stopped (%llu frames sent, %llu dropped)", name_.c_str(),
	     (unsigned long long)sent_.load(), (unsigned long long)dropped_.load());
}

void Sender::submit(const uint8_t *data, uint32_t linesize, uint32_t width, uint32_t height)
{
	if (!data || width == 0 || height == 0)
		return;
	std::lock_guard<std::mutex> lock(mutex_);
	if (stop_)
		return;
	if (pending_)
		dropped_++; /* worker still busy: the newer frame wins */
	const size_t row = (size_t)width * 4;
	waiting_.resize(row * height);
	if (linesize == row) {
		memcpy(waiting_.data(), data, row * height);
	} else {
		for (uint32_t y = 0; y < height; y++)
			memcpy(waiting_.data() + row * y, data + (size_t)linesize * y, row);
	}
	waitW_ = width;
	waitH_ = height;
	pending_ = true;
	cv_.notify_one();
}

void Sender::run()
{
	auto lastPoll = std::chrono::steady_clock::now() - std::chrono::seconds(2);
	for (;;) {
		uint32_t w = 0, h = 0;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			cv_.wait_for(lock, std::chrono::milliseconds(500), [this]() { return stop_ || pending_; });
			if (stop_)
				return;
			if (pending_) {
				sending_.swap(waiting_);
				w = waitW_;
				h = waitH_;
				pending_ = false;
			}
		}
		if (w && h) {
			abi::VideoFrameV2 f = {};
			f.xres = (int)w;
			f.yres = (int)h;
			f.FourCC = abi::kFourCC_BGRX;
			f.frame_rate_N = fpsNum_;
			f.frame_rate_D = fpsDen_;
			f.picture_aspect_ratio = 0.0f; /* square pixels */
			f.frame_format_type = abi::kProgressive;
			f.timecode = abi::kTimecodeSynthesize;
			f.p_data = sending_.data();
			f.line_stride_in_bytes = (int)w * 4;
			rt().send_video_v2(instance_, &f); /* returns once NDI has its copy */
			sent_++;
		}
		const auto now = std::chrono::steady_clock::now();
		if (rt().send_get_no_connections && now - lastPoll > std::chrono::seconds(1)) {
			connections_ = rt().send_get_no_connections(instance_, 0);
			lastPoll = now;
		}
	}
}

} // namespace om::ndi
