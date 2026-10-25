// Shared between the citro3d-on-vitaGL files (c3d_gpu.cpp, c3d_shader.cpp).
#pragma once
#include <citro3d.h>
#include <vitaGL.h>
#include <cstdint>

namespace c3dvita {

// Vertex programs: the GLSL ports of src/mkw.v.pica (DVLE 0) and
// src/effect.v.pica (DVLE 1).
enum : uint8_t { ProgramScene = 0, ProgramEffect = 1, ProgramCount = 2 };

// Everything that selects a generated fragment program. Zero-filled before
// use so it can be compared and hashed as bytes.
struct FragmentKey {
	uint16_t srcRgb[6], srcAlpha[6];
	uint32_t opAll[6];
	uint16_t funcRgb[6], funcAlpha[6];
	uint16_t scaleRgb[6], scaleAlpha[6];
	uint32_t bufferUpdate; // texEnvBuf bits: 8-11 colour, 12-15 alpha (stages 0-3)
	uint8_t alphaFunc;     // GPU_TESTFUNC, or 0xFF with the alpha test off
	uint8_t units;         // bit n: texture unit n has a texture bound
	uint8_t projection0;   // unit 0 samples projectively (GPU_TEX_PROJECTION)
	uint8_t vertexProgram;
	uint8_t constantAlpha;  // CONSTANT_ALPHA blending: the output alpha is blendAlpha
};

struct Program {
	GLuint gl = 0;
	GLint vsConst = -1, vsBones = -1, vsFlags = -1, vsFlags2 = -1, vsProj = -1, vsFlags3 = -1;
	GLint konst = -1, bufColor = -1, alphaRef = -1, blendAlpha = -1;
	uint32_t uploadedConst = ~0u, uploadedBones = ~0u;
	// The fragment constants last uploaded (PICA colours and alpha reference).
	uint32_t konstUploaded[6] = {}, bufUploaded = 0;
	int alphaUploaded = -1;
	bool fragmentUploaded = false;
};

// c3d_shader.cpp
Program* programFor(const FragmentKey& key);
void preloadPrograms();
int dvleIndex(const DVLE_s* dvle);
// The presentation pass: draws a target texture rotated onto the display.
GLuint presentProgram();
// Float uniform registers a vertex program reads (from c0; bones apart).
int programRegisters(uint8_t program);
// Programs created (compiled and linked) since the last reset, and their time.
extern unsigned programsCreated;
extern uint64_t programUs;

} // namespace c3dvita
