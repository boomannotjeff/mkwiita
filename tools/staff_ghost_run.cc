// Host tool for tools/cook_staff_ghost_runs.py: replays one disc ghost (RKG)
// through Kinoko, as its replay mode does, and writes what the port's own Time
// Trial ghosts keep (include/mkw/time_trial_ghost.hpp): from the countdown on,
// each tick's inputs and the kart's pose after that tick.
// Run from a directory holding the disc's Race/ files.
// Usage: staff_ghost_run ghost.rkg out.bin
// Output (little endian): u32 frames, race milliseconds, course, character,
// vehicle, automatic drift; the 0x4A-byte Mii data; then frames x
// {u8 buttons, i8 stick, u8 trick, u8 items}, then frames x 11 floats
// (position, full rotation xyzw, main rotation xyzw).
#include "host/KReplaySystem.hh"

#include <abstract/File.hh>
#include <egg/core/ExpHeap.hh>
#include <game/kart/KartObjectManager.hh>
#include <game/system/KPadDirector.hh>
#include <game/system/GhostFile.hh>
#include <game/system/RaceManager.hh>

#include <cstdio>
#include <vector>
#include <immintrin.h>

using namespace Kinoko;

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s ghost.rkg out.bin\n", argv[0]);
        return 2;
    }
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    void *memory = malloc(MEMORY_SPACE_SIZE);
    auto *root = EGG::ExpHeap::create(memory, MEMORY_SPACE_SIZE, DEFAULT_OPT);
    root->setName("EGGRoot");
    root->becomeCurrentHeap();
    EGG::SceneManager::SetRootHeap(root);

    auto *sys = KReplaySystem::CreateInstance();
    char flag[] = "-g";
    char *options[] = {flag, argv[1]};
    sys->parseOptions(2, options);
    sys->init();

    struct Frame {
        u8 buttons;
        s8 stick;
        u8 trick, items;
    };
    std::vector<Frame> frames;
    std::vector<float> poses;
    auto *race = System::RaceManager::Instance();
    constexpr u16 MaxMinutes = 10;
    while (race->stage() != System::RaceManager::Stage::FinishGlobal &&
            race->timerManager().currentTimer().min < MaxMinutes) {
        // The port records from the countdown (main.cpp ghostLive): the inputs a
        // tick is given, then the pose that tick leaves.
        const bool live = unsigned(race->stage()) >= 1;
        sys->calc();
        if (!live) {
            continue;
        }
        const auto &in = System::KPadDirector::Instance()->playerInput(0).currentState();
        const int stickY = int(in.stickYRaw) - 7;
        // ItemInput: 1 use, 2 backwards, 4 forwards (main.cpp: stick past 40/156); high nibble stick Y.
        const u8 items = u8((in.item() ? 1 : 0) | (stickY <= -2 ? 2 : 0) | (stickY >= 2 ? 4 : 0) |
                ((stickY + 8) << 4));
        frames.push_back({u8(in.buttons & 0xb), s8(int(in.stickXRaw) - 7), u8(in.trick), items});
        const auto *kart = Kart::KartObjectManager::Instance()->object(0);
        const auto &p = kart->pos();
        const auto &f = kart->fullRot();
        const auto &m = kart->mainRot();
        for (float v : {p.x, p.y, p.z, f.v.x, f.v.y, f.v.z, f.w, m.v.x, m.v.y, m.v.z, m.w}) {
            poses.push_back(v);
        }
    }
    if (race->stage() != System::RaceManager::Stage::FinishGlobal) {
        std::fprintf(stderr, "%s: the race did not finish\n", argv[1]);
        return 1;
    }
    const auto &timer = race->player().raceTimer();
    const u32 ms = u32(timer.min) * 60000 + u32(timer.sec) * 1000 + u32(timer.mil);
    FILE *out = std::fopen(argv[2], "wb");
    if (!out) {
        return 1;
    }
    size_t size = 0;
    const u8 *raw = Abstract::File::LoadHost(argv[1], size);
    const System::RawGhostFile file(raw);
    const System::GhostFile ghost(file);
    const u32 header[6] = {u32(frames.size()), ms, u32(ghost.course()), u32(ghost.character()),
            u32(ghost.vehicle()), u32(ghost.driftIsAuto())};
    std::fwrite(header, sizeof header, 1, out);
    std::fwrite(file.buffer() + 0x3c, 1, 0x4a, out); // RKG Mii data: the ghost's Mii and name

    std::fwrite(frames.data(), sizeof(Frame), frames.size(), out);
    std::fwrite(poses.data(), sizeof(float), poses.size(), out);
    return std::fclose(out) == 0 ? 0 : 1;
}
