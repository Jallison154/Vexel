/*
Output Mapping for OBS Studio
Copyright (C) 2026 Okami Designs

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// NDI output for NDI displays (video only).
//
// The NDI Runtime is loaded at run time from where NDI's own installers put
// it (NDI Tools / NDI Runtime); nothing NDI is linked or shipped. Without it,
// NDI displays report "NDI Runtime not installed" and everything else keeps
// working.
//
// Frames arrive from the graphics thread (submit) and are sent from a worker
// thread, so a slow network or encoder never stalls OBS rendering: if the
// worker is still busy, the newer frame replaces the waiting one.

#pragma once

#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace om::ndi {

/* Loads and initialises the NDI Runtime once (UI thread). Returns false and
 * sets `error` when it is missing or unusable. */
bool ensureRuntime(QString *error = nullptr);
bool runtimeAvailable();  /* already loaded successfully */
QString runtimeVersion(); /* e.g. "NDI SDK APPLE 6.1.1", empty if not loaded */
void shutdownRuntime();   /* after every sender is destroyed */

class Sender {
public:
	/* `name` is the NDI source name (receivers show "MACHINE (name)"). */
	static std::shared_ptr<Sender> create(const QString &name, int fpsNum, int fpsDen, QString *error = nullptr);
	~Sender();

	Sender(const Sender &) = delete;
	Sender &operator=(const Sender &) = delete;

	/* Graphics thread: copy one BGRA frame (top row first) for sending. */
	void submit(const uint8_t *data, uint32_t linesize, uint32_t width, uint32_t height);

	uint64_t framesSent() const { return sent_.load(); }
	uint64_t framesDropped() const { return dropped_.load(); }
	int connections() const { return connections_.load(); }
	const std::string &name() const { return name_; }

private:
	Sender() = default;
	void run();

	void *instance_ = nullptr;
	std::string name_;
	int fpsNum_ = 30, fpsDen_ = 1;

	std::mutex mutex_;
	std::condition_variable cv_;
	bool stop_ = false;
	bool pending_ = false;
	std::vector<uint8_t> waiting_, sending_; /* tightly packed BGRA */
	uint32_t waitW_ = 0, waitH_ = 0;

	std::atomic<uint64_t> sent_{0}, dropped_{0};
	std::atomic<int> connections_{0};
	std::thread worker_;
};

} // namespace om::ndi
