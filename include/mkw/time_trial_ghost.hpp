#pragma once
// Time Trial ghosts: the player's best run on each course, kept as the inputs
// the kart was given every simulation tick from the countdown on, and the
// kart's pose after that tick. The inputs drive a second kart (tricks, drift,
// items and animation); the pose pins it to the recorded line every tick, so
// the smallest divergence can't send it into a wall (it never touches
// anything: KartCollide's intangible mask).
// One file per course and license: ux0:data/mkw/ghosts/lL/course_NN.mkg.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mkw {
struct GhostFrame {
    uint8_t buttons=0;  // Kinoko pad buttons (1 accelerate, 2 brake/drift, 8 drift with accelerate)
    int8_t stick=0;     // the Wii lattice, -7..7
    uint8_t trick=0;    // Kinoko::System::Trick
    uint8_t items=0;    // ItemInput: 1 use, 2 backwards, 4 forwards; high nibble stick Y + 8 (0: neutral, older ghosts)
    static constexpr uint8_t packStickY(int y){return uint8_t((y+8)<<4);}
    int stickY() const{return items>>4?int(items>>4)-8:0;}
};
struct GhostPose {
    float pos[3]{};
    float fullRot[4]{0,0,0,1},mainRot[4]{0,0,0,1}; // x, y, z, w
};
class TimeTrialGhost {
public:
    static constexpr uint32_t Magic=0x4d4b4748; // "MKGH"
    static constexpr uint32_t Version=2;
    struct Header {uint32_t magic,version,course,choice,driftAuto,milliseconds,frames;};
    // Course, racer (the garage choice: driver * VehiclesPerDriver + vehicle)
    // and result; frames and poses start at the countdown, one each per tick.
    unsigned course=0,choice=0;bool driftAuto=false;
    uint32_t milliseconds=0;
    std::vector<GhostFrame> frames;
    std::vector<GhostPose> poses;

    bool valid() const{return milliseconds!=0&&!frames.empty()&&poses.size()==frames.size();}
    void clear(){*this=TimeTrialGhost{};}
    bool load(unsigned course);
    bool save() const;
    // The header alone: racer and time for the menu, without the run.
    static bool peek(unsigned course,Header& out);
    static bool exists(unsigned course){Header h;return peek(course,h);}
    const GhostFrame* frame(std::size_t tick) const{return tick<frames.size()?&frames[tick]:nullptr;}
    const GhostPose* pose(std::size_t tick) const{return tick<poses.size()?&poses[tick]:nullptr;}
};
}
