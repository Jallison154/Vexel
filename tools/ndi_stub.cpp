// Stand-in for the NDI Runtime, for automated tests only (never shipped).
//
// Exports the NDI entry points Vexel uses. Instead of sending on the network
// it records what it receives in $NDI_STUB_DIR:
//   <name>.json  per source: frames, width, height, fourcc, fps, stride
//   <name>.bgra  every 10th frame (raw, tightly packed, top row first)
// Point Vexel at it with OUTPUT_MAPPING_NDI_LIB=/path/to/libndi_stub.dylib.
//
// Build (macOS): clang++ -std=c++17 -dynamiclib -arch arm64 -arch x86_64 \
//                  -o libndi_stub.dylib tools/ndi_stub.cpp

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif

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

struct Source {
	std::string name;
	uint64_t frames = 0;
};

static std::mutex g_mutex;
static int g_live = 0;

static std::string outPath(const std::string &name, const char *ext)
{
	const char *dir = getenv("NDI_STUB_DIR");
	std::string safe;
	for (char c : name)
		safe += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
	return std::string(dir && *dir ? dir : "/tmp") + "/" + safe + ext;
}

static void writeFile(const std::string &path, const void *data, size_t n)
{
	const std::string tmp = path + ".tmp";
	if (FILE *f = fopen(tmp.c_str(), "wb")) {
		fwrite(data, 1, n, f);
		fclose(f);
		rename(tmp.c_str(), path.c_str());
	}
}

EXPORT bool NDIlib_initialize()
{
	return true;
}

EXPORT void NDIlib_destroy() {}

EXPORT const char *NDIlib_version()
{
	return "NDI stub (test)";
}

EXPORT void *NDIlib_send_create(const SendCreate *desc)
{
	if (!desc || !desc->p_ndi_name)
		return nullptr;
	auto *s = new Source();
	s->name = desc->p_ndi_name;
	std::lock_guard<std::mutex> lock(g_mutex);
	g_live++;
	return s;
}

EXPORT void NDIlib_send_destroy(void *inst)
{
	auto *s = static_cast<Source *>(inst);
	if (!s)
		return;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_live--;
	}
	char buf[256];
	snprintf(buf, sizeof(buf), "{\"name\":\"%s\",\"destroyed\":true,\"frames\":%llu}", s->name.c_str(),
		 (unsigned long long)s->frames);
	writeFile(outPath(s->name, ".json"), buf, strlen(buf));
	delete s;
}

EXPORT void NDIlib_send_send_video_v2(void *inst, const VideoFrameV2 *f)
{
	auto *s = static_cast<Source *>(inst);
	if (!s || !f || !f->p_data)
		return;
	s->frames++;
	char buf[512];
	snprintf(buf, sizeof(buf),
		 "{\"name\":\"%s\",\"frames\":%llu,\"width\":%d,\"height\":%d,\"fourcc\":%d,\"fpsN\":%d,"
		 "\"fpsD\":%d,\"stride\":%d,\"format\":%d}",
		 s->name.c_str(), (unsigned long long)s->frames, f->xres, f->yres, f->FourCC, f->frame_rate_N,
		 f->frame_rate_D, f->line_stride_in_bytes, f->frame_format_type);
	writeFile(outPath(s->name, ".json"), buf, strlen(buf));
	if (s->frames % 10 == 0) {
		std::vector<uint8_t> px((size_t)f->xres * f->yres * 4);
		for (int y = 0; y < f->yres; y++)
			memcpy(px.data() + (size_t)y * f->xres * 4, f->p_data + (size_t)y * f->line_stride_in_bytes,
			       (size_t)f->xres * 4);
		writeFile(outPath(s->name, ".bgra"), px.data(), px.size());
	}
}

EXPORT int NDIlib_send_get_no_connections(void *, uint32_t)
{
	return 2; /* pretend two receivers are watching */
}
