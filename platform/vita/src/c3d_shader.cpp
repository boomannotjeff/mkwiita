// citro3d shaders on the Vita: GLSL ports of the game's PICA vertex shaders,
// libctru's shader binary API over them, and fragment programs generated
// from the PICA texture combiner (TEV) state, following Azahar's
// glsl_fs_shader_gen.cpp semantics.
#include "c3d_internal.hpp"
#include <psp2/kernel/processmgr.h>
#include <cstdio>
#include <sys/stat.h>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace c3dvita {
namespace {

// ---- Uniform registers ------------------------------------------------------
// Each vertex program reads the PICA float uniform file as `c` (C3D_FVec is
// stored w,z,y,x, hence U(i) = c[i].wzyx). Registers follow src/mkw.v.pica's
// declaration order; boolean uniforms are 0x68 + their bit.
struct Uniform { const char* name; int reg; };
constexpr Uniform SceneUniforms[] = {
	{"projection", 0}, {"modelView", 4}, {"light", 8}, {"uvScroll", 9},
	{"envRow0", 10}, {"envRow1", 11}, {"envRow2", 12}, {"envParam", 13},
	{"uvRow0", 14}, {"uvRow1", 15}, {"uvRow2", 16}, {"uvRow3", 17}, {"tint", 18},
	{"uv1Row0", 19}, {"uv1Row1", 20}, {"lightDiff", 21}, {"lightDir", 22}, {"lightFn", 23},
	{"fogParam", 24}, {"bones", 25},
	{"retailLightTexture", 0x68 + 0}, {"environmentMapped", 0x68 + 1}, {"diffuseLighting", 0x68 + 2},
	{"skinned", 0x68 + 3}, {"skinnedNormals", 0x68 + 4},
};
constexpr int SceneRegisters = 25 + 57;
constexpr Uniform EffectUniforms[] = {{"projection", 0}};
constexpr int EffectRegisters = 4;

// PICA clip z is in [-w, 0] and its depth is -z/w (1 near, 0 far; the game
// tests GEQUAL and clears to 0). Here z' = w + z lies in [0, w], so GL depth
// is 1 - PICA depth: compares are mirrored and clears become 1 - value
// (c3d_gpu.cpp). Clearing to the far value 1 is what vitaGL handles; its
// clear quad for 0 sits on the clip boundary and left old depth behind.
const char* SceneVertex = R"(
attribute vec4 v0;
attribute vec4 v1;
attribute vec4 v2;
attribute vec4 v3;
attribute vec4 v4;
uniform vec4 c[25];
uniform vec4 bones[57];
uniform vec4 flags;
uniform float flags2;
varying vec4 vColor;
varying vec4 vTc0;
varying vec2 vTc1;
varying vec2 vTc2;
vec4 U(int i) { return c[i].wzyx; }
vec4 Bone(int i) { return bones[i].wzyx; }
void main() {
	vec3 n = v1.xyz;
	vec4 p = vec4(v0.xyz, 1.0);
	if (flags.w > 0.5) {
		int a = int(floor((flags2 > 0.5 ? v4.x : v1.x) + 0.5));
		vec4 B0 = Bone(a);
		vec4 B1 = Bone(a + 1);
		vec4 B2 = Bone(a + 2);
		vec3 skinnedPos = vec3(dot(B0, p), dot(B1, p), dot(B2, p));
		if (flags2 > 0.5) {
			vec3 k0 = B1.yzx * B2.zxy - B1.zxy * B2.yzx;
			vec3 k1 = B2.yzx * B0.zxy - B2.zxy * B0.yzx;
			vec3 k2 = B0.yzx * B1.zxy - B0.zxy * B1.yzx;
			float det = dot(B0.xyz, k0);
			if (det == 0.0) {
				n = vec3(dot(B0.xyz, v1.xyz), dot(B1.xyz, v1.xyz), dot(B2.xyz, v1.xyz));
			} else {
				float s = det >= 0.0 ? 1.0 : -1.0;
				n = vec3(dot(k0, v1.xyz), dot(k1, v1.xyz), dot(k2, v1.xyz)) * s;
			}
			n = n * inversesqrt(max(0.000000000001, dot(n, n)));
		}
		p = vec4(skinnedPos, 1.0);
	}
	vec4 mv = vec4(dot(U(4), p), dot(U(5), p), dot(U(6), p), dot(U(7), p));
	vec4 pos = vec4(dot(U(0), mv), dot(U(1), mv), dot(U(2), mv), dot(U(3), mv));
	gl_Position = vec4(pos.x, pos.y, pos.w + pos.z, pos.w);
	vec3 r11 = vec3(v2.xy, 1.0);
	vec4 t0 = vec4(dot(U(14).xyz, r11), dot(U(15).xyz, r11), 0.0, 1.0);
	t0.xy += U(9).xy;
	vTc0 = t0;
	r11 = vec3(v2.zw, 1.0);
	vec4 t1 = vec4(dot(U(16).xyz, r11), dot(U(17).xyz, r11), 0.0, 1.0);
	t1.xy += U(9).zw;
	if (flags.y > 0.5) {
		vec3 m = vec3(dot(U(10).xyz, n), dot(U(11).xyz, n), dot(U(12).xyz, n));
		m = m * inversesqrt(max(0.00392156863, dot(m, m)));
		vec4 e = U(13);
		vec2 r8 = vec2(e.x * m.x + e.y * m.z, m.y);
		r8 = vec2(0.5) + vec2(0.5) * r8;
		r8 = (r8 - t1.xy) * e.z;
		t1.xy += r8;
	}
	vec2 uv1 = vec2(dot(U(19), t1), dot(U(20), t1));
	vTc1 = uv1;
	vec4 light = U(8);
	vec4 col;
	if (flags.z > 0.5) {
		vec4 nn = vec4(n, 0.0);
		vec3 r1 = vec3(dot(U(4), nn), dot(U(5), nn), dot(U(6), nn));
		r1 = r1 * inversesqrt(max(0.00392156863, dot(r1, r1)));
		vec4 fn = U(23);
		float d = max(fn.z, fn.x * dot(U(22).xyz, r1) + fn.y);
		col = light + U(21) * d;
	} else {
		col = light;
	}
	col = min(vec4(U(22).w), col);
	vec4 r4 = 0.00392156863 * v3;
	r4 = r4 * col * U(18);
	vec4 fog = U(24);
	vTc2 = flags.x > 0.5 ? uv1 : vec2(fog.x * mv.z + fog.y, 0.5);
	vColor = clamp(r4, 0.0, 1.0);
}
)";

const char* EffectVertex = R"(
attribute vec4 v0;
attribute vec4 v1;
attribute vec4 v2;
attribute vec4 v3;
uniform vec4 c[4];
varying vec4 vColor;
varying vec4 vTc0;
varying vec2 vTc1;
varying vec2 vTc2;
void main() {
	vec4 p = vec4(v0.xyz, 1.0);
	vec4 pos = vec4(dot(c[0].wzyx, p), dot(c[1].wzyx, p), dot(c[2].wzyx, p), dot(c[3].wzyx, p));
	gl_Position = vec4(pos.x, pos.y, pos.w + pos.z, pos.w);
	vTc0 = vec4(v1.xy, 0.0, v1.z);
	vTc1 = v2.xy;
	vTc2 = v2.zw;
	vColor = clamp(0.00392156863 * v3, 0.0, 1.0);
}
)";

const char* PresentVertex = R"(
attribute vec2 position;
attribute vec2 texcoord;
varying vec2 uv;
void main() { gl_Position = vec4(position, 0.0, 1.0); uv = texcoord; }
)";
const char* PresentFragment = R"(
uniform sampler2D image;
varying vec2 uv;
void main() { gl_FragColor = vec4(texture2D(image, uv).rgb, 1.0); }
)";

void logShader(GLuint shader, const char* what) {
	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (ok) return;
	char log[1024] = {};
	glGetShaderInfoLog(shader, sizeof log, nullptr, log);
	std::printf("c3dvita: %s shader failed: %s\n", what, log);
}

GLuint compile(GLenum type, const std::string& source, const char* what) {
	GLuint shader = glCreateShader(type);
	const char* text = source.c_str();
	const GLint length = GLint(source.size());
	glShaderSource(shader, 1, &text, &length);
	glCompileShader(shader);
	logShader(shader, what);
	return shader;
}

// ---- Fragment generator ------------------------------------------------------
std::string source(const FragmentKey& k, int stage, int which, bool alpha) {
	const uint16_t sources = alpha ? k.srcAlpha[stage] : k.srcRgb[stage];
	int s = (sources >> (4 * which)) & 0xF;
	// Stage 0 has no previous output: the hardware reads its third source instead.
	if (stage == 0 && s == GPU_PREVIOUS) s = (sources >> 8) & 0xF;
	switch (s) {
	case GPU_PRIMARY_COLOR: return "pc";
	case GPU_TEXTURE0: return "t0";
	case GPU_TEXTURE1: return "t1";
	case GPU_TEXTURE2: return "t2";
	case GPU_PREVIOUS_BUFFER: return "cbuf";
	case GPU_CONSTANT: return "konst[" + std::to_string(stage) + "]";
	case GPU_PREVIOUS: return "co";
	default: return "vec4(0.0)"; // fragment lighting and unit 3 are unused
	}
}

std::string colorOperand(const FragmentKey& k, int stage, int which) {
	const std::string s = source(k, stage, which, false);
	switch ((k.opAll[stage] >> (4 * which)) & 0xF) {
	case 0x0: return s + ".rgb";
	case 0x1: return "(vec3(1.0) - " + s + ".rgb)";
	case 0x2: return s + ".aaa";
	case 0x3: return "(vec3(1.0) - " + s + ".aaa)";
	case 0x4: return s + ".rrr";
	case 0x5: return "(vec3(1.0) - " + s + ".rrr)";
	case 0x8: return s + ".ggg";
	case 0x9: return "(vec3(1.0) - " + s + ".ggg)";
	case 0xC: return s + ".bbb";
	case 0xD: return "(vec3(1.0) - " + s + ".bbb)";
	default: return "vec3(0.0)";
	}
}

std::string alphaOperand(const FragmentKey& k, int stage, int which) {
	const std::string s = source(k, stage, which, true);
	switch ((k.opAll[stage] >> (12 + 4 * which)) & 0x7) {
	case 0: return s + ".a";
	case 1: return "(1.0 - " + s + ".a)";
	case 2: return s + ".r";
	case 3: return "(1.0 - " + s + ".r)";
	case 4: return s + ".g";
	case 5: return "(1.0 - " + s + ".g)";
	case 6: return s + ".b";
	default: return "(1.0 - " + s + ".b)";
	}
}

std::string combine(int func, const std::string& a, const std::string& b, const std::string& c, bool vector) {
	const std::string one = vector ? "vec3(1.0)" : "1.0", halfOne = vector ? "vec3(0.5)" : "0.5";
	switch (func) {
	case GPU_REPLACE: return a;
	case GPU_MODULATE: return a + " * " + b;
	case GPU_ADD: return a + " + " + b;
	case GPU_ADD_SIGNED: return a + " + " + b + " - " + halfOne;
	case GPU_INTERPOLATE: return "mix(" + b + ", " + a + ", " + c + ")";
	case GPU_SUBTRACT: return a + " - " + b;
	case GPU_DOT3_RGB:
	case GPU_DOT3_RGBA:
		return vector ? "vec3(dot(" + a + " - vec3(0.5), " + b + " - vec3(0.5)) * 4.0)" : a;
	case GPU_MULTIPLY_ADD: return a + " * " + b + " + " + c;
	case GPU_ADD_MULTIPLY: return "min(" + a + " + " + b + ", " + one + ") * " + c;
	default: return vector ? "vec3(0.0)" : "0.0";
	}
}

std::string fragmentSource(const FragmentKey& k) {
	std::string s;
	s += "uniform sampler2D tex0;\nuniform sampler2D tex1;\nuniform sampler2D tex2;\n";
	s += "uniform vec4 konst[6];\nuniform vec4 bufColor;\nuniform float alphaRef;\n";
	s += "varying vec4 vColor;\nvarying vec4 vTc0;\nvarying vec2 vTc1;\nvarying vec2 vTc2;\n";
	s += "vec3 round3(vec3 x) { return floor(x * 255.0 + vec3(0.5)) / 255.0; }\n";
	s += "float round1(float x) { return floor(x * 255.0 + 0.5) / 255.0; }\n";
	s += "void main() {\n";
	s += "vec4 pc = vec4(round3(vColor.rgb), round1(vColor.a));\n";
	if (k.units & 1) s += k.projection0 ? "vec4 t0 = texture2D(tex0, vTc0.xy / vTc0.w);\n" : "vec4 t0 = texture2D(tex0, vTc0.xy);\n";
	else s += "vec4 t0 = vec4(0.0);\n";
	s += (k.units & 2) ? "vec4 t1 = texture2D(tex1, vTc1);\n" : "vec4 t1 = vec4(0.0);\n";
	s += (k.units & 4) ? "vec4 t2 = texture2D(tex2, vTc2);\n" : "vec4 t2 = vec4(0.0);\n";
	s += "vec4 cbuf = vec4(0.0);\nvec4 nextBuf = bufColor;\nvec4 co = vec4(0.0);\n";
	for (int i = 0; i < 6; ++i) {
		const bool passThrough = k.funcRgb[i] == GPU_REPLACE && k.funcAlpha[i] == GPU_REPLACE &&
			(k.srcRgb[i] & 0xF) == GPU_PREVIOUS && (k.srcAlpha[i] & 0xF) == GPU_PREVIOUS &&
			(k.opAll[i] & 0xF) == 0 && ((k.opAll[i] >> 12) & 0x7) == 0 && !k.scaleRgb[i] && !k.scaleAlpha[i] && i;
		if (!passThrough) {
			const int scaleRgb = 1 << k.scaleRgb[i], scaleAlpha = 1 << k.scaleAlpha[i];
			s += "{\n";
			s += "vec3 cr = round3(clamp(" + combine(k.funcRgb[i], colorOperand(k, i, 0), colorOperand(k, i, 1), colorOperand(k, i, 2), true) + ", 0.0, 1.0));\n";
			if (k.funcRgb[i] == GPU_DOT3_RGBA) s += "float ar = cr.r;\n";
			else s += "float ar = round1(clamp(" + combine(k.funcAlpha[i], alphaOperand(k, i, 0), alphaOperand(k, i, 1), alphaOperand(k, i, 2), false) + ", 0.0, 1.0));\n";
			s += "co = vec4(clamp(cr * " + std::to_string(scaleRgb) + ".0, 0.0, 1.0), clamp(ar * " + std::to_string(scaleAlpha) + ".0, 0.0, 1.0));\n";
			s += "}\n";
		}
		s += "cbuf = nextBuf;\n";
		if (i < 4 && (k.bufferUpdate >> (8 + i) & 1)) s += "nextBuf.rgb = co.rgb;\n";
		if (i < 4 && (k.bufferUpdate >> (12 + i) & 1)) s += "nextBuf.a = co.a;\n";
	}
	if (k.alphaFunc != 0xFF && k.alphaFunc != GPU_ALWAYS) {
		s += "float a8 = floor(co.a * 255.0 + 0.5);\n";
		switch (k.alphaFunc) {
		case GPU_NEVER: s += "discard;\n"; break;
		case GPU_EQUAL: s += "if (a8 != alphaRef) discard;\n"; break;
		case GPU_NOTEQUAL: s += "if (a8 == alphaRef) discard;\n"; break;
		case GPU_LESS: s += "if (a8 >= alphaRef) discard;\n"; break;
		case GPU_LEQUAL: s += "if (a8 > alphaRef) discard;\n"; break;
		case GPU_GREATER: s += "if (a8 <= alphaRef) discard;\n"; break;
		case GPU_GEQUAL: s += "if (a8 < alphaRef) discard;\n"; break;
		}
	}
	s += "gl_FragColor = co;\n}\n";
	return s;
}

GLuint vertexShaders[ProgramCount] = {};
GLuint vertexShader(uint8_t program) {
	if (!vertexShaders[program])
		vertexShaders[program] = compile(GL_VERTEX_SHADER, program == ProgramScene ? SceneVertex : EffectVertex,
			program == ProgramScene ? "scene vertex" : "effect vertex");
	return vertexShaders[program];
}

struct KeyLess {
	bool operator()(const FragmentKey& a, const FragmentKey& b) const { return std::memcmp(&a, &b, sizeof a) < 0; }
};
std::map<FragmentKey, Program*, KeyLess> programs;

} // namespace

unsigned programsCreated = 0;
uint64_t programUs = 0;

namespace {
// Compiled programs persist on the memory card: ux0:data/mkw/shaders/<key>.bin,
// and keys.bin lists every key so the next launch loads them all up front.
// The name mixes in the shader sources, so a changed shader recompiles.
constexpr const char* CacheDir = MKW_DATA_ROOT "/shaders";
uint64_t fnv(const void* data, size_t size, uint64_t h = 1469598103934665603ull) {
	auto* b = static_cast<const uint8_t*>(data);
	for (size_t i = 0; i < size; ++i) h = (h ^ b[i]) * 1099511628211ull;
	return h;
}
uint64_t sourceSalt() {
	static uint64_t salt = 0;
	if (!salt) {
		salt = fnv(SceneVertex, std::strlen(SceneVertex));
		salt = fnv(EffectVertex, std::strlen(EffectVertex), salt);
		FragmentKey probe;
		std::memset(&probe, 0, sizeof probe);
		const std::string f = fragmentSource(probe);
		salt = fnv(f.data(), f.size(), salt);
	}
	return salt;
}
std::string cachePath(const FragmentKey& key) {
	char name[96];
	std::snprintf(name, sizeof name, "%s/%016llx.bin", CacheDir, (unsigned long long)fnv(&key, sizeof key, sourceSalt()));
	return name;
}
// ux0:data/mkw/no-shader-cache.txt turns the cache off (always compile).
bool cacheEnabled() {
	static int enabled = -1;
	if (enabled < 0) { FILE* f = std::fopen(MKW_DATA_ROOT "/no-shader-cache.txt", "rb"); enabled = f ? 0 : 1; if (f) std::fclose(f); }
	return enabled == 1;
}
bool loadBinary(GLuint program, const FragmentKey& key) {
	if (!cacheEnabled()) return false;
	FILE* f = std::fopen(cachePath(key).c_str(), "rb");
	if (!f) return false;
	std::vector<uint8_t> data;
	std::fseek(f, 0, SEEK_END);
	const long size = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	if (size > 0) { data.resize(size_t(size)); if (std::fread(data.data(), 1, data.size(), f) != data.size()) data.clear(); }
	std::fclose(f);
	if (data.empty()) return false;
	glProgramBinary(program, 0, data.data(), GLsizei(data.size()));
	return true;
}
void saveBinary(GLuint program, const FragmentKey& key) {
	if (!cacheEnabled()) return;
	GLint length = 0;
	glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &length);
	if (length <= 0) return;
	std::vector<uint8_t> data(size_t(length) + 64);
	GLsizei written = 0;
	GLenum format = 0;
	glGetProgramBinary(program, GLsizei(data.size()), &written, &format, data.data());
	if (written <= 0) return;
	mkdir(CacheDir, 0777);
	if (FILE* f = std::fopen(cachePath(key).c_str(), "wb")) { std::fwrite(data.data(), 1, size_t(written), f); std::fclose(f); }
	char keys[96];
	std::snprintf(keys, sizeof keys, "%s/keys-%016llx.bin", CacheDir, (unsigned long long)sourceSalt());
	if (FILE* f = std::fopen(keys, "ab")) { std::fwrite(&key, sizeof key, 1, f); std::fclose(f); }
}
}

Program* programFor(const FragmentKey& key) {
	auto it = programs.find(key);
	if (it != programs.end()) return it->second;
	const uint64_t start = sceKernelGetProcessTimeWide();
	auto* p = new Program;
	p->gl = glCreateProgram();
	if (!loadBinary(p->gl, key)) {
		const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragmentSource(key), "combiner fragment");
		glAttachShader(p->gl, vertexShader(key.vertexProgram));
		glAttachShader(p->gl, fragment);
		for (int i = 0; i < 5; ++i) {
			char name[4] = {'v', char('0' + i), 0, 0};
			glBindAttribLocation(p->gl, i, name);
		}
		glLinkProgram(p->gl);
		saveBinary(p->gl, key);
		++programsCreated;
		programUs += sceKernelGetProcessTimeWide() - start;
	}
	glUseProgram(p->gl);
	p->vsConst = glGetUniformLocation(p->gl, "c");
	p->vsBones = glGetUniformLocation(p->gl, "bones");
	p->vsFlags = glGetUniformLocation(p->gl, "flags");
	p->vsFlags2 = glGetUniformLocation(p->gl, "flags2");
	p->konst = glGetUniformLocation(p->gl, "konst");
	p->bufColor = glGetUniformLocation(p->gl, "bufColor");
	p->alphaRef = glGetUniformLocation(p->gl, "alphaRef");
	for (int unit = 0; unit < 3; ++unit) {
		char name[5] = {'t', 'e', 'x', char('0' + unit), 0};
		const GLint at = glGetUniformLocation(p->gl, name);
		if (at >= 0) glUniform1i(at, unit);
	}
	programs.emplace(key, p);
	return p;
}

// Every program a previous launch used, loaded from its binary at start-up.
void preloadPrograms() {
	char keys[96];
	std::snprintf(keys, sizeof keys, "%s/keys-%016llx.bin", CacheDir, (unsigned long long)sourceSalt());
	FILE* f = std::fopen(keys, "rb");
	if (!f) return;
	const uint64_t start = sceKernelGetProcessTimeWide();
	unsigned loaded = 0;
	FragmentKey key;
	while (std::fread(&key, sizeof key, 1, f) == 1)
		if (!programs.count(key)) { programFor(key); ++loaded; }
	std::fclose(f);
	std::printf("c3dvita: preloaded %u shader programs in %.0f ms\n", loaded, double(sceKernelGetProcessTimeWide() - start) / 1e3);
}

GLuint presentProgram() {
	static GLuint program = 0;
	if (program) return program;
	program = glCreateProgram();
	glAttachShader(program, compile(GL_VERTEX_SHADER, PresentVertex, "present vertex"));
	glAttachShader(program, compile(GL_FRAGMENT_SHADER, PresentFragment, "present fragment"));
	glBindAttribLocation(program, 0, "position");
	glBindAttribLocation(program, 1, "texcoord");
	glLinkProgram(program);
	glUseProgram(program);
	glUniform1i(glGetUniformLocation(program, "image"), 0);
	return program;
}

// The fake DVLB has one DVLE per vertex program; a DVLE's index is its
// position in that array.
static DVLB_s* parsed = nullptr;
int dvleIndex(const DVLE_s* dvle) {
	if (!parsed || !dvle) return ProgramScene;
	const int index = int(dvle - parsed->DVLE);
	return index >= 0 && index < int(parsed->numDVLE) ? index : ProgramScene;
}

} // namespace c3dvita

using namespace c3dvita;

// ---- libctru shader binary API ---------------------------------------------------
// The game embeds a PICA binary (build/shader_data.hpp); here it is ignored and
// DVLE 0/1 name the GLSL scene and effect programs above.
DVLB_s* DVLB_ParseFile(u32*, u32) {
	auto* dvlb = static_cast<DVLB_s*>(std::calloc(1, sizeof(DVLB_s)));
	dvlb->numDVLE = ProgramCount;
	dvlb->DVLE = static_cast<DVLE_s*>(std::calloc(ProgramCount, sizeof(DVLE_s)));
	parsed = dvlb;
	return dvlb;
}
void DVLB_Free(DVLB_s* dvlb) {
	if (!dvlb) return;
	if (parsed == dvlb) parsed = nullptr;
	std::free(dvlb->DVLE);
	std::free(dvlb);
}
s8 DVLE_GetUniformRegister(DVLE_s* dvle, const char* name) {
	const int program = dvleIndex(dvle);
	const Uniform* table = program == ProgramScene ? SceneUniforms : EffectUniforms;
	const size_t count = program == ProgramScene ? std::size(SceneUniforms) : std::size(EffectUniforms);
	for (size_t i = 0; i < count; ++i)
		if (!std::strcmp(table[i].name, name)) return s8(table[i].reg);
	return -1;
}
Result shaderInstanceInit(shaderInstance_s* si, DVLE_s* dvle) {
	std::memset(si, 0, sizeof *si);
	si->dvle = dvle;
	return 0;
}
Result shaderInstanceFree(shaderInstance_s* si) { std::free(si); return 0; }
s8 shaderInstanceGetUniformLocation(shaderInstance_s* si, const char* name) {
	if (!si) return -1;
	return DVLE_GetUniformRegister(si->dvle, name);
}
Result shaderProgramInit(shaderProgram_s* sp) { std::memset(sp, 0, sizeof *sp); return 0; }
Result shaderProgramFree(shaderProgram_s* sp) {
	if (sp->vertexShader) shaderInstanceFree(sp->vertexShader);
	std::memset(sp, 0, sizeof *sp);
	return 0;
}
Result shaderProgramSetVsh(shaderProgram_s* sp, DVLE_s* dvle) {
	if (sp->vertexShader) shaderInstanceFree(sp->vertexShader);
	sp->vertexShader = static_cast<shaderInstance_s*>(std::calloc(1, sizeof(shaderInstance_s)));
	return shaderInstanceInit(sp->vertexShader, dvle);
}

// Register counts each program reads, for uniform uploads (c3d_gpu.cpp).
namespace c3dvita {
int programRegisters(uint8_t program) { return program == ProgramScene ? 25 : EffectRegisters; }
}
