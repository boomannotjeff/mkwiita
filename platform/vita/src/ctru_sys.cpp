// libctru system services on the PS Vita: time, threads, synchronization,
// input, APT, display stubs and memory (see platform/vita/include/3ds.h).
#include <3ds.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/error.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/display.h>
#include <psp2/power.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

// ---- Time ------------------------------------------------------------------
extern "C" u64 svcGetSystemTick(void) {
	const u64 us = sceKernelGetProcessTimeWide();
	// us * 268.111856 without overflowing 64 bits for any realistic uptime.
	return us * 268ULL + us * 111856ULL / 1000000ULL;
}
extern "C" void svcSleepThread(s64 ns) {
	if (ns <= 0) { sceKernelDelayThread(0); return; }
	sceKernelDelayThread(SceUInt(ns / 1000 > 0 ? ns / 1000 : 1));
}
extern "C" Result svcGetThreadPriority(s32* out, Handle) { *out = 0x30; return 0; }
extern "C" u64 osGetTime(void) { return sceKernelGetProcessTimeWide() / 1000ULL; }
extern "C" void osSetSpeedupEnable(bool enable) {
	if (!enable) return;
	scePowerSetArmClockFrequency(444);
	scePowerSetBusClockFrequency(222);
	scePowerSetGpuClockFrequency(222);
	scePowerSetGpuXbarClockFrequency(166);
}
extern "C" float osGet3DSliderState(void) { return 0.f; }

// ---- Synchronization -------------------------------------------------------
// Kernel objects are created on first use: state 0 = none, 1 = creating, 2 = ready.
template <class Create>
static int lazyUid(volatile int& state, int& uid, Create create) {
	auto* s = reinterpret_cast<std::atomic<int>*>(const_cast<int*>(&state));
	if (s->load(std::memory_order_acquire) == 2) return uid;
	int expected = 0;
	if (s->compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
		uid = create();
		s->store(2, std::memory_order_release);
		return uid;
	}
	while (s->load(std::memory_order_acquire) != 2) sceKernelDelayThread(10);
	return uid;
}
// LightLock is a lightweight mutex: an uncontended lock or unlock stays in
// user space. (A kernel mutex costs a system call each time; the drawing
// thread takes several locks per draw.) Its work area lives on the heap,
// its address in uid.
static SceKernelLwMutexWork* lockWork(LightLock* lock) {
	return reinterpret_cast<SceKernelLwMutexWork*>(lazyUid(lock->state, lock->uid, [] {
		auto* work = new SceKernelLwMutexWork{};
		sceKernelCreateLwMutex(work, "mkw_lock", 0, 0, nullptr);
		return int(reinterpret_cast<uintptr_t>(work));
	}));
}
extern "C" void LightLock_Init(LightLock* lock) { lock->state = 0; lock->uid = 0; lockWork(lock); }
extern "C" void LightLock_Lock(LightLock* lock) { sceKernelLockLwMutex(lockWork(lock), 1, nullptr); }
extern "C" int LightLock_TryLock(LightLock* lock) { return sceKernelTryLockLwMutex(lockWork(lock), 1) < 0 ? 1 : 0; }
extern "C" void LightLock_Unlock(LightLock* lock) { sceKernelUnlockLwMutex(lockWork(lock), 1); }

static int recursiveUid(RecursiveLock* lock) {
	return lazyUid(lock->state, lock->uid, [] { return sceKernelCreateMutex("mkw_rlock", SCE_KERNEL_MUTEX_ATTR_RECURSIVE, 0, nullptr); });
}
extern "C" void RecursiveLock_Init(RecursiveLock* lock) { lock->state = 0; lock->uid = 0; recursiveUid(lock); }
extern "C" void RecursiveLock_Lock(RecursiveLock* lock) { sceKernelLockMutex(recursiveUid(lock), 1, nullptr); }
extern "C" void RecursiveLock_Unlock(RecursiveLock* lock) { sceKernelUnlockMutex(recursiveUid(lock), 1); }

// An event is a binary semaphore: Signal makes one count available (at most
// one), a one-shot wait consumes it. A sticky event stays signaled until Clear.
static int eventUid(LightEvent* event) {
	return lazyUid(event->state, event->uid, [] { return sceKernelCreateSema("mkw_event", 0, 0, 1, nullptr); });
}
extern "C" void LightEvent_Init(LightEvent* event, ResetType reset) {
	event->state = 0; event->uid = 0; event->reset = reset; eventUid(event);
}
extern "C" void LightEvent_Clear(LightEvent* event) { sceKernelPollSema(eventUid(event), 1); }
extern "C" void LightEvent_Signal(LightEvent* event) { sceKernelSignalSema(eventUid(event), 1); }
extern "C" int LightEvent_TryWait(LightEvent* event) {
	const int uid = eventUid(event);
	if (sceKernelPollSema(uid, 1) < 0) return 0;
	if (event->reset == RESET_STICKY) sceKernelSignalSema(uid, 1);
	return 1;
}
extern "C" void LightEvent_Wait(LightEvent* event) {
	const int uid = eventUid(event);
	sceKernelWaitSema(uid, 1, nullptr);
	if (event->reset == RESET_STICKY) sceKernelSignalSema(uid, 1);
}
extern "C" int LightEvent_WaitTimeout(LightEvent* event, s64 timeout_ns) {
	const int uid = eventUid(event);
	SceUInt timeout = SceUInt(timeout_ns <= 0 ? 0 : timeout_ns / 1000);
	if (sceKernelWaitSema(uid, 1, &timeout) < 0) return 1;
	if (event->reset == RESET_STICKY) sceKernelSignalSema(uid, 1);
	return 0;
}

// ---- Threads ---------------------------------------------------------------
struct Thread_tag {
	SceUID uid = -1;
	ThreadFunc entry = nullptr;
	void* arg = nullptr;
	bool detached = false;
};
static int threadTrampoline(SceSize, void* argp) {
	Thread_tag* t = *static_cast<Thread_tag**>(argp);
	t->entry(t->arg);
	if (t->detached) { delete t; sceKernelExitDeleteThread(0); }
	return 0;
}
// 3DS 0x18 (highest) .. 0x3F (lowest) around the main thread's 0x30 map onto
// Vita user priorities around the default 160 (64 highest .. 191 lowest).
static int vitaPriority(int prio) {
	int p = 160 + (prio - 0x30) * 2;
	return p < 64 ? 64 : p > 191 ? 191 : p;
}
// The game puts its render, job and audio threads on the 3DS's second
// application core. The Vita has three user cores: the main thread keeps core
// 0, audio (the highest priorities) gets core 2, other pinned threads core 1.
// Core id 3 (Vita only, the job worker) also asks for core 2.
static int vitaAffinity(int prio, int core) {
	if (core < 0) return 0;
	if (prio <= 0x19 || core == 3) return SCE_KERNEL_CPU_MASK_USER_2;
	return core == 0 ? SCE_KERNEL_CPU_MASK_USER_0 : SCE_KERNEL_CPU_MASK_USER_1;
}
extern "C" Thread threadCreate(ThreadFunc entrypoint, void* arg, size_t stack_size, int prio, int core_id, bool detached) {
	auto* t = new Thread_tag;
	t->entry = entrypoint; t->arg = arg; t->detached = detached;
	// Vita threads need more stack than the 3DS: newlib's printf family alone uses several KiB.
	const SceSize stack = SceSize(stack_size < 0x10000 ? 0x10000 : (stack_size + 0xFFF) & ~size_t(0xFFF));
	t->uid = sceKernelCreateThread("mkw_thread", threadTrampoline, vitaPriority(prio), stack, 0, vitaAffinity(prio, core_id), nullptr);
	if (t->uid < 0) { delete t; return nullptr; }
	if (sceKernelStartThread(t->uid, sizeof t, &t) < 0) { sceKernelDeleteThread(t->uid); delete t; return nullptr; }
	return detached ? reinterpret_cast<Thread>(1) : t;
}
extern "C" Result threadJoin(Thread thread, u64 timeout_ns) {
	if (!thread || reinterpret_cast<uintptr_t>(thread) == 1) return 0;
	int status = 0;
	if (timeout_ns == U64_MAX) return sceKernelWaitThreadEnd(thread->uid, &status, nullptr) < 0 ? -1 : 0;
	SceUInt timeout = SceUInt(timeout_ns / 1000);
	return sceKernelWaitThreadEnd(thread->uid, &status, &timeout) < 0 ? -1 : 0;
}
extern "C" void threadFree(Thread thread) {
	if (!thread || reinterpret_cast<uintptr_t>(thread) == 1) return;
	sceKernelDeleteThread(thread->uid);
	delete thread;
}
extern "C" void threadExit(int rc) { sceKernelExitThread(rc); }

// ---- APT, CFG ----------------------------------------------------------------
static aptHookCookie* hooks = nullptr;
extern "C" bool aptMainLoop(void) { return true; }
extern "C" void aptHook(aptHookCookie* cookie, aptHookFn callback, void* param) {
	cookie->callback = callback; cookie->param = param; cookie->next = hooks; hooks = cookie;
}
extern "C" void aptUnhook(aptHookCookie* cookie) {
	for (aptHookCookie** p = &hooks; *p; p = &(*p)->next)
		if (*p == cookie) { *p = cookie->next; return; }
}
// The Vita outperforms a New 3DS, so the game takes its New 3DS paths.
extern "C" Result APT_CheckNew3DS(bool* out) { *out = true; return 0; }
extern "C" Result APT_GetAppCpuTimeLimit(u32* percent) { *percent = 100; return 0; }
extern "C" Result APT_SetAppCpuTimeLimit(u32) { return 0; }
// No CFG service: stereo detection fails, so the game runs in 2D.
extern "C" Result cfguInit(void) { return -1; }
extern "C" void cfguExit(void) {}
extern "C" Result CFGU_GetSystemModel(u8* model) { *model = CFG_MODEL_N2DSXL; return -1; }
extern "C" Result CFGU_GetModelNintendo2DS(u8* value) { *value = 0; return -1; }
extern "C" Result CFGU_GetConfigInfoBlk2(u32, u32, void*) { return -1; }

// ---- HID ---------------------------------------------------------------------
// Face buttons follow the Vita's own layout rather than Nintendo's positions:
// Cross accelerates and confirms (3DS A), Circle brakes and backs out (B),
// Triangle is X and Square is Y. The left stick is the Circle Pad.
static u32 held = 0, down = 0, up = 0, repeat = 0;
static u32 repeatDelay = 30, repeatInterval = 6, repeatCount = 0, repeatHeld = 0;
static circlePosition circle{};
static touchPosition touch{};
static bool inputReady = false;
static s16 stickAxis(int raw) {
	int v = raw - 128;
	if (v > -16 && v < 16) return 0; // Vita sticks rest a few counts off centre.
	v = v * 156 / 127;
	return s16(v < -156 ? -156 : v > 156 ? 156 : v);
}
extern "C" void hidScanInput(void) {
	if (!inputReady) {
		sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
		sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
		inputReady = true;
	}
	SceCtrlData pad{};
	sceCtrlPeekBufferPositive(0, &pad, 1);
	u32 keys = 0;
	const u32 b = pad.buttons;
	if (b & SCE_CTRL_CROSS) keys |= KEY_A;
	if (b & SCE_CTRL_CIRCLE) keys |= KEY_B;
	if (b & SCE_CTRL_TRIANGLE) keys |= KEY_X;
	if (b & SCE_CTRL_SQUARE) keys |= KEY_Y;
	if (b & SCE_CTRL_LTRIGGER) keys |= KEY_L;
	if (b & SCE_CTRL_RTRIGGER) keys |= KEY_R;
	if (b & SCE_CTRL_START) keys |= KEY_START;
	if (b & SCE_CTRL_SELECT) keys |= KEY_SELECT;
	if (b & SCE_CTRL_UP) keys |= KEY_DUP;
	if (b & SCE_CTRL_DOWN) keys |= KEY_DDOWN;
	if (b & SCE_CTRL_LEFT) keys |= KEY_DLEFT;
	if (b & SCE_CTRL_RIGHT) keys |= KEY_DRIGHT;
	circle.dx = stickAxis(pad.lx);
	circle.dy = s16(-stickAxis(pad.ly));
	// The 3DS HID module reports the Circle Pad as directions past about a quarter throw.
	// Only the stick's dominant axis counts as a direction: a diagonal would
	// otherwise press two at once (menus moving and paging in the same frame).
	const bool horizontal = (circle.dx < 0 ? -circle.dx : circle.dx) >= (circle.dy < 0 ? -circle.dy : circle.dy);
	if (horizontal && circle.dx >= 40) keys |= KEY_CPAD_RIGHT;
	if (horizontal && circle.dx <= -40) keys |= KEY_CPAD_LEFT;
	if (!horizontal && circle.dy >= 40) keys |= KEY_CPAD_UP;
	if (!horizontal && circle.dy <= -40) keys |= KEY_CPAD_DOWN;
	const int rx = stickAxis(pad.rx), ry = -stickAxis(pad.ry);
	if (rx >= 40) keys |= KEY_CSTICK_RIGHT;
	if (rx <= -40) keys |= KEY_CSTICK_LEFT;
	if (ry >= 40) keys |= KEY_CSTICK_UP;
	if (ry <= -40) keys |= KEY_CSTICK_DOWN;
	SceTouchData t{};
	if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1) > 0 && t.reportNum > 0) {
		keys |= KEY_TOUCH;
		// Front panel 1920x1088 onto the 3DS lower screen's 320x240.
		touch.px = u16(t.report[0].x * 320 / 1920);
		touch.py = u16(t.report[0].y * 240 / 1088);
	}
	down = keys & ~held;
	up = held & ~keys;
	held = keys;
	// libctru repeat: the held set fires again after `delay` scans, then every `interval`.
	repeat = down;
	if (held && held == repeatHeld) {
		if (++repeatCount >= repeatDelay) { repeat = held; repeatCount = repeatDelay - repeatInterval; }
	} else { repeatCount = 0; repeatHeld = held; }
}
extern "C" u32 hidKeysHeld(void) { return held; }
extern "C" u32 hidKeysDown(void) { return down; }
extern "C" u32 hidKeysUp(void) { return up; }
extern "C" u32 hidKeysDownRepeat(void) { return repeat; }
extern "C" void hidSetRepeatParameters(u32 delay, u32 interval) { repeatDelay = delay; repeatInterval = interval ? interval : 1; }
extern "C" void hidCircleRead(circlePosition* pos) { *pos = circle; }
extern "C" void hidTouchRead(touchPosition* pos) { *pos = touch; }

// ---- Display -----------------------------------------------------------------
namespace c3dvita { void initDisplay(); }
extern "C" void gfxInitDefault(void) { c3dvita::initDisplay(); }
extern "C" void gfxExit(void) {}
extern "C" void gfxSet3D(bool) {}
extern "C" u8* gfxGetFramebuffer(gfxScreen_t, gfx3dSide_t, u16* width, u16* height) {
	if (width) *width = 0;
	if (height) *height = 0;
	return nullptr;
}
extern "C" void gfxFlushBuffers(void) {}
extern "C" void gfxSwapBuffers(void) {}
extern "C" void gfxScreenSwapBuffers(gfxScreen_t, bool) {}
extern "C" void gspWaitForVBlank(void) { sceDisplayWaitVblankStart(); }
extern "C" void gspWaitForEvent(GSPGPU_Event, bool) { sceDisplayWaitVblankStart(); }
extern "C" Result GSPGPU_InvalidateDataCache(const void*, u32) { return 0; }
extern "C" Result DSP_FlushDataCache(const void*, u32) { return 0; }

// ---- Console -----------------------------------------------------------------
static PrintConsole console{};
extern "C" PrintConsole* consoleInit(gfxScreen_t, PrintConsole* c) {
	if (!c) c = &console;
	c->consoleInitialised = true;
	return c;
}
extern "C" PrintConsole* consoleGetDefault(void) { return &console; }
extern "C" void consoleClear(void) {}

// ---- Memory --------------------------------------------------------------------
// LINEAR memory is GPU-mapped vitaGL pool memory: see c3d_gpu.cpp.
extern "C" void* vramAlloc(size_t size) { return memalign(0x80, size ? size : 1); }
extern "C" void vramFree(void* mem) { free(mem); }
extern "C" u32 vramSpaceFree(void) { return 6u << 20; }
extern "C" u32 osConvertVirtToPhys(const void* vaddr) { return u32(uintptr_t(vaddr)); }

extern "C" Result romfsInit(void) { return -1; }
extern "C" Result romfsExit(void) { return 0; }
