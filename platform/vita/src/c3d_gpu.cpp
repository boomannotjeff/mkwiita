// citro3d on the PS Vita, through vitaGL: the GPU state machine, textures,
// render targets, draws and presentation (see c3d_internal.hpp).
//
// Conventions kept from the PICA so the game's renderer runs unchanged:
//  - "LINEAR" memory (linearAlloc) is GPU-mapped vitaGL pool memory; draws
//    read vertex and index data in place, as the PICA does.
//  - Render targets keep their portrait 3DS shapes (240x400 top screen) and
//    are rendered at Scale times that size. FrameEnd rotates the top target
//    onto the landscape display, undoing Mtx_PerspTilt/Mtx_OrthoTilt.
//  - Textures keep their tiled PICA data in CPU memory (tex->data) and are
//    converted for the GPU when bound after a flush or upload.
//  - Depth keeps the PICA's values (c3d_shader.cpp), so GEQUAL/clear-to-0 work.
#include "c3d_internal.hpp"
#include <3ds.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <malloc.h>
#include <sys/stat.h>
// A system dialog (the license name keyboard) is open: the swap draws it.
bool vitaCommonDialog = false;

using namespace c3dvita;

extern "C" {
C3D_FVec C3D_FVUnif[2][C3D_FVUNIF_COUNT];
C3D_IVec C3D_IVUnif[2][C3D_IVUNIF_COUNT];
u16 C3D_BoolUnifs[2];
bool C3D_FVUnifDirty[2][C3D_FVUNIF_COUNT];
bool C3D_IVUnifDirty[2][C3D_IVUNIF_COUNT];
bool C3D_BoolUnifsDirty[2];
// Newlib heap for the game (Kinoko's arena, menus, sound tables). The GPU
// pools (LINEAR, textures, render targets) take most of the rest at vglInit.
unsigned int _newlib_heap_size_user = 160 * 1024 * 1024; // incl. Kinoko's 48 MiB (MEMORY_SPACE_SIZE)
unsigned int sceUserMainThreadStackSize = 1024 * 1024;
}

namespace {

// Internal resolution: render targets are this many times their canvas size.
// 1.5 (the default, for 60 FPS races) renders the 16:9 top screen at 636x360;
// quality.txt "resolution=2" (1 to 2) gives 848x480 for more GPU time. Read
// once, before any target exists.
float Scale = 1.5f;
// Canvas units to target pixels: an edge, and a span from an edge.
inline int scaled(int v) { return int(std::lround(float(v) * Scale)); }
inline int scaledSpan(int from, int size) { return scaled(from + size) - scaled(from); }
constexpr int DisplayWidth = 960, DisplayHeight = 544;

bool displayReady = false;
// Vertex attribute arrays left enabled by the last draw: enable/disable only
// on change (the disabled default value is always 0,0,0,1).
bool attribEnabled[16] = {};

// ---- Textures -------------------------------------------------------------------
// One mip level converted for GL: ETC1 blocks or RGBA8 rows.
struct Converted {
	bool etc1 = false;
	unsigned w = 0, h = 0;
	std::vector<uint8_t> data;
};
struct GpuTexture {
	// Levels converted where the data was flushed (often the asset loader's
	// thread), so the drawing thread only uploads them.
	std::vector<Converted> prepared;
	GLuint gl = 0;
	bool dirty = true;
	bool renderTarget = false;
	u32 appliedParam = ~0u;
	size_t bytes = 0;
};
LightLock textureLock;
std::map<uintptr_t, GpuTexture*> textures; // by tex->data
std::vector<GLuint> deadTextures, deadFramebuffers, deadRenderbuffers;

GpuTexture* findTexture(const void* data) {
	auto it = textures.find(uintptr_t(data));
	return it == textures.end() ? nullptr : it->second;
}
// The texture whose CPU data contains addr (C3D_SyncTextureCopy targets).
GpuTexture* textureContaining(const void* addr) {
	auto it = textures.upper_bound(uintptr_t(addr));
	if (it == textures.begin()) return nullptr;
	--it;
	return uintptr_t(addr) < it->first + it->second->bytes ? it->second : nullptr;
}

uint8_t mortonTable[64];
void initMorton() {
	for (unsigned y = 0; y < 8; ++y)
		for (unsigned x = 0; x < 8; ++x) {
			unsigned m = 0;
			for (unsigned b = 0; b < 3; ++b) m |= ((x >> b) & 1u) << (2 * b) | ((y >> b) & 1u) << (2 * b + 1);
			mortonTable[y * 8 + x] = uint8_t(m);
		}
}
// PICA tiled texel (x, y), y counting memory rows. Memory row 0 is the top
// of the image (texcoord v = 1): uploads write it to GL's last row.
inline unsigned tiled(unsigned x, unsigned y, unsigned width) {
	return ((y >> 3) * (width >> 3) + (x >> 3)) * 64 + mortonTable[(y & 7) * 8 + (x & 7)];
}

// ETC1 block decode (Azahar's etc1.cpp): texel index 4x+y, y along memory rows.
const uint8_t Etc1Modifiers[8][2] = {{2, 8}, {5, 17}, {9, 29}, {13, 42}, {18, 60}, {24, 80}, {33, 106}, {47, 183}};
inline int expand5(int v) { return (v << 3) | (v >> 2); }
inline int expand4(int v) { return v * 17; }
void decodeEtc1Texel(uint64_t block, unsigned x, unsigned y, uint8_t* rgb) {
	const unsigned texel = 4 * x + y;
	if ((block >> 32) & 1) std::swap(x, y); // flip
	int r, g, b;
	if ((block >> 33) & 1) { // differential
		auto sext3 = [](int v) { return v & 4 ? v - 8 : v; };
		r = int((block >> 59) & 31); g = int((block >> 51) & 31); b = int((block >> 43) & 31);
		if (x >= 2) { r += sext3(int((block >> 56) & 7)); g += sext3(int((block >> 48) & 7)); b += sext3(int((block >> 40) & 7)); }
		r = expand5(r & 31); g = expand5(g & 31); b = expand5(b & 31);
	} else if (x < 2) {
		r = expand4(int((block >> 60) & 15)); g = expand4(int((block >> 52) & 15)); b = expand4(int((block >> 44) & 15));
	} else {
		r = expand4(int((block >> 56) & 15)); g = expand4(int((block >> 48) & 15)); b = expand4(int((block >> 40) & 15));
	}
	const unsigned table = x < 2 ? unsigned((block >> 37) & 7) : unsigned((block >> 34) & 7);
	int modifier = Etc1Modifiers[table][(block >> texel) & 1];
	if ((block >> (16 + texel)) & 1) modifier = -modifier;
	rgb[0] = uint8_t(std::clamp(r + modifier, 0, 255));
	rgb[1] = uint8_t(std::clamp(g + modifier, 0, 255));
	rgb[2] = uint8_t(std::clamp(b + modifier, 0, 255));
}

size_t levelBytes(GPU_TEXCOLOR fmt, unsigned w, unsigned h) {
	static const uint8_t bits[16] = {32, 24, 16, 16, 16, 16, 16, 8, 8, 8, 4, 4, 4, 8, 0, 0};
	return size_t(w) * h * bits[fmt & 15] / 8;
}

// Flips one ETC1 block (the 64-bit word, as the PICA stores it) vertically.
// Texel 4x+y: each column's four index bits reverse. A block split into
// top/bottom halves (flip bit) also swaps its halves' colours and tables.
uint64_t flipEtc1Block(uint64_t b) {
	uint32_t planes = uint32_t(b);
	uint32_t flipped = 0;
	for (unsigned plane = 0; plane < 2; ++plane)
		for (unsigned x = 0; x < 4; ++x)
			for (unsigned y = 0; y < 4; ++y)
				if (planes >> (16 * plane + 4 * x + y) & 1) flipped |= 1u << (16 * plane + 4 * x + (3 - y));
	uint64_t out = (b & 0xFFFFFFFF00000000ull) | flipped;
	if (!((b >> 32) & 1)) return out; // left/right halves: unchanged by a vertical flip
	auto field = [](uint64_t v, unsigned at, unsigned bits) { return unsigned(v >> at) & ((1u << bits) - 1); };
	auto put = [](uint64_t& v, unsigned at, unsigned bits, unsigned value) {
		v = (v & ~(uint64_t((1u << bits) - 1) << at)) | uint64_t(value & ((1u << bits) - 1)) << at;
	};
	const unsigned table1 = field(b, 37, 3), table2 = field(b, 34, 3);
	put(out, 37, 3, table2); put(out, 34, 3, table1);
	if (!((b >> 33) & 1)) { // individual: swap each 4-bit colour pair
		for (unsigned at : {56u, 48u, 40u}) {
			const unsigned c1 = field(b, at + 4, 4), c2 = field(b, at, 4);
			put(out, at + 4, 4, c2); put(out, at, 4, c1);
		}
		return out;
	}
	// Differential: base' = base + delta, delta' = -delta, unless -delta
	// overflows (delta = -4); then use individual mode at 4-bit precision.
	bool representable = true;
	for (unsigned at : {40u, 48u, 56u}) if (field(b, at, 3) == 4) representable = false;
	if (representable) {
		for (unsigned at : {40u, 48u, 56u}) {
			const int base = int(field(b, at + 3, 5)), d = int(field(b, at, 3) ^ 4) - 4;
			put(out, at + 3, 5, unsigned(base + d)); put(out, at, 3, unsigned(-d) & 7);
		}
		return out;
	}
	put(out, 33, 1, 0);
	for (unsigned at : {40u, 48u, 56u}) {
		const int base = int(field(b, at + 3, 5)), d = int(field(b, at, 3) ^ 4) - 4;
		auto to4 = [](int c5) { return unsigned((expand5(c5 & 31) * 15 + 127) / 255); };
		put(out, at + 4, 4, to4(base + d)); put(out, at, 4, to4(base));
	}
	return out;
}

// Converts one mip level of tiled PICA data for GL.
Converted convertLevel(GPU_TEXCOLOR fmt, unsigned w, unsigned h, const uint8_t* src) {
	Converted c;
	c.w = w; c.h = h;
	if (fmt == GPU_ETC1) {
		// PICA: 8x8 tiles of four 4x4 blocks (x/4 + 2*(y/4)), each a
		// little-endian ETC1 word. GL: row-major big-endian blocks.
		std::vector<uint8_t> out(size_t(w) * h / 2);
		const unsigned bw = w / 4;
		for (unsigned ty = 0; ty < h / 8; ++ty)
			for (unsigned tx = 0; tx < w / 8; ++tx)
				for (unsigned s = 0; s < 4; ++s) {
					const uint8_t* block = src + ((ty * (w / 8) + tx) * 4 + s) * 8;
					const unsigned bx = tx * 2 + (s & 1), by = h / 4 - 1 - (ty * 2 + (s >> 1));
					uint64_t word;
					std::memcpy(&word, block, 8);
					word = flipEtc1Block(word);
					uint8_t* o = out.data() + (by * bw + bx) * 8;
					for (int i = 0; i < 8; ++i) o[i] = uint8_t(word >> (56 - 8 * i));
				}
		c.etc1 = true;
		c.data = std::move(out);
		return c;
	}
	if (fmt == GPU_ETC1A4) {
		std::vector<uint8_t> out(size_t(w) * h * 4);
		for (unsigned ty = 0; ty < h / 8; ++ty)
			for (unsigned tx = 0; tx < w / 8; ++tx)
				for (unsigned s = 0; s < 4; ++s) {
					const uint8_t* p = src + ((ty * (w / 8) + tx) * 4 + s) * 16;
					uint64_t alpha, color;
					std::memcpy(&alpha, p, 8);
					std::memcpy(&color, p + 8, 8);
					for (unsigned y = 0; y < 4; ++y)
						for (unsigned x = 0; x < 4; ++x) {
							const unsigned px = tx * 8 + (s & 1) * 4 + x, py = ty * 8 + (s >> 1) * 4 + y;
							uint8_t* o = out.data() + (size_t(h - 1 - py) * w + px) * 4;
							decodeEtc1Texel(color, x, y, o);
							o[3] = uint8_t(expand4(int((alpha >> (4 * (x * 4 + y))) & 15)));
						}
				}
		c.data = std::move(out);
		return c;
	}
	// Untile to RGBA8 GL rows with the PICA's sampled values (Azahar's
	// texture_decode.cpp): vitaGL's luminance/alpha formats need not sample
	// the same way (an alpha-only fog ramp must read 0,0,0,A).
	std::vector<uint8_t> out(size_t(w) * h * 4);
	auto u16 = [](const uint8_t* p) { return unsigned(p[0] | p[1] << 8); };
	auto x5 = [](unsigned v) { return uint8_t((v << 3) | (v >> 2)); };
	auto x6 = [](unsigned v) { return uint8_t((v << 2) | (v >> 4)); };
	for (unsigned y = 0; y < h; ++y)
		for (unsigned x = 0; x < w; ++x) {
			const unsigned t = tiled(x, y, w);
			uint8_t* o = out.data() + (size_t(h - 1 - y) * w + x) * 4;
			o[3] = 255;
			switch (fmt) {
			case GPU_RGBA8: { const uint8_t* p = src + t * 4; o[0] = p[3]; o[1] = p[2]; o[2] = p[1]; o[3] = p[0]; break; }
			case GPU_RGB8: { const uint8_t* p = src + t * 3; o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; break; }
			case GPU_RGBA5551: { const unsigned v = u16(src + t * 2); o[0] = x5(v >> 11); o[1] = x5((v >> 6) & 31); o[2] = x5((v >> 1) & 31); o[3] = (v & 1) ? 255 : 0; break; }
			case GPU_RGB565: { const unsigned v = u16(src + t * 2); o[0] = x5(v >> 11); o[1] = x6((v >> 5) & 63); o[2] = x5(v & 31); break; }
			case GPU_RGBA4: { const unsigned v = u16(src + t * 2); o[0] = uint8_t((v >> 12) * 17); o[1] = uint8_t(((v >> 8) & 15) * 17); o[2] = uint8_t(((v >> 4) & 15) * 17); o[3] = uint8_t((v & 15) * 17); break; }
			case GPU_LA8: { const uint8_t* p = src + t * 2; o[0] = o[1] = o[2] = p[1]; o[3] = p[0]; break; }
			case GPU_HILO8: { const uint8_t* p = src + t * 2; o[0] = p[1]; o[1] = p[0]; o[2] = 0; break; }
			case GPU_L8: o[0] = o[1] = o[2] = src[t]; break;
			case GPU_A8: o[0] = o[1] = o[2] = 0; o[3] = src[t]; break;
			case GPU_LA4: { const uint8_t v = src[t]; o[0] = o[1] = o[2] = uint8_t((v >> 4) * 17); o[3] = uint8_t((v & 15) * 17); break; }
			case GPU_L4: { const uint8_t v = src[t / 2]; o[0] = o[1] = o[2] = uint8_t(((t & 1) ? v >> 4 : v & 15) * 17); break; }
			case GPU_A4: { const uint8_t v = src[t / 2]; o[0] = o[1] = o[2] = 0; o[3] = uint8_t(((t & 1) ? v >> 4 : v & 15) * 17); break; }
			default: break;
			}
		}
	c.data = std::move(out);
	return c;
}
std::vector<Converted> convertTexture(const C3D_Tex& tex) {
	std::vector<Converted> levels;
	const uint8_t* data = static_cast<const uint8_t*>(tex.data);
	for (int level = 0; level <= tex.maxLevel; ++level) {
		const unsigned w = std::max(8u, unsigned(tex.width) >> level), h = std::max(8u, unsigned(tex.height) >> level);
		levels.push_back(convertLevel(tex.fmt, w, h, data));
		data += levelBytes(tex.fmt, w, h);
	}
	return levels;
}
struct UploadStats { unsigned uploads = 0, converted = 0; uint64_t us = 0; } uploadStats;
void uploadLevels(const std::vector<Converted>& levels) {
	for (size_t level = 0; level < levels.size(); ++level) {
		const Converted& c = levels[level];
		if (c.etc1) glCompressedTexImage2D(GL_TEXTURE_2D, GLint(level), GL_ETC1_RGB8_OES, GLsizei(c.w), GLsizei(c.h), 0, GLsizei(c.data.size()), c.data.data());
		else glTexImage2D(GL_TEXTURE_2D, GLint(level), GL_RGBA, GLsizei(c.w), GLsizei(c.h), 0, GL_RGBA, GL_UNSIGNED_BYTE, c.data.data());
	}
}

void applyParams(GpuTexture& t, const C3D_Tex& tex) {
	if (t.appliedParam == tex.param) return;
	t.appliedParam = tex.param;
	static const GLint wraps[4] = {GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT};
	const bool magLinear = tex.param & GPU_TEXTURE_MAG_FILTER(GPU_LINEAR);
	const bool minLinear = tex.param & GPU_TEXTURE_MIN_FILTER(GPU_LINEAR);
	const bool mipLinear = tex.param & GPU_TEXTURE_MIP_FILTER(GPU_LINEAR);
	GLint minFilter = minLinear ? GL_LINEAR : GL_NEAREST;
	if (tex.maxLevel && !t.renderTarget)
		minFilter = minLinear ? (mipLinear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST)
		                      : (mipLinear ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magLinear ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wraps[(tex.param >> 12) & 3]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wraps[(tex.param >> 8) & 3]);
}

GLuint boundTexture[3] = {~0u, ~0u, ~0u};
void flushClearOfTexture(GLuint color);
// Binds tex to a unit, uploading its data first when it changed.
void bindTexture(int unit, const C3D_Tex* tex) {
	LightLock_Lock(&textureLock);
	GpuTexture* t = tex && tex->data ? findTexture(tex->data) : nullptr;
	if (!t) {
		LightLock_Unlock(&textureLock);
		if (boundTexture[unit] != 0) { glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_2D, 0); boundTexture[unit] = 0; }
		return;
	}
	if (t->renderTarget && t->gl) {
		// A target sampled before it is drawn again still owes its clear.
		const GLuint gl = t->gl;
		LightLock_Unlock(&textureLock);
		flushClearOfTexture(gl);
		LightLock_Lock(&textureLock);
	}
	if (!t->dirty && t->gl && boundTexture[unit] == t->gl && t->appliedParam == tex->param) { LightLock_Unlock(&textureLock); return; }
	glActiveTexture(GL_TEXTURE0 + unit);
	const bool upload = t->dirty && !t->renderTarget;
	std::vector<Converted> levels;
	if (upload) levels.swap(t->prepared);
	t->dirty = false;
	LightLock_Unlock(&textureLock);
	if (!t->gl) glGenTextures(1, &t->gl);
	glBindTexture(GL_TEXTURE_2D, t->gl);
	boundTexture[unit] = t->gl;
	if (upload) {
		const uint64_t start = sceKernelGetProcessTimeWide();
		if (levels.empty()) { levels = convertTexture(*tex); ++uploadStats.converted; }
		uploadLevels(levels);
		++uploadStats.uploads;
		uploadStats.us += sceKernelGetProcessTimeWide() - start;
		t->appliedParam = ~0u;
	}
	applyParams(*t, *tex);
}

// ---- Render targets ---------------------------------------------------------------
struct Target {
	C3D_RenderTarget c3d; // first: C3D_RenderTarget* converts to Target*
	GLuint fbo = 0, color = 0, depth = 0;
	bool ownsColor = true;
	int glWidth = 0, glHeight = 0;
	// A clear waiting for the target's next use (C3D_FrameBufClear): done
	// inside the scene that draws it rather than in a scene of its own.
	GLbitfield pendingClear = 0;
	float clearColor[4] = {};
	float clearDepth = 1.f;
	int clearStencil = 0;
};
Target* targetOf(C3D_RenderTarget* t) { return reinterpret_cast<Target*>(t); }
Target* targetOfFrameBuf(C3D_FrameBuf* fb) {
	return reinterpret_cast<Target*>(reinterpret_cast<uint8_t*>(fb) - offsetof(C3D_RenderTarget, frameBuf));
}
std::vector<Target*> targets;
Target* topOutput = nullptr;

// The framebuffer last bound (bindFramebuffer); ~0u after a bind made elsewhere.
GLuint boundFramebuffer = ~0u;

bool createTargetStorage(Target& t, GLuint colorTexture, int depthFormat) {
	glGenFramebuffers(1, &t.fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
	boundFramebuffer = ~0u;
	if (!colorTexture) {
		glGenTextures(1, &t.color);
		glBindTexture(GL_TEXTURE_2D, t.color);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.glWidth, t.glHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	} else t.color = colorTexture;
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);
	if (depthFormat >= 0) {
		glGenRenderbuffers(1, &t.depth);
		glBindRenderbuffer(GL_RENDERBUFFER, t.depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, t.glWidth, t.glHeight);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, t.depth);
	}
	return true;
}

// ---- GPU state ------------------------------------------------------------------------
struct State {
	C3D_TexEnv env[6];
	u32 envBuf = 0, envBufColor = 0xFFFFFFFF;
	C3D_AttrInfo attr{};
	C3D_BufInfo buf{};
	C3D_Tex* tex[3] = {};
	uint8_t program = ProgramScene;
	// Effect state, as citro3d's C3D_Effect fields.
	bool depthEnable = true; u8 depthFunc = GPU_GREATER; u8 writeMask = GPU_WRITE_ALL;
	bool alphaEnable = false; u8 alphaFunc = GPU_ALWAYS; u8 alphaRef = 0;
	u8 blendEq[2] = {GPU_BLEND_ADD, GPU_BLEND_ADD};
	u8 blendFactor[4] = {GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA};
	u32 blendColor = 0;
	u8 cull = GPU_CULL_BACK_CCW;
	bool stencilEnable = false; u8 stencilFunc = GPU_ALWAYS; u8 stencilRef = 0, stencilInMask = 0xFF, stencilWriteMask = 0;
	u8 stencilOp[3] = {GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP};
	int viewport[4] = {0, 0, 240, 400};
	bool scissor = false; int scissorBox[4] = {};
	Target* target = nullptr;
	bool targetDirty = true;
} state;

u32 uniformVersion = 0;
uint64_t frameNumber = 0;

// Diagnostics: every 600th busy frame (up to six), the state of each draw,
// and for its first indexed draws the matrices, lighting/fog uniforms, the
// combiner and the clip-space position of the first vertex, to vita.log.
struct DrawRecord { uint8_t program, units, alphaFunc, depthFunc, depthOn, writeMask, cull, stencil; uint16_t targetW, targetH; uint32_t count; bool elements; };
std::vector<DrawRecord> frameDraws;
std::vector<std::string> frameDetails;
unsigned dumpsWritten = 0;
bool gpuTrace = false; // ux0:data/mkw/gpu-trace.txt (checked at start-up)
bool gpuSync = false;  // ux0:data/mkw/gpu-sync.txt: wait for the GPU every frame
// ux0:data/mkw/profile.txt: time each part of a draw and, once per report,
// wait for the GPU (a one-frame hitch every 300 frames).
bool profile = false;
bool dumpThisFrame() { return gpuTrace && dumpsWritten < 6 && frameNumber % 600 == 0; }
inline float reg(int r, int c) { const C3D_FVec& v = C3D_FVUnif[0][r]; return c == 0 ? v.x : c == 1 ? v.y : c == 2 ? v.z : v.w; }
void recordDraw(uint32_t count, bool elements, const void* indices = nullptr, int indexType = 1) {
	if (!dumpThisFrame() || frameDraws.size() >= 600) return;
	DrawRecord r{};
	r.program = state.program;
	for (int u = 0; u < 3; ++u) if (state.tex[u] && state.tex[u]->data) r.units |= 1 << u;
	r.alphaFunc = state.alphaEnable ? state.alphaFunc : 0xFF;
	r.depthFunc = state.depthFunc; r.depthOn = state.depthEnable; r.writeMask = state.writeMask;
	r.cull = state.cull; r.stencil = state.stencilEnable;
	r.targetW = state.target ? state.target->c3d.frameBuf.width : 0;
	r.targetH = state.target ? state.target->c3d.frameBuf.height : 0;
	r.count = count; r.elements = elements;
	frameDraws.push_back(r);
	if (!elements || frameDetails.size() >= 6 || state.program != ProgramScene || !state.buf.bufCount) return;
	char line[2048];
	int n = std::snprintf(line, sizeof line, "    draw %u detail:\n", unsigned(frameDraws.size() - 1));
	for (int row = 0; row < 8; ++row)
		n += std::snprintf(line + n, sizeof line - n, "      %s[%d] = %.4g %.4g %.4g %.4g\n", row < 4 ? "proj" : "mv", row & 3, reg(row, 0), reg(row, 1), reg(row, 2), reg(row, 3));
	n += std::snprintf(line + n, sizeof line - n, "      light %.3g %.3g %.3g %.3g tint %.3g %.3g %.3g %.3g lightDir.w %.3g fog %.4g %.4g bools %x\n",
		reg(8, 0), reg(8, 1), reg(8, 2), reg(8, 3), reg(18, 0), reg(18, 1), reg(18, 2), reg(18, 3), reg(22, 3), reg(24, 0), reg(24, 1), C3D_BoolUnifs[0]);
	n += std::snprintf(line + n, sizeof line - n, "      tev");
	for (int i = 0; i < 6; ++i)
		n += std::snprintf(line + n, sizeof line - n, " [%x/%x %x/%x op%x s%u%u]", state.env[i].srcRgb, state.env[i].srcAlpha, state.env[i].funcRgb, state.env[i].funcAlpha, state.env[i].opAll, state.env[i].scaleRgb, state.env[i].scaleAlpha);
	n += std::snprintf(line + n, sizeof line - n, " buf %x color %08x\n", state.envBuf, unsigned(state.envBufColor));
	// The first vertex through modelView and projection (unskinned).
	const C3D_BufCfg& cfg = state.buf.buffers[0];
	const unsigned stride = (cfg.flags[1] >> 16) & 0xFFF;
	const unsigned index = indices ? (indexType == C3D_UNSIGNED_SHORT ? *static_cast<const u16*>(indices) : *static_cast<const u8*>(indices)) : 0;
	const float* v = reinterpret_cast<const float*>(uintptr_t(cfg.offset) + size_t(index) * stride);
	float mv[4], clip[4];
	for (int row = 0; row < 4; ++row) mv[row] = reg(4 + row, 0) * v[0] + reg(4 + row, 1) * v[1] + reg(4 + row, 2) * v[2] + reg(4 + row, 3);
	for (int row = 0; row < 4; ++row) clip[row] = reg(row, 0) * mv[0] + reg(row, 1) * mv[1] + reg(row, 2) * mv[2] + reg(row, 3) * mv[3];
	n += std::snprintf(line + n, sizeof line - n, "      v0 %.4g %.4g %.4g stride %u -> view %.4g %.4g %.4g -> clip %.4g %.4g %.4g %.4g\n",
		v[0], v[1], v[2], stride, mv[0], mv[1], mv[2], clip[0], clip[1], clip[2], clip[3]);
	frameDetails.emplace_back(line);
}
void endFrameDraws() {
	if (dumpThisFrame() && frameDraws.size() >= 20) {
		++dumpsWritten;
		std::printf("c3dvita: frame %u: %u draws\n", unsigned(frameNumber), unsigned(frameDraws.size()));
		size_t detail = 0;
		for (size_t i = 0; i < frameDraws.size(); ++i) {
			const DrawRecord& r = frameDraws[i];
			std::printf("  %3u %s n=%-6u vs%u units%x alpha%02x depth%u/%u mask%02x cull%u stencil%u target%ux%u\n",
				unsigned(i), r.elements ? "elem" : "arr ", unsigned(r.count), r.program, r.units, r.alphaFunc,
				r.depthOn, r.depthFunc, r.writeMask, r.cull, r.stencil, r.targetW, r.targetH);
		}
		for (; detail < frameDetails.size(); ++detail) std::fputs(frameDetails[detail].c_str(), stdout);
	}
	frameDraws.clear();
	frameDetails.clear();
}

GLenum glTestFunc(u8 f) {
	static const GLenum funcs[8] = {GL_NEVER, GL_ALWAYS, GL_EQUAL, GL_NOTEQUAL, GL_LESS, GL_LEQUAL, GL_GREATER, GL_GEQUAL};
	return funcs[f & 7];
}
// Depth is stored as 1 - PICA depth (c3d_shader.cpp): mirror the compare.
GLenum glDepthTestFunc(u8 f) {
	static const GLenum funcs[8] = {GL_NEVER, GL_ALWAYS, GL_EQUAL, GL_NOTEQUAL, GL_GREATER, GL_GEQUAL, GL_LESS, GL_LEQUAL};
	return funcs[f & 7];
}
GLenum glBlendFactor(u8 f) {
	static const GLenum factors[15] = {GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
		GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA,
		// vitaGL has no constant blend colour; the game never selects these.
		GL_ONE, GL_ONE, GL_ONE, GL_ONE, GL_SRC_ALPHA_SATURATE};
	return f < 15 ? factors[f] : GL_ONE;
}
GLenum glBlendEquation(u8 e) {
	static const GLenum eqs[5] = {GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX};
	return e < 5 ? eqs[e] : GL_FUNC_ADD;
}
GLenum glStencilOperation(u8 op) {
	static const GLenum ops[8] = {GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT, GL_INCR_WRAP, GL_DECR_WRAP};
	return ops[op & 7];
}
void unpackColor(u32 c, float* out) { // PICA 0xAABBGGRR
	out[0] = float(c & 255) / 255.f; out[1] = float((c >> 8) & 255) / 255.f;
	out[2] = float((c >> 16) & 255) / 255.f; out[3] = float(c >> 24) / 255.f;
}

void bindFramebuffer(GLuint fbo);
void bindTarget(Target* t) { bindFramebuffer(t ? t->fbo : 0); }

// The GL fixed-function state last applied; only differences are sent. Clears
// and the present change GL state behind it: they invalidate the copy.
struct AppliedState {
	bool valid = false;
	Target* target = nullptr;
	int viewport[4] = {};
	bool scissor = false; int scissorBox[4] = {};
	bool depthEnable = false; u8 depthFunc = 0, writeMask = 0;
	u8 blendEq[2] = {}, blendFactor[4] = {};
	u8 cull = 0;
	bool stencilEnable = false; u8 stencilFunc = 0, stencilRef = 0, stencilInMask = 0, stencilWriteMask = 0, stencilOp[3] = {};
} applied;
void invalidateApplied() { applied.valid = false; for (auto& t : boundTexture) t = ~0u; }

void flushClear(Target* t);
void applyFixedState() {
	AppliedState& a = applied;
	// Binds the target and clears it; the clear's state changes invalidate a.
	if (state.target && state.target->pendingClear) flushClear(state.target);
	const bool all = !a.valid;
	if (state.targetDirty || all || a.target != state.target) { bindTarget(state.target); state.targetDirty = false; a.target = state.target; }
	if (all || std::memcmp(a.viewport, state.viewport, sizeof a.viewport)) {
		glViewport(scaled(state.viewport[0]), scaled(state.viewport[1]), scaledSpan(state.viewport[0], state.viewport[2]), scaledSpan(state.viewport[1], state.viewport[3]));
		std::memcpy(a.viewport, state.viewport, sizeof a.viewport);
	}
	if (all || a.scissor != state.scissor || (state.scissor && std::memcmp(a.scissorBox, state.scissorBox, sizeof a.scissorBox))) {
		if (state.scissor) {
			glEnable(GL_SCISSOR_TEST);
			glScissor(scaled(state.scissorBox[0]), scaled(state.scissorBox[1]), scaledSpan(state.scissorBox[0], state.scissorBox[2]), scaledSpan(state.scissorBox[1], state.scissorBox[3]));
		} else glDisable(GL_SCISSOR_TEST);
		a.scissor = state.scissor; std::memcpy(a.scissorBox, state.scissorBox, sizeof a.scissorBox);
	}
	// PICA writes depth only while the depth test is on, as GL does.
	if (all || a.depthEnable != state.depthEnable || a.depthFunc != state.depthFunc) {
		if (state.depthEnable) { glEnable(GL_DEPTH_TEST); glDepthFunc(glDepthTestFunc(state.depthFunc)); }
		else glDisable(GL_DEPTH_TEST);
		a.depthEnable = state.depthEnable; a.depthFunc = state.depthFunc;
	}
	if (all || a.writeMask != state.writeMask) {
		glDepthMask((state.writeMask & GPU_WRITE_DEPTH) ? GL_TRUE : GL_FALSE);
		glColorMask(state.writeMask & GPU_WRITE_RED, state.writeMask & GPU_WRITE_GREEN, state.writeMask & GPU_WRITE_BLUE, state.writeMask & GPU_WRITE_ALPHA);
		a.writeMask = state.writeMask;
	}
	if (all) glEnable(GL_BLEND);
	if (all || std::memcmp(a.blendEq, state.blendEq, 2) || std::memcmp(a.blendFactor, state.blendFactor, 4)) {
		glBlendEquationSeparate(glBlendEquation(state.blendEq[0]), glBlendEquation(state.blendEq[1]));
		glBlendFuncSeparate(glBlendFactor(state.blendFactor[0]), glBlendFactor(state.blendFactor[1]),
			glBlendFactor(state.blendFactor[2]), glBlendFactor(state.blendFactor[3]));
		std::memcpy(a.blendEq, state.blendEq, 2); std::memcpy(a.blendFactor, state.blendFactor, 4);
	}
	if (all || a.cull != state.cull) {
		if (state.cull == GPU_CULL_NONE) glDisable(GL_CULL_FACE);
		else { glEnable(GL_CULL_FACE); glFrontFace(GL_CCW); glCullFace(state.cull == GPU_CULL_FRONT_CCW ? GL_FRONT : GL_BACK); }
		a.cull = state.cull;
	}
	if (all || a.stencilEnable != state.stencilEnable || (state.stencilEnable && (a.stencilFunc != state.stencilFunc || a.stencilRef != state.stencilRef ||
		a.stencilInMask != state.stencilInMask || a.stencilWriteMask != state.stencilWriteMask || std::memcmp(a.stencilOp, state.stencilOp, 3)))) {
		if (state.stencilEnable) {
			glEnable(GL_STENCIL_TEST);
			glStencilFunc(glTestFunc(state.stencilFunc), state.stencilRef, state.stencilInMask);
			glStencilMask(state.stencilWriteMask);
			glStencilOp(glStencilOperation(state.stencilOp[0]), glStencilOperation(state.stencilOp[1]), glStencilOperation(state.stencilOp[2]));
		} else glDisable(GL_STENCIL_TEST);
		a.stencilEnable = state.stencilEnable; a.stencilFunc = state.stencilFunc; a.stencilRef = state.stencilRef;
		a.stencilInMask = state.stencilInMask; a.stencilWriteMask = state.stencilWriteMask; std::memcpy(a.stencilOp, state.stencilOp, 3);
	}
	a.valid = true;
}

Program* currentProgram = nullptr;

// ---- LINEAR data the CPU rewrites -------------------------------------------------
// The GPU reads LINEAR vertex and index data in place, a frame or two after
// the draw is recorded. The 3DS game rewrites some of it (sprites, effects,
// skinned vertices) every frame, relying on citro3d's FrameBegin waiting for
// the GPU. Instead of that stall, data flushed in the last few frames is
// copied into vitaGL's per-frame scratch memory at the draw; static data
// (course and model geometry) stays in place.
struct WrittenRange { uintptr_t end; uint64_t frame; };
LightLock writtenLock;
std::map<uintptr_t, WrittenRange> written; // by start
constexpr uint64_t WrittenFrames = 3;
void noteWritten(const void* address, u32 size) {
	if (!size) return;
	LightLock_Lock(&writtenLock);
	auto& r = written[uintptr_t(address)];
	r.end = std::max(r.end, uintptr_t(address) + size);
	r.frame = frameNumber;
	LightLock_Unlock(&writtenLock);
}
bool recentlyWritten(uintptr_t begin, uintptr_t end) {
	bool hit = false;
	LightLock_Lock(&writtenLock);
	auto it = written.lower_bound(end);
	while (it != written.begin()) {
		--it;
		if (it->second.end > begin) { hit = true; break; }
		// Flushed ranges are short; one starting this far before cannot reach.
		if (it->first + (4u << 20) < begin) break;
	}
	LightLock_Unlock(&writtenLock);
	return hit;
}
void pruneWritten() {
	LightLock_Lock(&writtenLock);
	for (auto it = written.begin(); it != written.end();)
		it = it->second.frame + WrittenFrames < frameNumber ? written.erase(it) : std::next(it);
	LightLock_Unlock(&writtenLock);
}
// Index range of an indexed draw, cached per (indices, count): model index
// data is static, so each range is scanned once.
struct IndexKey {
	uintptr_t at; uint32_t count;
	bool operator<(const IndexKey& o) const { return at != o.at ? at < o.at : count < o.count; }
};
std::map<IndexKey, std::pair<uint32_t, uint32_t>> indexRanges;
std::pair<uint32_t, uint32_t> indexRange(const void* indices, int count, int type, bool rescan) {
	const IndexKey key{uintptr_t(indices), uint32_t(count) | (type == C3D_UNSIGNED_SHORT ? 0x80000000u : 0)};
	if (!rescan) {
		auto it = indexRanges.find(key);
		if (it != indexRanges.end()) return it->second;
	}
	uint32_t lo = ~0u, hi = 0;
	for (int i = 0; i < count; ++i) {
		const uint32_t v = type == C3D_UNSIGNED_SHORT ? static_cast<const u16*>(indices)[i] : static_cast<const u8*>(indices)[i];
		lo = std::min(lo, v);
		hi = std::max(hi, v);
	}
	if (lo > hi) lo = hi = 0;
	return indexRanges[key] = {lo, hi + 1};
}
// glUs: time inside C3D_Draw* (state, uniforms, textures, vitaGL's draw);
// the rest of the draw thread's work is the game's own (skinning, effects).
// targetSwitches: framebuffer changes, each a new GXM scene. gpuTailUs: on
// the report frame, how long the GPU ran on after the CPU presented.
struct FrameStats {
	unsigned frames = 0, draws = 0, copies = 0, targetSwitches = 0;
	uint64_t copiedBytes = 0, beginWaitUs = 0, frameUs = 0, lastPresentUs = 0, swapUs = 0, workUs = 0, frameStartUs = 0, glUs = 0, gpuTailUs = 0;
	// profile.txt only: prepareDraw's parts and vitaGL's draw call.
	uint64_t uniformUs = 0, textureUs = 0, stateUs = 0, attributeUs = 0, drawUs = 0;
} stats;
inline uint64_t profileNow() { return profile ? sceKernelGetProcessTimeWide() : 0; }
void bindFramebuffer(GLuint fbo) {
	if (fbo != boundFramebuffer) { ++stats.targetSwitches; boundFramebuffer = fbo; }
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
}
// Each bind-and-clear is a GXM scene that stores the whole target: a clear
// done on its own costs a full store, and its target's next draw a reload.
// Clears wait for the target's first use and run in that scene.
void flushClear(Target* t) {
	if (!t->pendingClear) return;
	bindFramebuffer(t->fbo);
	glDisable(GL_SCISSOR_TEST);
	glViewport(0, 0, t->glWidth, t->glHeight);
	if (t->pendingClear & GL_COLOR_BUFFER_BIT) {
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(t->clearColor[0], t->clearColor[1], t->clearColor[2], t->clearColor[3]);
	}
	if (t->pendingClear & GL_DEPTH_BUFFER_BIT) {
		glDepthMask(GL_TRUE);
		glStencilMask(0xFF);
		glClearDepthf(t->clearDepth);
		glClearStencil(t->clearStencil);
	}
	glClear(t->pendingClear);
	t->pendingClear = 0;
	state.targetDirty = true;
	invalidateApplied();
}
void flushClearOfTexture(GLuint color) {
	for (Target* t : targets)
		if (t->color == color && t->pendingClear) flushClear(t);
}

void prepareDraw(uint32_t firstVertex, uint32_t endVertex) {
	FragmentKey key;
	std::memset(&key, 0, sizeof key);
	for (int i = 0; i < 6; ++i) {
		const C3D_TexEnv& e = state.env[i];
		key.srcRgb[i] = e.srcRgb; key.srcAlpha[i] = e.srcAlpha; key.opAll[i] = e.opAll;
		key.funcRgb[i] = e.funcRgb; key.funcAlpha[i] = e.funcAlpha;
		key.scaleRgb[i] = e.scaleRgb; key.scaleAlpha[i] = e.scaleAlpha;
	}
	key.bufferUpdate = state.envBuf;
	key.alphaFunc = state.alphaEnable ? state.alphaFunc : 0xFF;
	for (int u = 0; u < 3; ++u) if (state.tex[u] && state.tex[u]->data) key.units |= 1 << u;
	key.projection0 = state.tex[0] && C3D_TexGetType(state.tex[0]) == GPU_TEX_PROJECTION;
	key.vertexProgram = state.program;
	// Consecutive draws usually share their combiner: skip the lookup.
	static FragmentKey lastKey;
	static Program* lastProgram = nullptr;
	Program* p = lastProgram && !std::memcmp(&key, &lastKey, sizeof key) ? lastProgram : programFor(key);
	lastKey = key; lastProgram = p;
	const uint64_t t0 = profileNow();
	if (p != currentProgram) { glUseProgram(p->gl); currentProgram = p; }

	// Vertex uniforms: c0-c24 and the bone palette (c25-c81) are versioned
	// apart; bones go only to skinned draws, and only after they change.
	bool constChanged = false, bonesChanged = false;
	for (int i = 0; i < 25; ++i)
		if (C3D_FVUnifDirty[0][i]) { C3D_FVUnifDirty[0][i] = false; constChanged = true; }
	for (int i = 25; i < 82; ++i)
		if (C3D_FVUnifDirty[0][i]) { C3D_FVUnifDirty[0][i] = false; bonesChanged = true; }
	if (C3D_BoolUnifsDirty[0]) { C3D_BoolUnifsDirty[0] = false; constChanged = true; }
	if (constChanged) ++uniformVersion;
	static uint32_t bonesVersion = 0;
	if (bonesChanged) ++bonesVersion;
	if (p->uploadedConst != uniformVersion) {
		p->uploadedConst = uniformVersion;
		glUniform4fv(p->vsConst, programRegisters(state.program), reinterpret_cast<const GLfloat*>(C3D_FVUnif[0]));
		if (p->vsFlags >= 0) {
			const u16 b = C3D_BoolUnifs[0];
			glUniform4f(p->vsFlags, b & 1 ? 1.f : 0.f, b & 2 ? 1.f : 0.f, b & 4 ? 1.f : 0.f, b & 8 ? 1.f : 0.f);
			glUniform1f(p->vsFlags2, b & 16 ? 1.f : 0.f);
		}
	}
	if (p->vsBones >= 0 && (C3D_BoolUnifs[0] & 8) && p->uploadedBones != bonesVersion) {
		p->uploadedBones = bonesVersion;
		glUniform4fv(p->vsBones, 57, reinterpret_cast<const GLfloat*>(&C3D_FVUnif[0][25]));
	}
	// Fragment constants, when they differ from the program's last upload.
	bool konstSame = p->fragmentUploaded && p->bufUploaded == state.envBufColor && p->alphaUploaded == int(state.alphaRef);
	for (int i = 0; konstSame && i < 6; ++i) konstSame = p->konstUploaded[i] == state.env[i].color;
	if (!konstSame) {
		float konst[6][4];
		for (int i = 0; i < 6; ++i) { unpackColor(state.env[i].color, konst[i]); p->konstUploaded[i] = state.env[i].color; }
		glUniform4fv(p->konst, 6, &konst[0][0]);
		float buffer[4]; unpackColor(state.envBufColor, buffer);
		glUniform4fv(p->bufColor, 1, buffer);
		if (p->alphaRef >= 0) glUniform1f(p->alphaRef, float(state.alphaRef));
		p->bufUploaded = state.envBufColor; p->alphaUploaded = int(state.alphaRef); p->fragmentUploaded = true;
	}

	const uint64_t t1 = profileNow();
	for (int u = 0; u < 3; ++u) bindTexture(u, state.tex[u]);
	const uint64_t t2 = profileNow();
	applyFixedState();
	const uint64_t t3 = profileNow();
	if (profile) { stats.uniformUs += t1 - t0; stats.textureUs += t2 - t1; stats.stateUs += t3 - t2; }

	// Attribute loaders (AttrInfo) fed from the buffers (BufInfo) in place.
	static GLuint arrayBuffers[12] = {};
	if (!arrayBuffers[0]) glGenBuffers(12, arrayBuffers);
	bool enabled[16] = {};
	for (int b = 0; b < state.buf.bufCount; ++b) {
		const C3D_BufCfg& cfg = state.buf.buffers[b];
		const u64 permutation = u64(cfg.flags[0]) | (u64(cfg.flags[1] & 0xFFFF) << 32);
		const unsigned stride = (cfg.flags[1] >> 16) & 0xFFF, count = cfg.flags[1] >> 28;
		glBindBuffer(GL_ARRAY_BUFFER, arrayBuffers[b]);
		const uint8_t* base = reinterpret_cast<const uint8_t*>(uintptr_t(cfg.offset));
		const uintptr_t from = uintptr_t(base) + size_t(firstVertex) * stride, to = uintptr_t(base) + size_t(endVertex) * stride;
		if (to > from && recentlyWritten(from, to)) {
			// Rewritten data: the draw reads a copy (see noteWritten).
			if (auto* copy = static_cast<uint8_t*>(vglAllocFromScratch(to - from))) {
				std::memcpy(copy, reinterpret_cast<const void*>(from), to - from);
				base = copy - size_t(firstVertex) * stride;
				++stats.copies;
				stats.copiedBytes += to - from;
			}
		}
		vglBufferData(GL_ARRAY_BUFFER, base);
		unsigned offset = 0;
		for (unsigned j = 0; j < count; ++j) {
			const unsigned loader = (permutation >> (4 * j)) & 0xF;
			if (loader >= 12) { offset += 4 * (loader - 11); continue; } // padding entries
			const u64 attrPermutation = state.attr.permutation;
			const unsigned reg = (attrPermutation >> (4 * loader)) & 0xF;
			const u32 flags = loader < 8 ? state.attr.flags[0] >> (4 * loader) : state.attr.flags[1] >> (4 * (loader - 8));
			const unsigned format = flags & 3, components = ((flags >> 2) & 3) + 1;
			static const GLenum types[4] = {GL_BYTE, GL_UNSIGNED_BYTE, GL_SHORT, GL_FLOAT};
			static const unsigned sizes[4] = {1, 1, 2, 4};
			if (!attribEnabled[reg]) { glEnableVertexAttribArray(reg); attribEnabled[reg] = true; }
			glVertexAttribPointer(reg, GLint(components), types[format], GL_FALSE, GLsizei(stride), reinterpret_cast<const void*>(uintptr_t(offset)));
			enabled[reg] = true;
			offset += components * sizes[format];
		}
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	for (unsigned reg = 0; reg < 5; ++reg)
		if (!enabled[reg] && attribEnabled[reg]) { glDisableVertexAttribArray(reg); glVertexAttrib4f(reg, 0, 0, 0, 1); attribEnabled[reg] = false; }
	if (profile) stats.attributeUs += profileNow() - t3;
}

GLenum glPrimitive(GPU_Primitive_t p) {
	switch (p) {
	case GPU_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
	case GPU_TRIANGLE_FAN: return GL_TRIANGLE_FAN;
	default: return GL_TRIANGLES;
	}
}

// Deferred work for the thread that owns GL (frame boundaries).
void collectGarbage() {
	LightLock_Lock(&textureLock);
	std::vector<GLuint> deadTex, deadFbo, deadRb;
	deadTex.swap(deadTextures); deadFbo.swap(deadFramebuffers); deadRb.swap(deadRenderbuffers);
	LightLock_Unlock(&textureLock);
	if (!deadTex.empty()) glDeleteTextures(GLsizei(deadTex.size()), deadTex.data());
	if (!deadFbo.empty()) glDeleteFramebuffers(GLsizei(deadFbo.size()), deadFbo.data());
	if (!deadRb.empty()) glDeleteRenderbuffers(GLsizei(deadRb.size()), deadRb.data());
}

void present(Target* top) {
	flushClear(top); // cleared but never drawn: show the clear
	bindFramebuffer(0);
	glViewport(0, 0, DisplayWidth, DisplayHeight);
	glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST);
	glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	// The landscape picture (the portrait target turned), scaled to the
	// display's height: 16:9 fills it, a 5:3 one is letterboxed.
	const float h = float(DisplayHeight);
	const float w = std::min(float(DisplayWidth), h * float(top->c3d.frameBuf.height) / float(top->c3d.frameBuf.width));
	const float x0 = (float(DisplayWidth) - w) * 0.5f;
	const float left = x0 / DisplayWidth * 2 - 1, right = (x0 + w) / DisplayWidth * 2 - 1;
	// Landscape (x, y from the top-left) shows portrait window (240 - y, W - x)
	// of a W-wide canvas: u = 1 - y/240, v = 1 - x/W.
	const float quad[4][4] = {
		{left, 1, 1, 1}, {right, 1, 1, 0}, {left, -1, 0, 1}, {right, -1, 0, 0}, // x, y, u, v
	};
	glUseProgram(presentProgram());
	currentProgram = nullptr;
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, top->color);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	for (int i = 2; i < 5; ++i) { glDisableVertexAttribArray(i); attribEnabled[i] = false; }
	glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); attribEnabled[0] = attribEnabled[1] = true;
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, &quad[0][0]);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, &quad[0][2]);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	const uint64_t swapStart = sceKernelGetProcessTimeWide();
	vglSwapBuffers(vitaCommonDialog ? GL_TRUE : GL_FALSE);
	stats.swapUs += sceKernelGetProcessTimeWide() - swapStart;
	invalidateApplied();
	state.targetDirty = true;
}

} // namespace

// ---- Display start-up (gfxInitDefault) and LINEAR memory -------------------------------
namespace c3dvita {
void initDisplay() {
	if (displayReady) return;
	// The Vita shows no console: printf output (including this layer's
	// shader and status lines) goes to a log beside the game files.
	mkdir("ux0:data", 0777);
	mkdir(MKW_DATA_ROOT, 0777);
	if (std::freopen(MKW_DATA_ROOT "/vita.log", "w", stdout)) std::setvbuf(stdout, nullptr, _IOLBF, 0);
	initMorton();
	LightLock_Init(&textureLock);
	LightLock_Init(&writtenLock);
	auto marker = [](const char* path) { FILE* f = std::fopen(path, "rb"); if (f) std::fclose(f); return f != nullptr; };
	gpuTrace = marker(MKW_DATA_ROOT "/gpu-trace.txt");
	gpuSync = marker(MKW_DATA_ROOT "/gpu-sync.txt");
	profile = marker(MKW_DATA_ROOT "/profile.txt");
	if (FILE* f = std::fopen(MKW_DATA_ROOT "/quality.txt", "rb")) {
		char line[128];
		float value = 0;
		while (std::fgets(line, sizeof line, f))
			if (std::sscanf(line, " resolution = %f", &value) == 1) Scale = std::clamp(value, 1.f, 2.f);
		std::fclose(f);
	}
	std::printf("c3dvita: internal resolution %.2fx\n", double(Scale));
	vglUseExtraMem(GL_FALSE); // LINEAR allocations must stay GPU-mapped
	vglInitExtended(0, DisplayWidth, DisplayHeight, 16 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
	vglWaitVblankStart(GL_TRUE);
	displayReady = true;
	std::printf("c3dvita: display ready, RAM pool %u KiB, VRAM pool %u KiB\n",
		unsigned(vglMemTotal(VGL_MEM_RAM) / 1024), unsigned(vglMemTotal(VGL_MEM_VRAM) / 1024));
}
} // namespace c3dvita

extern "C" void* linearAlloc(size_t size) { c3dvita::initDisplay(); return vglMemalign(0x80, u32(size ? size : 1)); }
extern "C" void* linearMemAlign(size_t size, size_t alignment) {
	c3dvita::initDisplay();
	return vglMemalign(u32(alignment < 0x80 ? 0x80 : alignment), u32(size ? size : 1));
}
extern "C" void* linearRealloc(void* mem, size_t size) { return vglRealloc(mem, u32(size)); }
extern "C" size_t linearGetSize(void* mem) { return mem ? vglMallocUsableSize(mem) : 0; }
// The GPU may still read a block freed during a frame; vitaGL frees it once
// the GPU is done.
extern "C" void linearFree(void* mem) { if (mem) vglLazyFree(mem); }
extern "C" u32 linearSpaceFree(void) {
	c3dvita::initDisplay();
	const size_t free = vglMemFree(VGL_MEM_RAM);
	// Leave room for vitaGL's own per-frame allocations.
	return u32(free > (8u << 20) ? free - (8u << 20) : 0);
}

// The 3DS flushes CPU writes before the GPU reads them. LINEAR vertex data is
// read in place (uncached pool memory); a flushed range inside a texture's
// CPU data (menu movie frames) means that texture must be converted again.
extern "C" Result GSPGPU_FlushDataCache(const void* adr, u32 size) {
	noteWritten(adr, size);
	LightLock_Lock(&textureLock);
	if (GpuTexture* t = textureContaining(adr)) { t->dirty = true; t->prepared.clear(); }
	LightLock_Unlock(&textureLock);
	return 0;
}

// ---- citro3d: context ------------------------------------------------------------------
bool C3D_Init(size_t) {
	c3dvita::initDisplay();
	for (auto& e : state.env) C3D_TexEnvInit(&e);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	preloadPrograms();
	return true;
}
void C3D_Fini(void) {}
float C3D_GetCmdBufUsage(void) { return 0.f; }
void C3D_BindProgram(shaderProgram_s* program) {
	state.program = uint8_t(program && program->vertexShader ? dvleIndex(program->vertexShader->dvle) : ProgramScene);
	++uniformVersion;
}
void C3D_SetViewport(u32 x, u32 y, u32 w, u32 h) {
	state.viewport[0] = int(x); state.viewport[1] = int(y); state.viewport[2] = int(w); state.viewport[3] = int(h);
}
void C3D_SetScissor(GPU_SCISSORMODE mode, u32 left, u32 top, u32 right, u32 bottom) {
	state.scissor = mode != GPU_SCISSOR_DISABLE;
	state.scissorBox[0] = int(left); state.scissorBox[1] = int(top);
	state.scissorBox[2] = int(right) - int(left); state.scissorBox[3] = int(bottom) - int(top);
}
void C3D_DrawArrays(GPU_Primitive_t primitive, int first, int size) {
	if (!state.target) return;
	const uint64_t start = sceKernelGetProcessTimeWide();
	prepareDraw(u32(first), u32(first + size));
	++stats.draws;
	recordDraw(u32(size), false);
	const uint64_t drawStart = profileNow();
	glDrawArrays(glPrimitive(primitive), first, size);
	if (profile) stats.drawUs += profileNow() - drawStart;
	stats.glUs += sceKernelGetProcessTimeWide() - start;
}
void C3D_DrawElements(GPU_Primitive_t primitive, int count, int type, const void* indices) {
	if (!state.target || count <= 0) return;
	const uint64_t start = sceKernelGetProcessTimeWide();
	const size_t indexBytes = size_t(count) * (type == C3D_UNSIGNED_SHORT ? 2 : 1);
	const bool indicesWritten = recentlyWritten(uintptr_t(indices), uintptr_t(indices) + indexBytes);
	const auto range = indexRange(indices, count, type, indicesWritten);
	prepareDraw(range.first, range.second);
	++stats.draws;
	recordDraw(u32(count), true, indices, type);
	const void* drawIndices = indices;
	if (indicesWritten)
		if (void* copy = vglAllocFromScratch(indexBytes)) { std::memcpy(copy, indices, indexBytes); drawIndices = copy; }
	static GLuint elementBuffer = 0;
	if (!elementBuffer) glGenBuffers(1, &elementBuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementBuffer);
	vglBufferData(GL_ELEMENT_ARRAY_BUFFER, drawIndices);
	const uint64_t drawStart = profileNow();
	glDrawElements(glPrimitive(primitive), count, type == C3D_UNSIGNED_SHORT ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE, nullptr);
	if (profile) stats.drawUs += profileNow() - drawStart;
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	stats.glUs += sceKernelGetProcessTimeWide() - start;
}

// ---- Texture combiner -------------------------------------------------------------------
C3D_TexEnv* C3D_GetTexEnv(int id) { return &state.env[id]; }
void C3D_SetTexEnv(int id, C3D_TexEnv* env) { state.env[id] = *env; }
void C3D_DirtyTexEnv(C3D_TexEnv*) {}
void C3D_TexEnvBufUpdate(int mode, int mask) {
	u32 val = state.envBuf;
	mask &= 0xF;
	if (mode & C3D_RGB) { val &= ~(0xFu << 8); val |= u32(mask) << 8; }
	if (mode & C3D_Alpha) { val &= ~(0xFu << 12); val |= u32(mask) << 12; }
	state.envBuf = val;
}
void C3D_TexEnvBufColor(u32 color) { state.envBufColor = color; }

// ---- Effects ----------------------------------------------------------------------------
void C3D_DepthMap(bool, float, float) {} // the game keeps citro3d's default map
void C3D_CullFace(GPU_CULLMODE mode) { state.cull = u8(mode); }
void C3D_StencilTest(bool enable, GPU_TESTFUNC function, int ref, int inputMask, int writeMask) {
	state.stencilEnable = enable; state.stencilFunc = u8(function);
	state.stencilRef = u8(ref); state.stencilInMask = u8(inputMask); state.stencilWriteMask = u8(writeMask);
}
void C3D_StencilOp(GPU_STENCILOP sfail, GPU_STENCILOP dfail, GPU_STENCILOP pass) {
	state.stencilOp[0] = u8(sfail); state.stencilOp[1] = u8(dfail); state.stencilOp[2] = u8(pass);
}
void C3D_BlendingColor(u32 color) { state.blendColor = color; }
void C3D_EarlyDepthTest(bool, GPU_EARLYDEPTHFUNC, u32) {}
void C3D_DepthTest(bool enable, GPU_TESTFUNC function, GPU_WRITEMASK writemask) {
	state.depthEnable = enable; state.depthFunc = u8(function); state.writeMask = u8(writemask);
}
void C3D_AlphaTest(bool enable, GPU_TESTFUNC function, int ref) {
	state.alphaEnable = enable; state.alphaFunc = u8(function); state.alphaRef = u8(ref);
}
void C3D_AlphaBlend(GPU_BLENDEQUATION colorEq, GPU_BLENDEQUATION alphaEq, GPU_BLENDFACTOR srcClr, GPU_BLENDFACTOR dstClr, GPU_BLENDFACTOR srcAlpha, GPU_BLENDFACTOR dstAlpha) {
	state.blendEq[0] = u8(colorEq); state.blendEq[1] = u8(alphaEq);
	state.blendFactor[0] = u8(srcClr); state.blendFactor[1] = u8(dstClr);
	state.blendFactor[2] = u8(srcAlpha); state.blendFactor[3] = u8(dstAlpha);
}
void C3D_ColorLogicOp(GPU_LOGICOP) {}
void C3D_FragOpMode(GPU_FRAGOPMODE) {}
void C3D_FragOpShadow(float, float) {}

// ---- Attributes and buffers ---------------------------------------------------------------
// Loader n: format | (components - 1) << 2 in nibble n of flags; its input
// register in nibble n of permutation (citro3d's own encoding).
void AttrInfo_Init(C3D_AttrInfo* info) { std::memset(info, 0, sizeof *info); }
int AttrInfo_AddLoader(C3D_AttrInfo* info, int regId, GPU_FORMATS format, int count) {
	if (info->attrCount == 12 || regId < 0 || regId > 15 || count < 1 || count > 4) return -1;
	const int id = info->attrCount++;
	const u32 nibble = u32(format) | u32(count - 1) << 2;
	if (id < 8) info->flags[0] |= nibble << (4 * id);
	else info->flags[1] |= nibble << (4 * (id - 8));
	info->permutation |= u64(regId) << (4 * id);
	return id;
}
int AttrInfo_AddFixed(C3D_AttrInfo*, int) { return -1; }
C3D_AttrInfo* C3D_GetAttrInfo(void) { return &state.attr; }
void C3D_SetAttrInfo(C3D_AttrInfo* info) { state.attr = *info; }
void BufInfo_Init(C3D_BufInfo* info) { std::memset(info, 0, sizeof *info); }
int BufInfo_Add(C3D_BufInfo* info, const void* data, ptrdiff_t stride, int attribCount, u64 permutation) {
	if (info->bufCount == 12) return -1;
	const int id = info->bufCount++;
	C3D_BufCfg& cfg = info->buffers[id];
	cfg.offset = u32(uintptr_t(data));
	cfg.flags[0] = u32(permutation);
	cfg.flags[1] = u32((permutation >> 32) & 0xFFFF) | u32(stride & 0xFFF) << 16 | u32(attribCount) << 28;
	return id;
}
C3D_BufInfo* C3D_GetBufInfo(void) { return &state.buf; }
void C3D_SetBufInfo(C3D_BufInfo* info) { state.buf = *info; }

// ---- Textures ------------------------------------------------------------------------------
bool C3D_TexInitWithParams(C3D_Tex* tex, C3D_TexCube*, C3D_TexInitParams p) {
	if (p.type != GPU_TEX_2D && p.type != GPU_TEX_PROJECTION) return false;
	size_t total = 0;
	for (int level = 0; level <= p.maxLevel; ++level)
		total += levelBytes(p.format, std::max(8u, unsigned(p.width) >> level), std::max(8u, unsigned(p.height) >> level));
	void* data = memalign(0x80, total);
	if (!data) return false;
	std::memset(tex, 0, sizeof *tex);
	tex->data = data;
	tex->fmt = p.format;
	tex->size = levelBytes(p.format, p.width, p.height);
	tex->width = p.width;
	tex->height = p.height;
	tex->param = GPU_TEXTURE_MODE(p.type);
	if (p.format == GPU_ETC1) tex->param |= GPU_TEXTURE_ETC1_PARAM;
	tex->maxLevel = p.maxLevel;
	auto* record = new GpuTexture;
	record->bytes = total;
	LightLock_Lock(&textureLock);
	textures[uintptr_t(data)] = record;
	LightLock_Unlock(&textureLock);
	return true;
}
void C3D_TexLoadImage(C3D_Tex* tex, const void* data, GPU_TEXFACE, int level) {
	u32 size = 0;
	void* out = C3D_Tex2DGetImagePtr(tex, level, &size);
	std::memcpy(out, data, size);
	C3D_TexFlush(tex);
}
void C3D_TexGenerateMipmap(C3D_Tex*, GPU_TEXFACE) {}
void C3D_TexBind(int unitId, C3D_Tex* tex) { if (unitId >= 0 && unitId < 3) state.tex[unitId] = tex; }
void C3D_TexFlush(C3D_Tex* tex) {
	if (!tex || !tex->data) return;
	LightLock_Lock(&textureLock);
	GpuTexture* t = findTexture(tex->data);
	const bool renderTarget = t && t->renderTarget;
	LightLock_Unlock(&textureLock);
	if (!t || renderTarget) return;
	// Convert here, on the thread that wrote the data (menu and race assets
	// load on the asset loader's thread), so drawing only uploads.
	std::vector<Converted> levels = convertTexture(*tex);
	LightLock_Lock(&textureLock);
	if ((t = findTexture(tex->data))) { t->prepared.swap(levels); t->dirty = true; }
	LightLock_Unlock(&textureLock);
}
void C3D_TexDelete(C3D_Tex* tex) {
	if (!tex || !tex->data) return;
	LightLock_Lock(&textureLock);
	auto it = textures.find(uintptr_t(tex->data));
	if (it != textures.end()) {
		if (it->second->gl && !it->second->renderTarget) deadTextures.push_back(it->second->gl);
		delete it->second;
		textures.erase(it);
	}
	LightLock_Unlock(&textureLock);
	for (auto& bound : state.tex) if (bound == tex) bound = nullptr;
	std::free(tex->data);
	tex->data = nullptr;
}
void C3D_TexShadowParams(bool, float) {}

// ---- Render targets ------------------------------------------------------------------------
extern "C" C3D_RenderTarget* c3dvita_RenderTargetCreate(int width, int height, GPU_COLORBUF colorFmt, int depthFmt) {
	auto* t = new Target{};
	t->glWidth = scaled(width); t->glHeight = scaled(height);
	createTargetStorage(*t, 0, depthFmt);
	C3D_FrameBuf& fb = t->c3d.frameBuf;
	fb.colorBuf = t; // identifies the target (C3D_SyncDisplayTransfer)
	fb.depthBuf = depthFmt >= 0 ? t : nullptr;
	fb.width = u16(width); fb.height = u16(height);
	fb.colorFmt = colorFmt;
	fb.depthFmt = depthFmt >= 0 ? GPU_DEPTHBUF(depthFmt) : GPU_RB_DEPTH24;
	fb.colorMask = 0xF; fb.depthMask = depthFmt >= 0 ? 0x3 : 0;
	t->c3d.ownsColor = true; t->c3d.ownsDepth = depthFmt >= 0;
	targets.push_back(t);
	state.targetDirty = true;
	invalidateApplied();
	return &t->c3d;
}
extern "C" C3D_RenderTarget* c3dvita_RenderTargetCreateFromTex(C3D_Tex* tex, GPU_TEXFACE, int, int depthFmt) {
	LightLock_Lock(&textureLock);
	GpuTexture* record = tex && tex->data ? findTexture(tex->data) : nullptr;
	LightLock_Unlock(&textureLock);
	if (!record) return nullptr;
	auto* t = new Target{};
	t->glWidth = scaled(tex->width); t->glHeight = scaled(tex->height);
	// The texture is sampled through its record: give it the target's storage.
	if (!record->gl) glGenTextures(1, &record->gl);
	glBindTexture(GL_TEXTURE_2D, record->gl);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->glWidth, t->glHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	record->renderTarget = true; record->dirty = false; record->appliedParam = ~0u;
	invalidateApplied();
	t->ownsColor = false;
	createTargetStorage(*t, record->gl, depthFmt);
	C3D_FrameBuf& fb = t->c3d.frameBuf;
	fb.colorBuf = t; fb.depthBuf = depthFmt >= 0 ? t : nullptr;
	fb.width = tex->width; fb.height = tex->height;
	fb.colorFmt = GPU_RB_RGBA8; fb.depthFmt = depthFmt >= 0 ? GPU_DEPTHBUF(depthFmt) : GPU_RB_DEPTH24;
	fb.colorMask = 0xF; fb.depthMask = depthFmt >= 0 ? 0x3 : 0;
	targets.push_back(t);
	state.targetDirty = true;
	invalidateApplied();
	return &t->c3d;
}
void C3D_RenderTargetDelete(C3D_RenderTarget* target) {
	if (!target) return;
	Target* t = targetOf(target);
	if (topOutput == t) topOutput = nullptr;
	if (state.target == t) { state.target = nullptr; state.targetDirty = true; }
	targets.erase(std::remove(targets.begin(), targets.end(), t), targets.end());
	LightLock_Lock(&textureLock);
	deadFramebuffers.push_back(t->fbo);
	if (t->depth) deadRenderbuffers.push_back(t->depth);
	if (t->ownsColor) deadTextures.push_back(t->color);
	LightLock_Unlock(&textureLock);
	delete t;
}
void C3D_RenderTargetSetOutput(C3D_RenderTarget* target, gfxScreen_t screen, gfx3dSide_t side, u32) {
	// Only the upper screen's left eye reaches the display.
	if (screen != GFX_TOP || side != GFX_LEFT) return;
	topOutput = target ? targetOf(target) : nullptr;
}
void C3D_FrameBufClear(C3D_FrameBuf* fb, C3D_ClearBits clearBits, u32 clearColor, u32 clearDepth) {
	// Recorded, not run: see flushClear. A later clear of the same target
	// replaces the values it sets.
	Target* t = targetOfFrameBuf(fb);
	if (clearBits & C3D_CLEAR_COLOR) {
		// RGBA8 targets clear to 0xRRGGBBAA.
		t->clearColor[0] = float(clearColor >> 24) / 255.f; t->clearColor[1] = float((clearColor >> 16) & 255) / 255.f;
		t->clearColor[2] = float((clearColor >> 8) & 255) / 255.f; t->clearColor[3] = float(clearColor & 255) / 255.f;
		t->pendingClear |= GL_COLOR_BUFFER_BIT;
	}
	if ((clearBits & C3D_CLEAR_DEPTH) && t->depth) {
		// GL window depth is 0.5 + 0.5 * (1 - PICA depth): a PICA clear to 0 (the
		// far value) is the usual GL clear to 1.
		t->clearDepth = 1.f - 0.5f * float(clearDepth & 0xFFFFFF) / float(0xFFFFFF);
		t->clearStencil = int(clearDepth >> 24);
		t->pendingClear |= GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
	}
}
void C3D_FrameBufTransfer(C3D_FrameBuf*, gfxScreen_t, gfx3dSide_t, u32) {}

// ---- Frames ---------------------------------------------------------------------------------
float C3D_FrameRate(float fps) { return fps; }
void C3D_FrameSync(void) { glFinish(); }
u32 C3D_FrameCounter(int) { return u32(sceDisplayGetVcount()); }
bool C3D_FrameBegin(u8 flags) {
	// citro3d waits here for the previous frame's GPU work; rewritten LINEAR
	// data is copied at the draw instead (noteWritten), so only an explicit
	// sync request (or the gpu-sync.txt marker) waits.
	if (gpuSync || (flags & C3D_FRAME_SYNCDRAW)) {
		const uint64_t start = sceKernelGetProcessTimeWide();
		glFinish();
		stats.beginWaitUs += sceKernelGetProcessTimeWide() - start;
	}
	collectGarbage();
	stats.frameStartUs = sceKernelGetProcessTimeWide();
	for (Target* t : targets) t->c3d.used = false;
	return true;
}
bool C3D_FrameDrawOn(C3D_RenderTarget* target) {
	if (!target) return false;
	Target* t = targetOf(target);
	t->c3d.used = true;
	state.target = t;
	state.targetDirty = true;
	C3D_SetViewport(0, 0, target->frameBuf.width, target->frameBuf.height);
	return true;
}
void C3D_FrameSplit(u8) {}
void C3D_FrameEnd(u8) {
	if (topOutput && topOutput->c3d.used) {
		if (stats.frameStartUs) stats.workUs += sceKernelGetProcessTimeWide() - stats.frameStartUs;
		present(topOutput);
		// Frame rate, draws and copies, every 300 presented frames.
		uint64_t now = sceKernelGetProcessTimeWide();
		if (profile && stats.frames == 299) {
			// Once per report: wait for the frame just presented. A long wait
			// means the GPU, not the draw thread, sets the frame rate.
			glFinish();
			const uint64_t finished = sceKernelGetProcessTimeWide();
			stats.gpuTailUs = finished - now;
			now = finished;
		}
		if (stats.lastPresentUs) {
			stats.frameUs += now - stats.lastPresentUs;
			const uint64_t frame = now - stats.lastPresentUs;
			if (frame > 70000)
				std::printf("c3dvita: slow frame %.1f ms: %u texture uploads (%u converted while drawing, %.1f ms), %u shader programs (%.1f ms)\n",
					double(frame) / 1e3, uploadStats.uploads, uploadStats.converted, double(uploadStats.us) / 1e3,
					c3dvita::programsCreated, double(c3dvita::programUs) / 1e3);
		}
		uploadStats = {};
		c3dvita::programsCreated = 0;
		c3dvita::programUs = 0;
		stats.lastPresentUs = now;
		if (++stats.frames == 300) {
			std::printf("c3dvita: %.1f fps, %u draws/frame, %u copies/frame (%u KiB), sync wait %.2f ms, draw thread %.2f ms, present %.2f ms\n",
				stats.frameUs ? 299e6 / double(stats.frameUs) : 0.0, stats.draws / 300, stats.copies / 300,
				unsigned(stats.copiedBytes / 300 / 1024), double(stats.beginWaitUs) / 300e3, double(stats.workUs) / 300e3,
				double(stats.swapUs) / 300e3);
			std::printf("c3dvita:   in draw calls %.2f ms (game work %.2f ms), %u target switches/frame\n",
				double(stats.glUs) / 300e3, double(stats.workUs > stats.glUs ? stats.workUs - stats.glUs : 0) / 300e3,
				stats.targetSwitches / 300);
			if (profile)
				std::printf("c3dvita:   gpu tail %.2f ms; per frame: uniforms %.2f, textures %.2f, state %.2f, attributes %.2f, vitaGL draw %.2f ms\n",
					double(stats.gpuTailUs) / 1e3, double(stats.uniformUs) / 300e3, double(stats.textureUs) / 300e3,
					double(stats.stateUs) / 300e3, double(stats.attributeUs) / 300e3, double(stats.drawUs) / 300e3);
			const uint64_t keep = stats.lastPresentUs;
			stats = {};
			stats.lastPresentUs = keep;
		}
	}
	endFrameDraws();
	++frameNumber;
	pruneWritten();
}
void C3D_FrameEndHook(void (*)(void*), void*) {}
float C3D_GetDrawingTime(void) { return 0.f; }
float C3D_GetProcessingTime(void) { return 0.f; }

// Readback of a target into RGBA8 rows from the portrait top (QA captures).
void C3D_SyncDisplayTransfer(u32* inadr, u32, u32* outadr, u32, u32) {
	Target* t = nullptr;
	for (Target* candidate : targets) if (candidate->c3d.frameBuf.colorBuf == inadr) t = candidate;
	if (!t) return;
	flushClear(t);
	const int w = t->c3d.frameBuf.width, h = t->c3d.frameBuf.height;
	std::vector<uint8_t> pixels(size_t(t->glWidth) * t->glHeight * 4);
	glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
	boundFramebuffer = ~0u;
	glReadPixels(0, 0, t->glWidth, t->glHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	state.targetDirty = true;
	invalidateApplied();
	auto* out = reinterpret_cast<uint8_t*>(outadr);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const uint8_t* p = pixels.data() + (size_t(y * Scale) * t->glWidth + size_t(x * Scale)) * 4; // nearest sample
			uint8_t* o = out + (size_t(h - 1 - y) * w + x) * 4;
			o[0] = p[3]; o[1] = p[2]; o[2] = p[1]; o[3] = p[0];
		}
}
void C3D_SyncTextureCopy(u32* inadr, u32, u32* outadr, u32, u32 size, u32) {
	std::memcpy(outadr, inadr, size);
	LightLock_Lock(&textureLock);
	if (GpuTexture* t = textureContaining(outadr)) t->dirty = true;
	LightLock_Unlock(&textureLock);
}
void C3D_SyncMemoryFill(u32*, u32, u32*, u16, u32*, u32, u32*, u16) {}
